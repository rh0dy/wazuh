#include "hlp/logpar.hpp"

#include <stdexcept>

#include <fmt/format.h>

namespace hlp::logpar::parser
{
parsec::Parser<char> pChar(std::string chars)
{
    return [=](std::string_view t, size_t i)
    {
        if (i < t.size())
        {
            if (chars.find(t[i]) != std::string::npos)
            {
                return parsec::makeSuccess(t[i], t, i + 1);
            }
            else
            {
                return parsec::makeError<char>(
                    parsec::Error {
                        fmt::format("Expected one of '{}', found '{}'", chars, t[i])},
                    t,
                    i);
            }
        }
        else
        {
            return parsec::makeError<char>(
                parsec::Error {fmt::format("Expected one of '{}', found EOF", chars)},
                t,
                i);
        }
    };
}

parsec::Parser<char> pNotChar(std::string chars)
{
    return [=](std::string_view t, size_t i)
    {
        if (i < t.size())
        {
            if (chars.find(t[i]) == std::string::npos)
            {
                return parsec::makeSuccess(t[i], t, i + 1);
            }
            else
            {
                return parsec::makeError<char>(
                    parsec::Error {
                        fmt::format("Expected not one of '{}', found '{}'", chars, t[i])},
                    t,
                    i);
            }
        }
        else
        {
            return parsec::makeError<char>(
                parsec::Error {fmt::format("Expected not one of '{}', found EOF", chars)},
                t,
                i);
        }
    };

} // namespace

parsec::Parser<char> pEscapedChar(std::string chars, char esc)
{
    return pChar(std::string {esc}) >> pChar(std::string {chars + esc});
}

parsec::Parser<std::string> pRawLiteral(std::string reservedChars, char esc)
{
    return parsec::fmap<std::string, parsec::Values<char>>(
        [=](parsec::Values<char> values)
        {
            std::string result {};
            for (auto c : values)
            {
                result += c;
            }
            return result;
        },
        parsec::many(pNotChar(reservedChars + esc) | pEscapedChar(reservedChars, esc)));
}

parsec::Parser<std::string> pRawLiteral1(std::string reservedChars, char esc)
{
    return parsec::fmap<std::string, parsec::Values<char>>(
        [=](parsec::Values<char> values)
        {
            std::string result {};
            for (auto c : values)
            {
                result += c;
            }
            return result;
        },
        parsec::many1(pNotChar(reservedChars + esc) | pEscapedChar(reservedChars, esc)));
}

parsec::Parser<char> pCharAlphaNum(std::string extended)
{
    return [=](std::string_view t, size_t i)
    {
        if (i < t.size())
        {
            if ((t[i] >= 'a' && t[i] <= 'z') || (t[i] >= 'A' && t[i] <= 'Z')
                || (t[i] >= '0' && t[i] <= '9')
                || extended.find(t[i]) != std::string::npos)
            {
                return parsec::makeSuccess(t[i], t, i + 1);
            }
            else
            {
                return parsec::makeError<char>(
                    parsec::Error {
                        fmt::format("Expected alphanumeric, found '{}'", t[i])},
                    t,
                    i);
            }
        }
        else
        {
            return parsec::makeError<char>(
                parsec::Error {fmt::format("Expected alphanumeric, found EOF")}, t, i);
        }
    };
}

parsec::Parser<parsec::Values<std::string>> pArgs()
{
    auto pArg = pChar({syntax::EXPR_ARG_SEP}) >> pRawLiteral(
                    {syntax::EXPR_ARG_SEP, syntax::EXPR_END}, {syntax::EXPR_ESCAPE});

    return parsec::many(pArg);
}

parsec::Parser<FieldName> pFieldName()
{
    parsec::Parser<std::string> pCustom = [](std::string_view text, size_t i)
    {
        if (i < text.size())
        {
            if (text[i] == syntax::EXPR_CUSTOM_FIELD)
            {
                return parsec::makeSuccess(std::string {text[i]}, text, i + 1);
            }
            else
            {
                return parsec::makeSuccess(std::string {}, text, i);
            }
        }
        else
        {
            return parsec::makeError<std::string>(
                parsec::Error {
                    fmt::format("Optional '{}', found EOF", syntax::EXPR_CUSTOM_FIELD)},
                text,
                i);
        }
    };

    std::string extendedChars = syntax::EXPR_FIELD_EXTENDED_CHARS;
    extendedChars += syntax::EXPR_FIELD_SEP;
    auto pName = parsec::fmap<std::string, std::tuple<char, parsec::Values<char>>>(
        [](auto t)
        {
            std::string result {};
            result.push_back(std::get<0>(t));
            for (auto c : std::get<1>(t))
            {
                result += c;
            }
            return result;
        },
        pCharAlphaNum() & parsec::many(pCharAlphaNum(extendedChars)));
    parsec::M<FieldName, std::string> m = [=](auto customS)
    {
        if (!customS.empty())
        {
            return parsec::fmap<FieldName, std::string>(
                [](auto s) {
                    return FieldName {s, true};
                },
                parsec::fmap<std::string, std::string>(
                    [=](auto s) { return customS + s; }, parsec::opt(pName)));
        }
        else
        {
            return parsec::fmap<FieldName, std::string>(
                [](auto s) {
                    return FieldName {s, false};
                },
                pName);
        }
    };

    return pCustom >>= m;
}

parsec::Parser<Field> pField()
{
    auto start = pChar({syntax::EXPR_BEGIN});
    auto end = pChar({syntax::EXPR_END});
    auto pField = parsec::
        fmap<Field, std::tuple<std::tuple<char, FieldName>, parsec::Values<std::string>>>(
            [](auto t)
            {
                const auto& [tInner, args] = t;
                const auto& [c, name] = tInner;
                return Field {name, args, c != '\0'};
            },
            (parsec::opt(pChar({syntax::EXPR_OPT})) & pFieldName()) & pArgs());
    return start >> pField << end;
}

parsec::Parser<Literal> pLiteral()
{
    std::string reservedLiteralChars {};
    reservedLiteralChars.push_back(syntax::EXPR_BEGIN);
    reservedLiteralChars.push_back(syntax::EXPR_OPT);
    reservedLiteralChars.push_back(syntax::EXPR_GROUP_BEGIN);
    reservedLiteralChars.push_back(syntax::EXPR_GROUP_END);

    return parsec::fmap<Literal, std::string>(
        [](auto s) { return Literal {s}; },
        pRawLiteral1(reservedLiteralChars, {syntax::EXPR_ESCAPE}));
}

parsec::Parser<Choice> pChoice()
{
    auto p = (pField() << pChar({syntax::EXPR_OPT})) & pField();

    return [=](std::string_view text, size_t i)
    {
        auto res = p(text, i);
        if (res.failure())
        {
            return parsec::makeError<Choice>(res.error(), text, i);
        }
        else
        {
            const auto& [f1, f2] = res.value();
            if (f1.optional)
            {
                return parsec::makeError<Choice>(
                    parsec::Error {fmt::format(
                        "Expected field, found optional field '{}'", f1.name.value)},
                    text,
                    i);
            }

            if (f2.optional)
            {
                return parsec::makeError<Choice>(
                    parsec::Error {fmt::format(
                        "Expected field, found optional field '{}'", f2.name.value)},
                    text,
                    i);
            }

            return parsec::makeSuccess(Choice {f1, f2}, text, res.index);
        }
    };
}

parsec::Parser<parsec::Values<ParserInfo>> pExpr()
{
    auto pF =
        parsec::fmap<ParserInfo, Field>([](auto f) { return ParserInfo {f}; }, pField());
    auto pL = parsec::fmap<ParserInfo, Literal>([](auto l) { return ParserInfo {l}; },
                                                pLiteral());
    auto pC = parsec::fmap<ParserInfo, Choice>([](auto c) { return ParserInfo {c}; },
                                               pChoice());

    auto pExpr = pC | pF | pL;
    return parsec::many1(pExpr);
}

namespace
{
parsec::Result<Group> pG(std::string_view text, size_t i)
{
    auto resStart =
        (pChar({syntax::EXPR_GROUP_BEGIN}) & pChar({syntax::EXPR_OPT}))(text, i);
    auto lastIdx = i;
    if (resStart.failure())
    {
        return parsec::makeError<Group>(resStart.error(), text, i);
    }
    lastIdx = resStart.index;

    parsec::Parser<Group> pGfn = pG;
    auto pGmap = parsec::fmap<parsec::Values<ParserInfo>, Group>(
        [](auto v) { return parsec::Values<ParserInfo> {v}; }, pGfn);
    auto pBody = parsec::fmap<parsec::Values<ParserInfo>,
                              parsec::Values<parsec::Values<ParserInfo>>>(
        [](auto v)
        {
            parsec::Values<ParserInfo> merge {};
            for (auto& vv : v)
            {
                merge.splice(merge.end(), vv);
            }
            return merge;
        },
        parsec::many1(pExpr() | pGmap));

    auto resBody = pBody(text, lastIdx);
    if (resBody.failure())
    {
        return parsec::makeError<Group>(resBody.error(), text, i);
    }
    lastIdx = resBody.index;

    auto resEnd = pChar({syntax::EXPR_GROUP_END})(text, lastIdx);
    if (resEnd.failure())
    {
        return parsec::makeError<Group>(resEnd.error(), text, i);
    }

    return parsec::makeSuccess(Group {resBody.value()}, text, resEnd.index);
}
} // namespace

parsec::Parser<Group> pGroup()
{
    return [](std::string_view text, size_t i)
    {
        return pG(text, i);
    };
}

parsec::Parser<std::list<ParserInfo>> pLogpar()
{
    auto pE = pExpr();
    auto pG = pGroup();
    auto p = pE
             | parsec::fmap<std::list<ParserInfo>, Group>(
                 [](auto g) { return std::list<ParserInfo> {g}; }, pG);
    return parsec::fmap<std::list<ParserInfo>,
                        parsec::Values<parsec::Values<ParserInfo>>>(
               [](auto v)
               {
                   std::list<ParserInfo> merged;
                   for (auto& l : v)
                   {
                       merged.splice(merged.end(), l);
                   }
                   return merged;
               },
               parsec::many(p))
           << pEof<std::list<ParserInfo>>();
}

} // namespace hlp::logpar::parser

