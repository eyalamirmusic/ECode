#pragma once

#include <ECodeCore/Document.h>
#include <ECodeCore/Style.h>

#include <chrono>
#include <memory>

namespace ecode
{
// tree-sitter highlighting, behind ECodeCore's Highlighter interface so nothing
// downstream links or includes tree-sitter.
//
// Only the visible lines are ever queried. tree-sitter parses the whole file —
// that part is fast and incremental — but running the highlight query over an
// entire document would make scrolling cost proportional to file size, which is
// exactly the property the renderer is built to avoid.
//
// The grammar and the compiled query are shared between every instance; see
// SyntaxLanguage. Constructing one of these is cheap.
class SyntaxHighlighter final : public Highlighter
{
public:
    // How long one update() may spend parsing before handing the frame back, for
    // a caller that is drawing.
    //
    // Small enough that a frame which has to parse still arrives — a 60 Hz frame
    // is 16.7 ms — and large enough that an 8,000-line file is coloured within
    // five of them. Only the first sight of a file is ever interrupted: reparses
    // after a keystroke are incremental and finish far inside this.
    static constexpr auto frameParseBudget = std::chrono::microseconds {2000};

    // The default, and it is no budget at all: update() finishes what it started,
    // which is the promise its documentation has always made and what a caller
    // with no frame to protect wants.
    //
    // Deliberately not the other way round. A wall-clock budget generous enough
    // to be invisible in an optimised build interrupts a 500-line parse in a
    // debug one, so a default budget would make what update() returns depend on
    // how the binary was compiled — and the tests are the callers that would
    // discover it. Whoever owns the frame is also the one who knows there is a
    // frame to give back, so the budget belongs at that call site.
    static constexpr auto noParseBudget = std::chrono::microseconds {0};

    explicit SyntaxHighlighter(
        std::chrono::microseconds parseBudget = noParseBudget);

    ~SyntaxHighlighter() override;

    SyntaxHighlighter(const SyntaxHighlighter&) = delete;
    SyntaxHighlighter& operator=(const SyntaxHighlighter&) = delete;

    // False when the grammar or the highlight query failed to load, in which
    // case every line reports as plain text and the editor still works.
    bool isValid() const;

    // Tells the highlighter the document changed, so the next update reparses
    // *incrementally* -- tree-sitter reuses the parts of the tree the edit did
    // not touch, which is the whole reason for using it over a plain lexer.
    //
    // `document` is the state *after* the edit. Without this the alternative is
    // reparsing the file on every keystroke.
    void applyEdit(const Document& document, const TextEdit& edit) override;

    // Discards the tree, so the next update parses from scratch. For opening a
    // file or any change not described by a TextEdit.
    void reset() override;

    // Computes spans for the given line range, parsing first if needed. Call
    // before drawing, with the same range the renderer will draw.
    //
    // Cheap to call every frame: an unchanged range over unchanged text runs no
    // query, and the band computed reaches a margin past the window so that
    // scrolling a line at a time stays inside it.
    //
    // May return with the parse unfinished — see hasPendingWork. Every line then
    // reports as plain text rather than making the frame wait for the tree.
    void update(const Document& document,
                std::size_t firstLine,
                std::size_t lastLine) override;

    // Whether the last update ran out of its budget with the parse incomplete,
    // so the caller should draw what there is and come back.
    bool hasPendingWork() const override;

    const LineStyle& lineStyle(std::size_t line) override;

    // Ticks on every completed reparse and on reset, which are the two things
    // that can change what a line already reported comes back as.
    std::uint64_t version() const override;

    // How many times the highlight query has actually run.
    //
    // For tests rather than for callers, for the reason LineMap::rebuildCount
    // exists: update() returning the same spans it returned last frame is
    // indistinguishable from update() having recomputed them, so nothing an
    // oracle can see says whether the work was skipped. See PLAN.md §9.
    std::uint64_t queries() const;

    // How many times an unfinished parse has been abandoned and begun again,
    // which is what an edit arriving mid-parse forces.
    //
    // For the tests, as above, and it is the one thing about resuming that the
    // spans cannot show: a parse that quietly started over every frame would
    // reach the same tree in the end. Zero across an uninterrupted resume; one
    // per edit that lands on top of one.
    std::uint64_t parseRestarts() const;

    // How many times a tree has been built from nothing rather than from the
    // previous one — the difference between a whole file reparsed and the subtrees
    // an edit did not touch being reused.
    //
    // For the tests, and this is the counter that was missing: the two paths agree
    // on every span, so the suite was green for as long as applyEdit forgot to
    // keep reparse()'s length check in step and *every* keystroke took the slow
    // one. One per file opened; typing must not move it.
    std::uint64_t fullParses() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ecode
