#include "SyntaxHighlighter.h"

#include <tree_sitter/api.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" const TSLanguage* tree_sitter_cpp(void);

namespace ecode
{
// Generated from the two grammars' highlights.scm by FindTreeSitter.cmake.
extern const char* const cppHighlightQuery;

namespace
{
// tree-sitter capture names -> the vocabulary the theme understands.
//
// `variable` is deliberately Text: C's query opens with a catch-all
// `(identifier) @variable`, so treating it as styled would colour every
// identifier in the file and drown out the captures that actually mean
// something.
TokenKind kindForCapture(std::string_view capture)
{
    // Names are hierarchical (`function.special`); match on the root so
    // unrecognised refinements fall back to their family rather than to Text.
    if (const auto dot = capture.find('.'); dot != std::string_view::npos)
        capture = capture.substr(0, dot);

    if (capture == "keyword")
        return TokenKind::Keyword;
    if (capture == "string" || capture == "character")
        return TokenKind::String;
    if (capture == "comment")
        return TokenKind::Comment;
    if (capture == "number")
        return TokenKind::Number;
    if (capture == "function")
        return TokenKind::Function;
    if (capture == "type")
        return TokenKind::Type;
    if (capture == "constant")
        return TokenKind::Constant;
    if (capture == "operator")
        return TokenKind::Operator;
    if (capture == "delimiter" || capture == "punctuation")
        return TokenKind::Punctuation;
    if (capture == "preproc")
        return TokenKind::Preprocessor;

    return TokenKind::Text;
}
} // namespace

struct SyntaxHighlighter::Impl
{
    Impl()
    {
        parser = ts_parser_new();

        if (!ts_parser_set_language(parser, tree_sitter_cpp()))
            return; // grammar ABI outside the runtime's supported range

        auto errorOffset = std::uint32_t {0};
        auto errorType = TSQueryError {};

        query =
            ts_query_new(tree_sitter_cpp(),
                         cppHighlightQuery,
                         static_cast<std::uint32_t>(std::strlen(cppHighlightQuery)),
                         &errorOffset,
                         &errorType);

        if (query == nullptr)
            return;

        disablePredicatedPatterns();
        cacheCaptureKinds();

        cursor = ts_query_cursor_new();
        valid = true;
    }

    ~Impl()
    {
        if (cursor != nullptr)
            ts_query_cursor_delete(cursor);
        if (query != nullptr)
            ts_query_delete(query);
        if (tree != nullptr)
            ts_tree_delete(tree);
        if (parser != nullptr)
            ts_parser_delete(parser);
    }

    // libtree-sitter parses `#match?` predicates but never evaluates them, so a
    // pattern that relies on one fires unconditionally: C's ALL-CAPS rule tags
    // every lowercase identifier as @constant, and C++'s namespace rule tags
    // lowercase namespaces as @type. Wrong highlighting is worse than none, and
    // the alternative — reimplementing the regexes here — buys two patterns out
    // of seventy-seven. Disable them instead.
    void disablePredicatedPatterns()
    {
        const auto patterns = ts_query_pattern_count(query);

        for (auto index = std::uint32_t {0}; index < patterns; ++index)
        {
            auto count = std::uint32_t {0};
            ts_query_predicates_for_pattern(query, index, &count);

            if (count > 0)
                ts_query_disable_pattern(query, index);
        }
    }

    // A capture's index is stable for the query's lifetime, so the name lookup
    // happens once here rather than per capture per frame.
    void cacheCaptureKinds()
    {
        const auto captures = ts_query_capture_count(query);
        captureKinds.resize(captures, TokenKind::Text);

        for (auto index = std::uint32_t {0}; index < captures; ++index)
        {
            auto length = std::uint32_t {0};
            const auto* name = ts_query_capture_name_for_id(query, index, &length);

            if (name != nullptr)
                captureKinds[index] = kindForCapture({name, length});
        }
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
        auto* previous = tree;

        // Parsing against the edited tree is what makes this incremental:
        // tree-sitter reuses every subtree the edit did not touch. Passing
        // nullptr instead would reparse the file.
        tree = ts_parser_parse_string(
            parser, previous, text.c_str(), static_cast<std::uint32_t>(text.size()));

        if (previous != nullptr)
            ts_tree_delete(previous);

        dirty = false;
        parsedLength = text.size();
        lines.clear();
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
    }

    void highlight(const Document& document,
                   std::size_t firstLine,
                   std::size_t lastLine)
    {
        lines.clear();

        if (!valid || tree == nullptr || firstLine >= lastLine)
            return;

        // A capture is returned when it *overlaps* the range, not only when it
        // is contained, so multi-line comments and strings crossing into view
        // still arrive — and have to be clamped per line below.
        const auto start = TSPoint {static_cast<std::uint32_t>(firstLine), 0};
        const auto end = TSPoint {static_cast<std::uint32_t>(lastLine), 0};

        ts_query_cursor_set_point_range(cursor, start, end);
        ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));

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
            const auto kind = capture.index < captureKinds.size()
                                  ? captureKinds[capture.index]
                                  : TokenKind::Text;

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

    TSParser* parser = nullptr;
    TSQuery* query = nullptr;
    TSQueryCursor* cursor = nullptr;
    TSTree* tree = nullptr;

    std::vector<TokenKind> captureKinds;
    std::vector<std::vector<TokenKind>> paint;
    std::unordered_map<std::size_t, LineStyle> lines;

    bool valid = false;

    // Whether the tree needs reparsing before the next query.
    bool dirty = true;

    // Length of the text the tree was built from, for the sanity check above.
    std::size_t parsedLength = 0;
};

SyntaxHighlighter::SyntaxHighlighter()
    : impl(std::make_unique<Impl>())
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

    impl->dirty = true;
    impl->lines.clear();
}

void SyntaxHighlighter::update(const Document& document,
                               std::size_t firstLine,
                               std::size_t lastLine)
{
    if (!impl->valid)
        return;

    impl->reparse(document);
    impl->highlight(document, firstLine, lastLine);
}

const LineStyle& SyntaxHighlighter::lineStyle(std::size_t line)
{
    static const auto plain = LineStyle {};

    const auto found = impl->lines.find(line);

    return found != impl->lines.end() ? found->second : plain;
}
} // namespace ecode
