#pragma once

#include <ECodeCore/Style.h>

#include <cstdint>
#include <vector>

struct TSLanguage;
struct TSQuery;

namespace ecode
{
// A grammar and its compiled highlight query — the half of highlighting that
// every document of a language has in common.
//
// Internal to ECodeSyntax. It is the one header here that names tree-sitter
// types, and nothing outside the module should include it.
//
// Split out from SyntaxHighlighter because compiling the query is by far the
// most expensive thing constructing one does: ts_query_new analyses each of C++'s
// 77 patterns against the whole grammar, which measures 14 ms, against 10 ms to
// parse an 8,000-line file and 0.06 ms for an idle frame. It depends on nothing
// but the grammar and the query source, so it is the same object for every file —
// and PLAN.md §7.8 gave every open tab a highlighter of its own, which turned one
// 14 ms cost into one per tab. Opening twenty files cost 295 ms, of which 280 was
// this, compiled twenty times to the same answer.
//
// Everything derived from a *document* — the parser, its tree, the query cursor —
// stays with the highlighter. This holds only what is immutable once built, which
// is what makes sharing it safe: a TSQuery may be executed by any number of
// cursors, and ts_query_disable_pattern, the one call that mutates it, happens
// here before anyone else can see it.
class SyntaxLanguage
{
public:
    // C++, built on first use and shared from then on. Null if the grammar's ABI
    // is out of range or the query fails to compile, in which case every line
    // reports as plain text and the editor still works.
    //
    // Lives for the process. A compiled grammar has no per-document state to go
    // stale and nothing to free that matters, so the alternative — reference
    // counting it so the last closed tab throws it away — would only mean paying
    // the 14 ms again on the next open.
    static const SyntaxLanguage* cpp();

    // How many times a query has actually been compiled.
    //
    // For the tests rather than for callers, in the shape LineMap::rebuildCount
    // and SyntaxHighlighter::queries established: a shared query and a query
    // compiled per highlighter produce identical spans, so nothing an oracle can
    // see says which happened. See PLAN.md §9 — an oracle proves the answer,
    // never the path.
    static std::uint64_t compilations();

    const TSLanguage* grammar() const { return language; }

    // Const because a cursor takes it as const and nothing may mutate it after
    // construction; see the note above about disabled patterns.
    const TSQuery* query() const { return highlights; }

    // What a capture index means in the theme's vocabulary. Resolved once at
    // construction, since a capture's index is stable for the query's lifetime
    // and the alternative is a string comparison per capture per frame.
    TokenKind kindOfCapture(std::uint32_t index) const;

    ~SyntaxLanguage();

    SyntaxLanguage(const SyntaxLanguage&) = delete;
    SyntaxLanguage& operator=(const SyntaxLanguage&) = delete;

private:
    SyntaxLanguage();

    bool isValid() const { return highlights != nullptr; }

    // libtree-sitter parses `#match?` predicates but never evaluates them, so a
    // pattern relying on one fires unconditionally: C's ALL-CAPS rule tags every
    // lowercase identifier as @constant, and C++'s namespace rule tags lowercase
    // namespaces as @type. Wrong highlighting is worse than none, and the
    // alternative — reimplementing the regexes — buys two patterns out of
    // seventy-seven.
    void disablePredicatedPatterns();

    void cacheCaptureKinds();

    const TSLanguage* language = nullptr;
    TSQuery* highlights = nullptr;

    std::vector<TokenKind> captureKinds;
};
} // namespace ecode
