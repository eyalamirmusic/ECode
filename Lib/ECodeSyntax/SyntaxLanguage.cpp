#include "SyntaxLanguage.h"

#include <tree_sitter/api.h>

#include <cstring>
#include <string_view>

extern "C" const TSLanguage* tree_sitter_cpp(void);

namespace ecode
{
// Generated from the two grammars' highlights.scm by FindTreeSitter.cmake.
extern const char* const cppHighlightQuery;

namespace
{
std::uint64_t queryCompilations = 0;

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

SyntaxLanguage::SyntaxLanguage()
    : language(tree_sitter_cpp())
{
    auto errorOffset = std::uint32_t {0};
    auto errorType = TSQueryError {};

    ++queryCompilations;

    highlights =
        ts_query_new(language,
                     cppHighlightQuery,
                     static_cast<std::uint32_t>(std::strlen(cppHighlightQuery)),
                     &errorOffset,
                     &errorType);

    if (highlights == nullptr)
        return;

    disablePredicatedPatterns();
    cacheCaptureKinds();
}

SyntaxLanguage::~SyntaxLanguage()
{
    if (highlights != nullptr)
        ts_query_delete(highlights);
}

const SyntaxLanguage* SyntaxLanguage::cpp()
{
    static const auto instance = SyntaxLanguage {};

    return instance.isValid() ? &instance : nullptr;
}

std::uint64_t SyntaxLanguage::compilations()
{
    return queryCompilations;
}

void SyntaxLanguage::disablePredicatedPatterns()
{
    const auto patterns = ts_query_pattern_count(highlights);

    for (auto index = std::uint32_t {0}; index < patterns; ++index)
    {
        auto count = std::uint32_t {0};
        ts_query_predicates_for_pattern(highlights, index, &count);

        if (count > 0)
            ts_query_disable_pattern(highlights, index);
    }
}

void SyntaxLanguage::cacheCaptureKinds()
{
    const auto captures = ts_query_capture_count(highlights);
    captureKinds.resize(captures, TokenKind::Text);

    for (auto index = std::uint32_t {0}; index < captures; ++index)
    {
        auto length = std::uint32_t {0};
        const auto* name = ts_query_capture_name_for_id(highlights, index, &length);

        if (name != nullptr)
            captureKinds[index] = kindForCapture({name, length});
    }
}

TokenKind SyntaxLanguage::kindOfCapture(std::uint32_t index) const
{
    return index < captureKinds.size() ? captureKinds[index] : TokenKind::Text;
}
} // namespace ecode
