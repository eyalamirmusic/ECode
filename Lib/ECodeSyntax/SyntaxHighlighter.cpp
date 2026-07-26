#include "SyntaxHighlighter.h"

#include "SyntaxLanguage.h"

#include <tree_sitter/api.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecode
{
namespace
{
using Clock = std::chrono::steady_clock;

// What the progress callback below is handed. tree-sitter asks it periodically
// during a parse and stops if it answers true.
struct ParseDeadline
{
    Clock::time_point limit;
};

bool pastDeadline(TSParseState* state)
{
    const auto& deadline = *static_cast<const ParseDeadline*>(state->payload);

    return Clock::now() > deadline.limit;
}

// Reading the whole remainder in one go, since a Document's text is one
// contiguous string. The callback form is only needed because the options
// overload of ts_parser_parse takes a TSInput and there is no _string variant of
// it — the parse is not incremental in its *input*, only in its work.
const char* readFrom(void* payload,
                     std::uint32_t byteIndex,
                     TSPoint,
                     std::uint32_t* bytesRead)
{
    const auto& text = *static_cast<const std::string*>(payload);

    if (byteIndex >= text.size())
    {
        *bytesRead = 0;
        return "";
    }

    *bytesRead = static_cast<std::uint32_t>(text.size() - byteIndex);

    return text.data() + byteIndex;
}

TSInput inputOver(const std::string& text)
{
    auto input = TSInput {};

    input.payload = const_cast<std::string*>(&text);
    input.read = readFrom;
    input.encoding = TSInputEncodingUTF8;

    return input;
}
} // namespace

struct SyntaxHighlighter::Impl
{
    explicit Impl(std::chrono::microseconds budget)
        : parseBudget(budget)
        , language(SyntaxLanguage::cpp())
    {
        if (language == nullptr)
            return; // grammar ABI out of range, or the query did not compile

        parser = ts_parser_new();

        if (!ts_parser_set_language(parser, language->grammar()))
            return;

        cursor = ts_query_cursor_new();
        valid = true;
    }

    ~Impl()
    {
        if (cursor != nullptr)
            ts_query_cursor_delete(cursor);
        if (tree != nullptr)
            ts_tree_delete(tree);
        if (parser != nullptr)
            ts_parser_delete(parser);
    }

    // Runs the parser for at most the budget, or to completion if there is none.
    // Null means the budget ran out with the tree unfinished; the parser keeps
    // its place and the next call resumes from there.
    TSTree* parseWithin(const std::string& text)
    {
        if (parseBudget <= std::chrono::microseconds {0})
            return ts_parser_parse_string(
                parser, tree, text.c_str(), static_cast<std::uint32_t>(text.size()));

        auto deadline = ParseDeadline {Clock::now() + parseBudget};

        auto options = TSParseOptions {};
        options.payload = &deadline;
        options.progress_callback = pastDeadline;

        // Progress is guaranteed however small the budget: tree-sitter asks the
        // callback between units of work rather than before starting one, so a
        // deadline already past still leaves the parse further along than it was.
        return ts_parser_parse_with_options(parser, tree, inputOver(text), options);
    }

    void reparse(const Document& document)
    {
        // A safety net, not the mechanism. Callers are expected to report edits
        // so the reparse can be incremental; a caller that forgets would
        // otherwise get silently stale highlighting, which is a much worse
        // failure than a slow one. Comparing lengths is O(1) and catches the
        // common case of a document swapped out wholesale. Same-length edits
        // still slip through, which is why reporting them is the contract.
        // The tree has to be discarded, not reused: reparsing against a tree
        // that was never told about the change gives tree-sitter a stale
        // starting point and a wrong result. An unreported change means we do
        // not know what to tell it, so the only safe answer is to start over.
        if (tree != nullptr && document.length() != parsedLength)
        {
            ts_tree_delete(tree);
            tree = nullptr;
            dirty = true;
        }

        if (tree != nullptr && !dirty)
            return;

        const auto& text = document.text();

        // An edit that lands while a budgeted parse is still running leaves the
        // parser holding a position in text that no longer exists, and resuming
        // from it would build a tree describing neither version. ts_parser_reset
        // is what throws that half-done work away; without it the resumed parse
        // walks off the end of the shorter string or stops short of the longer
        // one, and either way the tree is wrong rather than merely stale.
        if (parsing && parsingRevision != document.revision())
        {
            ts_parser_reset(parser);
            parsing = false;
            ++restartCount;
        }

        if (!parsing)
        {
            parsingRevision = document.revision();

            // Counted where a parse *begins*, not where one runs: a budgeted
            // parse comes back through here once per frame until it finishes, and
            // all of those are the same parse.
            if (tree == nullptr)
                ++fullParseCount;
        }

        // Parsing against the edited tree is what makes this incremental:
        // tree-sitter reuses every subtree the edit did not touch. It also has to
        // survive an unfinished parse, which is why the old tree is not replaced
        // until a new one actually arrives — a resumed parse is handed the same
        // `tree` again as its starting point, and a caller mid-parse keeps
        // drawing from the spans the old one gave rather than flashing to plain.
        auto* fresh = parseWithin(text);

        if (fresh == nullptr)
        {
            parsing = true;
            return;
        }

        if (tree != nullptr)
            ts_tree_delete(tree);

        tree = fresh;
        parsing = false;
        dirty = false;
        parsedLength = text.size();

        forgetQuery();

        // Every span reported so far came from the tree that was just replaced,
        // so anything caching colours has to be told. See Highlighter::version.
        ++styleVersion;
    }

    // Gives up on a document too large to parse, and frees what was derived from
    // it. Idempotent, which is the part that matters: update() calls this on
    // every frame a large file is on screen, and doing the work each time would
    // tick styleVersion each time — which drops the renderer's whole row cache
    // each time (§7.3), turning a size limit meant to save a frame into a
    // guarantee that no frame is ever cheap.
    void suppress()
    {
        if (suppressed)
            return;

        suppressed = true;

        if (tree != nullptr)
        {
            ts_tree_delete(tree);
            tree = nullptr;
        }

        // A budgeted parse may be halfway through the very document that has
        // just grown past the limit, holding a position in it.
        if (parsing)
        {
            ts_parser_reset(parser);
            parsing = false;
            ++restartCount;
        }

        // Both the spans and the tree they came from are gone, so every line now
        // answers plain — and anything caching colours has to be told, exactly as
        // it would be for a reparse.
        forgetQuery();
        dirty = true;

        ++styleVersion;
    }

    // And back again, for a file that shrinks below the limit — a paste undone,
    // a selection deleted, a smaller file reloaded over a larger one.
    void unsuppress()
    {
        if (!suppressed)
            return;

        suppressed = false;
        dirty = true;

        ++styleVersion;
    }

    // Whether `lines` already answers for [firstLine, lastLine).
    //
    // A subset of what was queried counts, because a line's spans do not depend
    // on the range they were asked for: captures are returned when they overlap
    // the range and are clamped per line, so a line queried as part of a wider
    // window gets exactly the spans it would have got on its own.
    //
    // The revision comparison is redundant against a *reported* edit — that
    // makes the tree dirty, and reparsing forgets the query — so no test can
    // catch its removal. It stays because it is what makes this self-contained
    // rather than correct by a side effect of reparse(), and because it is the
    // only thing that would notice a change nobody reported: an unreported edit
    // of the same length slips past reparse()'s length check.
    bool queryCovers(std::uint64_t revision,
                     std::size_t firstLine,
                     std::size_t lastLine) const
    {
        return queriedRevision == revision && firstLine >= queriedFirst
               && lastLine <= queriedLast;
    }

    void forgetQuery()
    {
        lines.clear();

        // Revisions start at one, so zero is "nothing has been queried".
        queriedRevision = 0;
    }

    // Advances a point over a run of text, for deriving the edit's end points
    // without walking the document.
    static TSPoint advance(TSPoint point, std::string_view text)
    {
        for (const auto character: text)
        {
            if (character == '\n')
            {
                ++point.row;
                point.column = 0;
            }
            else
            {
                ++point.column;
            }
        }

        return point;
    }

    void applyEdit(const Document& document, const TextEdit& edit)
    {
        if (tree == nullptr)
        {
            dirty = true;
            return;
        }

        // The edit's start is unchanged by the edit, so its position can come
        // from the document as it is now. The two end points are that start
        // advanced over the removed and inserted text respectively -- both
        // small -- so none of this walks the file.
        const auto startRow = document.lineAt(edit.start);
        const auto start =
            TSPoint {static_cast<std::uint32_t>(startRow),
                     static_cast<std::uint32_t>(document.columnAt(edit.start))};

        auto change = TSInputEdit {};
        change.start_byte = static_cast<std::uint32_t>(edit.start);
        change.old_end_byte =
            static_cast<std::uint32_t>(edit.start + edit.removed.size());
        change.new_end_byte =
            static_cast<std::uint32_t>(edit.start + edit.inserted.size());
        change.start_point = start;
        change.old_end_point = advance(start, edit.removed);
        change.new_end_point = advance(start, edit.inserted);

        // Mutates the tree in place, marking the affected range stale.
        ts_tree_edit(tree, &change);
        dirty = true;

        // The tree has now been told about text of the new length, so reparse()'s
        // sanity check must not read this as a change nobody reported.
        //
        // Without this line the check fired on every edit that moved the length —
        // which is very nearly every edit — and threw the tree away, so the
        // incremental reparse this whole method exists for never ran. Typing one
        // character into an 8,000-line file cost a full parse, 9.6 ms against the
        // 0.24 ms it costs now; the only edits that ever reached the fast path
        // were the same-length ones, which are exactly the ones the check cannot
        // catch. Invisible to every test, because they compare the spans against a
        // fresh parse and a fresh parse is what the slow path does: PLAN.md §9, an
        // oracle proves the answer, never the path. fullParses() is the counter
        // that makes it visible.
        parsedLength = document.length();
    }

    void highlight(const Document& document,
                   std::size_t firstLine,
                   std::size_t lastLine)
    {
        forgetQuery();

        if (!valid || tree == nullptr || firstLine >= lastLine)
            return;

        queriedRevision = document.revision();
        queriedFirst = firstLine;
        queriedLast = lastLine;

        ++queryCount;

        // A capture is returned when it *overlaps* the range, not only when it
        // is contained, so multi-line comments and strings crossing into view
        // still arrive — and have to be clamped per line below.
        const auto start = TSPoint {static_cast<std::uint32_t>(firstLine), 0};
        const auto end = TSPoint {static_cast<std::uint32_t>(lastLine), 0};

        ts_query_cursor_set_point_range(cursor, start, end);
        ts_query_cursor_exec(cursor, language->query(), ts_tree_root_node(tree));

        // One TokenKind per byte of each visible line, painted in delivery
        // order. Captures overlap constantly — C's catch-all @variable covers
        // identifiers that later, more specific patterns also match — and since
        // more specific patterns appear later in the query file, letting the
        // last write win resolves precedence correctly. Painting bytes handles
        // partial overlaps that interval bookkeeping gets wrong, and the cost is
        // bounded by what is on screen, not by the file.
        //
        // Precedence-by-query-order is the convention editors settled on, not
        // something libtree-sitter guarantees.
        paint.clear();
        paint.resize(lastLine - firstLine);

        for (auto line = firstLine; line < lastLine; ++line)
            paint[line - firstLine].assign(document.line(line).size(),
                                           TokenKind::Text);

        auto match = TSQueryMatch {};
        auto captureIndex = std::uint32_t {0};

        // next_capture, not next_match: it yields one stream ordered by start
        // byte, where next_match can hand back a match whose captures precede
        // ones already delivered.
        while (ts_query_cursor_next_capture(cursor, &match, &captureIndex))
        {
            if (captureIndex >= match.capture_count)
                continue;

            const auto& capture = match.captures[captureIndex];
            const auto kind = language->kindOfCapture(capture.index);

            if (kind == TokenKind::Text)
                continue;

            const auto from = ts_node_start_point(capture.node);
            const auto to = ts_node_end_point(capture.node);

            for (auto row = from.row; row <= to.row; ++row)
            {
                if (row < firstLine || row >= lastLine)
                    continue;

                auto& row_ = paint[row - firstLine];

                const auto begin = row == from.row ? from.column : 0;
                const auto finish = row == to.row ? to.column : row_.size();

                for (auto column = begin; column < finish && column < row_.size();
                     ++column)
                    row_[column] = kind;
            }
        }

        buildSpans(firstLine);
    }

    // Run-length encodes each painted line into spans.
    void buildSpans(std::size_t firstLine)
    {
        for (std::size_t offset = 0; offset < paint.size(); ++offset)
        {
            const auto& row = paint[offset];
            auto spans = LineStyle {};

            for (std::size_t column = 0; column < row.size();)
            {
                const auto kind = row[column];
                auto run = column;

                while (run < row.size() && row[run] == kind)
                    ++run;

                if (kind != TokenKind::Text)
                    spans.push_back({column, run - column, kind});

                column = run;
            }

            if (!spans.empty())
                lines.emplace(firstLine + offset, std::move(spans));
        }
    }

    std::chrono::microseconds parseBudget;

    // Shared, immutable, and outlives every highlighter; see SyntaxLanguage.
    const SyntaxLanguage* language = nullptr;

    TSParser* parser = nullptr;
    TSQueryCursor* cursor = nullptr;
    TSTree* tree = nullptr;

    std::vector<std::vector<TokenKind>> paint;
    std::unordered_map<std::size_t, LineStyle> lines;

    bool valid = false;

    // Whether the tree needs reparsing before the next query.
    bool dirty = true;

    // Whether a parse ran out of budget partway and is waiting to be resumed.
    bool parsing = false;

    // Whether the last document seen was over the size limit, so nothing is
    // parsed and every line answers plain.
    bool suppressed = false;

    // Which document state the unfinished parse is reading, so an edit arriving
    // mid-parse is noticed rather than resumed over.
    std::uint64_t parsingRevision = 0;

    // Length of the text the tree was built from, for the sanity check above.
    std::size_t parsedLength = 0;

    // What `lines` currently answers for: which document state, and which band
    // of it. The query is by far the most expensive thing a frame asks for, and
    // an idle editor asks for the same band of the same text every time.
    std::uint64_t queriedRevision = 0;
    std::size_t queriedFirst = 0;
    std::size_t queriedLast = 0;

    std::uint64_t styleVersion = 1;

    // Only the tests read these; see SyntaxHighlighter::queries and
    // parseRestarts.
    std::uint64_t queryCount = 0;
    std::uint64_t restartCount = 0;
    std::uint64_t fullParseCount = 0;
};

SyntaxHighlighter::SyntaxHighlighter(std::chrono::microseconds parseBudget)
    : impl(std::make_unique<Impl>(parseBudget))
{
}

SyntaxHighlighter::~SyntaxHighlighter() = default;

bool SyntaxHighlighter::isValid() const
{
    return impl->valid;
}

void SyntaxHighlighter::applyEdit(const Document& document, const TextEdit& edit)
{
    if (!impl->valid)
        return;

    impl->applyEdit(document, edit);
}

void SyntaxHighlighter::reset()
{
    if (!impl->valid)
        return;

    if (impl->tree != nullptr)
    {
        ts_tree_delete(impl->tree);
        impl->tree = nullptr;
    }

    // A parse in flight was reading the document this reset is throwing away, so
    // its position means nothing now.
    if (impl->parsing)
    {
        ts_parser_reset(impl->parser);
        impl->parsing = false;
        ++impl->restartCount;
    }

    impl->dirty = true;
    impl->forgetQuery();

    ++impl->styleVersion;
}

void SyntaxHighlighter::update(const Document& document,
                               std::size_t firstLine,
                               std::size_t lastLine)
{
    if (!impl->valid)
        return;

    // Before the reparse, because the whole point is that the reparse does not
    // run. Asked of the document handed in rather than remembered from the file
    // being opened, so a buffer crossing the limit in either direction is
    // noticed wherever it happens — a paste, an undo, a reload — with no event
    // to subscribe to and nothing to keep in step.
    if (document.length() > maxHighlightedBytes)
    {
        impl->suppress();
        return;
    }

    impl->unsuppress();

    // Reparsing first, because it is what decides whether the answers already
    // held are still about this text: a reparse forgets them.
    impl->reparse(document);

    // A parse still in flight means the tree does not describe this text yet.
    // Querying it anyway would spend the frame's most expensive call on spans
    // that are about to be thrown away — and on the first sight of a file there
    // is no tree to query at all. So whatever is held stays held: nothing on the
    // first parse, which draws as plain text, and the previous colouring on a
    // reparse, which is what stops an edit flashing the file to plain.
    if (impl->parsing)
        return;

    // An editor sitting still asks for the same lines of the same document on
    // every frame — a caret blink, a hover, a scrollbar. Running the query
    // again would be the single most expensive thing in that frame, and it
    // would arrive at exactly the answer already stored.
    if (impl->queryCovers(document.revision(), firstLine, lastLine))
        return;

    // Widened past what was asked for, because scrolling asks for a window one
    // line further down each frame and an exact answer is a miss every time.
    // A margin costs a longer query on the frames that do run one — the cost is
    // linear in the lines queried — and buys a whole margin's worth of
    // scrolling that runs none at all.
    const auto margin = std::size_t {96};

    const auto from = firstLine > margin ? firstLine - margin : 0;
    const auto to = std::min(lastLine + margin, document.lineCount());

    impl->highlight(document, from, std::max(to, lastLine));
}

bool SyntaxHighlighter::hasPendingWork() const
{
    return impl->parsing;
}

bool SyntaxHighlighter::isTooLargeToHighlight() const
{
    return impl->suppressed;
}

std::uint64_t SyntaxHighlighter::version() const
{
    return impl->styleVersion;
}

std::uint64_t SyntaxHighlighter::queries() const
{
    return impl->queryCount;
}

std::uint64_t SyntaxHighlighter::parseRestarts() const
{
    return impl->restartCount;
}

std::uint64_t SyntaxHighlighter::fullParses() const
{
    return impl->fullParseCount;
}

const LineStyle& SyntaxHighlighter::lineStyle(std::size_t line)
{
    static const auto plain = LineStyle {};

    const auto found = impl->lines.find(line);

    return found != impl->lines.end() ? found->second : plain;
}
} // namespace ecode