namespace hlp::logpar
{
Logpar::Logpar(const json::Json& ecsFieldTypes)
{
    if (!ecsFieldTypes.isObject())
    {
        // TODO: check message
        throw std::runtime_error("ECS field types must be an object");
    }

    if (ecsFieldTypes.size() == 0)
    {
        // TODO: check message
        throw std::runtime_error("ECS field types must not be empty");
    }

    // Populate m_fieldTypes
    auto obj = ecsFieldTypes.getObject().value();
    for (const auto& [key, value] : obj)
    {
        if (!value.isString())
        {
            throw std::runtime_error(
                fmt::format("When loading logpar schema fields, field '{}' must "
                            "be a string with the name of the type",
                            key));
        }

        const auto schemaType = strToSchemaType(value.getString().value());
        if (schemaType == SchemaType::ERROR_TYPE)
        {
            throw std::runtime_error(
                fmt::format("When loading logpar schema fields, type '{}' in schema "
                            "field '{}' is not supported",
                            value.getString().value(),
                            key));
        }

        m_fieldTypes[key] = schemaType;
    }

    m_typeParsers = {{SchemaType::IP, ParserType::P_IP},
                     {SchemaType::LONG, ParserType::P_LONG},
                     {SchemaType::OBJECT, ParserType::P_TEXT},
                     {SchemaType::GEO_POINT, ParserType::P_TEXT},
                     {SchemaType::KEYWORD, ParserType::P_TEXT},
                     {SchemaType::NESTED, ParserType::P_TEXT},
                     {SchemaType::SCALED_FLOAT, ParserType::P_SCALED_FLOAT},
                     {SchemaType::TEXT, ParserType::P_TEXT},
                     {SchemaType::BOOLEAN, ParserType::P_BOOL},
                     {SchemaType::DATE, ParserType::P_DATE},
                     {SchemaType::FLOAT, ParserType::P_FLOAT},
                     {SchemaType::URL, ParserType::P_URI},
                     {SchemaType::USER_AGENT, ParserType::P_USER_AGENT}};

    m_parserBuilders = {};
}

parsec::Parser<json::Json>
Logpar::buildLiteralParser(const parser::Literal& literal) const
{
    if (m_parserBuilders.count(ParserType::P_LITERAL) == 0)
    {
        throw std::runtime_error(fmt::format("Parser type '{}' not found",
                                             parserTypeToStr(ParserType::P_LITERAL)));
    }

    return m_parserBuilders.at(ParserType::P_LITERAL)({}, {literal.value});
}

parsec::Parser<json::Json>
Logpar::buildFieldParser(const parser::Field& field,
                         std::optional<std::string> endToken) const
{
    // Get type of the parser to be built
    ParserType type;
    std::vector<std::string> args(field.args.begin(), field.args.end());
    // Custom field
    if (field.name.custom)
    {
        // Custom fields use specified parser in args or text parser by default
        if (args.size() == 0)
        {
            type = ParserType::P_TEXT;
        }
        else
        {
            type = strToParserType(args.front());
            if (type == ParserType::ERROR_TYPE)
            {
                throw std::runtime_error(
                    fmt::format("Parser type '{}' not found", field.args.front()));
            }
            args.erase(args.begin());
        }
    }
    // Schema field
    else
    {
        if (m_fieldTypes.count(field.name.value) == 0)
        {
            throw std::runtime_error(
                fmt::format("Field '{}' not found in schema", field.name.value));
        }

        const auto schemaType = m_fieldTypes.at(field.name.value);
        if (m_typeParsers.count(schemaType) == 0)
        {
            throw std::runtime_error(fmt::format(
                "Parser type for ECS type '{}' not found", schemaTypeToStr(schemaType)));
        }

        type = m_typeParsers.at(schemaType);
    }

    if (m_parserBuilders.count(type) == 0)
    {
        throw std::runtime_error(
            fmt::format("Parser type '{}' not found", parserTypeToStr(type)));
    }

    // Get parser from type
    auto p = m_parserBuilders.at(type)(endToken, args);
    parsec::Parser<json::Json> ret;

    // Build target field
    // Special case <~> -> If name is ~, ignore output
    if (field.name.value == std::string {syntax::EXPR_CUSTOM_FIELD})
    {
        ret = parsec::replace<json::Json, json::Json>(p, {});
    }
    // Build json object -> {"name": value}
    else
    {
        auto targetField = field.name.value;
        targetField = json::Json::formatJsonPath(targetField);

        ret = parsec::fmap<json::Json, json::Json>(
            [targetField](auto v)
            {
                json::Json doc;
                // TODO: make moveable
                doc.set(targetField, v);
                return doc;
            },
            p);
    }

    // If field is optional, wrap in optional parser
    if (field.optional)
    {
        ret = parsec::opt(ret);
    }

    return ret;
}

parsec::Parser<json::Json>
Logpar::buildChoiceParser(const parser::Choice& choice,
                          std::optional<std::string> endToken) const
{
    auto p1 = buildFieldParser(choice.left, endToken);
    auto p2 = buildFieldParser(choice.right, endToken);

    return p1 | p2;
}

parsec::Parser<json::Json> Logpar::buildGroupOptParser(const parser::Group& group) const
{
    auto p = buildParsers(group.children);
    return parsec::opt(p);
}

parsec::Parser<json::Json>
Logpar::buildParsers(const std::list<parser::ParserInfo>& parserInfos) const
{
    std::list<parsec::Parser<json::Json>> parsers;
    for (auto parserInfo = parserInfos.begin(); parserInfo != parserInfos.end();
         ++parserInfo)
    {
        // Get end token from next parser info if it exists
        auto next = std::next(parserInfo);
        std::optional<std::string> endToken = std::nullopt;
        if (next == parserInfos.end())
        {
            endToken = "";
        }
        else
        {
            if (std::holds_alternative<parser::Literal>(*next))
            {
                endToken = std::get<parser::Literal>(*next).value;
            }
        }

        // Call specific parser builder based on parser info type
        // Field
        if (std::holds_alternative<parser::Field>(*parserInfo))
        {
            parsers.push_back(
                buildFieldParser(std::get<parser::Field>(*parserInfo), endToken));
        }
        // Literal
        else if (std::holds_alternative<parser::Literal>(*parserInfo))
        {
            parsers.push_back(buildLiteralParser(std::get<parser::Literal>(*parserInfo)));
        }
        // Choice
        else if (std::holds_alternative<parser::Choice>(*parserInfo))
        {
            parsers.push_back(
                buildChoiceParser(std::get<parser::Choice>(*parserInfo), endToken));
        }
        // Group
        else if (std::holds_alternative<parser::Group>(*parserInfo))
        {
            // Recursively calls buildParsers
            parsers.push_back(buildGroupOptParser(std::get<parser::Group>(*parserInfo)));
        }
        else
        {
            throw std::runtime_error("Unknown parser info type");
        }
    }

    // return parsers;
    auto p = parsers.front();
    parsers.pop_front();
    for (const auto& parser : parsers)
    {
        p = parsec::fmap<json::Json, std::tuple<json::Json, json::Json>>(
            [](auto t) -> json::Json
            {
                auto& [a, b] = t;
                if (a.isObject() && b.isObject())
                {
                    a.merge(b);
                    return a;
                }
                else if (a.isObject())
                {
                    return a;
                }
                else if (b.isObject())
                {
                    return b;
                }
                else
                {
                    return json::Json();
                }
            },
            p& parser);
    }

    return p << parser::pEof<json::Json>();
}

void Logpar::registerBuilder(ParserType type, ParserBuilder builder)
{
    if (m_parserBuilders.count(type) != 0)
    {
        throw std::runtime_error(
            fmt::format("Parser type '{}' already registered", parserTypeToStr(type)));
    }

    m_parserBuilders[type] = builder;
}

parsec::Parser<json::Json> Logpar::build(std::string_view logpar) const
{
    auto result = parser::pLogpar()(logpar, 0);
    if (result.failure())
    {
        throw std::runtime_error(result.error().msg);
    }

    auto parserInfos = result.value();
    auto p = buildParsers(parserInfos);
    return p;
}

} // namespace hlp::logpar
