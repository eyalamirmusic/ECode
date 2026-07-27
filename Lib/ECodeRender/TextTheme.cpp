#include "TextTheme.h"

namespace ecode
{
using namespace eacp;

const Graphics::Color& TextTheme::colorFor(TokenKind kind) const
{
    switch (kind)
    {
        case TokenKind::Keyword:
            return keyword;
        case TokenKind::String:
            return string;
        case TokenKind::Comment:
            return comment;
        case TokenKind::Number:
            return number;
        case TokenKind::Function:
            return function;
        case TokenKind::Type:
            return type;
        case TokenKind::Constant:
            return constant;
        case TokenKind::Operator:
            return operatorColor;
        case TokenKind::Punctuation:
            return punctuation;
        case TokenKind::Preprocessor:
            return preprocessor;
        case TokenKind::Text:
            break;
    }

    return text;
}
} // namespace ecode
