#pragma once

#include <ECodeCore/Document.h>
#include <ECodeCore/Style.h>

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
class SyntaxHighlighter final : public Highlighter
{
public:
    SyntaxHighlighter();
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
    void applyEdit(const Document& document, const TextEdit& edit);

    // Discards the tree, so the next update parses from scratch. For opening a
    // file or any change not described by a TextEdit.
    void reset();

    // Computes spans for the given line range, parsing first if needed. Call
    // before drawing, with the same range the renderer will draw; lineStyle()
    // outside that range returns empty.
    void update(const Document& document,
                std::size_t firstLine,
                std::size_t lastLine) override;

    const LineStyle& lineStyle(std::size_t line) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ecode
