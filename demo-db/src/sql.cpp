#include "sql.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace db {

namespace {

// === Tokenizer ===

struct Token {
    enum Type {
        kIdent,    // имя таблицы/колонки
        kKeyword,  // CREATE, TABLE, INSERT, INTO, ...
        kNumber,
        kStar,     // *
        kComma,
        kLParen,
        kRParen,
        kOp,       // =, <, >, <=, >=, !=
        kSemi,
        kEnd
    };
    Type type;
    std::string text;
};

std::string to_upper(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

bool is_keyword(const std::string& u) {
    static const std::vector<std::string> kw = {
        "CREATE", "TABLE", "INT", "PRIMARY", "KEY",
        "INDEX", "ON",
        "INSERT", "INTO", "VALUES",
        "SELECT", "FROM", "WHERE",
        "EXPLAIN",
    };
    for (const auto& k : kw) if (u == k) return true;
    return false;
}

std::vector<Token> tokenize(const std::string& s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        if (c == '(') { out.push_back({Token::kLParen, "("}); ++i; continue; }
        if (c == ')') { out.push_back({Token::kRParen, ")"}); ++i; continue; }
        if (c == ',') { out.push_back({Token::kComma,  ","}); ++i; continue; }
        if (c == ';') { out.push_back({Token::kSemi,   ";"}); ++i; continue; }
        if (c == '*') { out.push_back({Token::kStar,   "*"}); ++i; continue; }

        if (c == '=' || c == '<' || c == '>' || c == '!') {
            std::string op(1, c);
            if (i + 1 < s.size() && s[i + 1] == '=') { op += '='; i += 2; }
            else { ++i; }
            if (op != "=" && op != "<" && op != ">" && op != "<=" && op != ">=" && op != "!=") {
                throw SqlError("плохой оператор: " + op);
            }
            out.push_back({Token::kOp, op});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            std::size_t j = i;
            if (c == '-') ++j;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
            out.push_back({Token::kNumber, s.substr(i, j - i)});
            i = j;
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            std::string text = s.substr(i, j - i);
            std::string upper = to_upper(text);
            if (is_keyword(upper)) {
                out.push_back({Token::kKeyword, upper});
            } else {
                out.push_back({Token::kIdent, text});
            }
            i = j;
            continue;
        }

        throw SqlError(std::string("неизвестный символ: '") + c + "'");
    }
    out.push_back({Token::kEnd, ""});
    return out;
}

// === Parser (recursive descent) ===

struct Parser {
    const std::vector<Token>& tokens;
    std::size_t pos = 0;

    explicit Parser(const std::vector<Token>& t) : tokens(t) {}

    const Token& peek() const { return tokens[pos]; }

    Token consume() { return tokens[pos++]; }

    Token expect(Token::Type t) {
        if (peek().type != t) {
            throw SqlError("ожидался токен, получено '" + peek().text + "'");
        }
        return consume();
    }

    Token expect_keyword(const std::string& kw) {
        if (peek().type != Token::kKeyword || peek().text != kw) {
            throw SqlError("ожидалось '" + kw + "', получено '" + peek().text + "'");
        }
        return consume();
    }

    std::int64_t parse_number() {
        Token t = expect(Token::kNumber);
        return static_cast<std::int64_t>(std::strtoll(t.text.c_str(), nullptr, 10));
    }

    Statement parse() {
        if (peek().type != Token::kKeyword) {
            throw SqlError("ожидался keyword");
        }
        if (peek().text == "EXPLAIN") {
            consume();
            Statement st = parse_select();
            st.is_explain = true;
            return st;
        }
        const std::string& kw = peek().text;
        if (kw == "CREATE") return parse_create();
        if (kw == "INSERT") return parse_insert();
        if (kw == "SELECT") return parse_select();
        throw SqlError("неизвестный statement: " + kw);
    }

    Statement parse_create() {
        Statement st;
        expect_keyword("CREATE");
        if (peek().type == Token::kKeyword && peek().text == "INDEX") {
            consume();
            st.kind = Statement::kCreateIndex;
            st.index_name = expect(Token::kIdent).text;
            expect_keyword("ON");
            st.table = expect(Token::kIdent).text;
            expect(Token::kLParen);
            std::string col = expect(Token::kIdent).text;
            st.columns.push_back({col, "INT"});
            expect(Token::kRParen);
            if (peek().type == Token::kSemi) consume();
            return st;
        }
        st.kind = Statement::kCreate;
        expect_keyword("TABLE");
        st.table = expect(Token::kIdent).text;
        expect(Token::kLParen);
        while (true) {
            std::string col = expect(Token::kIdent).text;
            expect_keyword("INT");
            if (peek().type == Token::kKeyword && peek().text == "PRIMARY") {
                consume();
                expect_keyword("KEY");
            }
            st.columns.push_back({col, "INT"});
            if (peek().type == Token::kComma) { consume(); continue; }
            break;
        }
        expect(Token::kRParen);
        if (peek().type == Token::kSemi) consume();
        return st;
    }

    Statement parse_insert() {
        Statement st;
        st.kind = Statement::kInsert;
        expect_keyword("INSERT");
        expect_keyword("INTO");
        st.table = expect(Token::kIdent).text;
        expect_keyword("VALUES");
        expect(Token::kLParen);
        while (true) {
            st.values.push_back(parse_number());
            if (peek().type == Token::kComma) { consume(); continue; }
            break;
        }
        expect(Token::kRParen);
        if (peek().type == Token::kSemi) consume();
        return st;
    }

    Statement parse_select() {
        Statement st;
        st.kind = Statement::kSelect;
        expect_keyword("SELECT");
        if (peek().type == Token::kStar) {
            consume();
            st.select_columns.push_back("*");
        } else {
            while (true) {
                st.select_columns.push_back(expect(Token::kIdent).text);
                if (peek().type == Token::kComma) { consume(); continue; }
                break;
            }
        }
        expect_keyword("FROM");
        st.table = expect(Token::kIdent).text;
        if (peek().type == Token::kKeyword && peek().text == "WHERE") {
            consume();
            st.has_where = true;
            st.where_col = expect(Token::kIdent).text;
            st.where_op = expect(Token::kOp).text;
            st.where_val = parse_number();
        }
        if (peek().type == Token::kSemi) consume();
        return st;
    }
};

}  // namespace

Statement parse_sql(const std::string& sql) {
    auto tokens = tokenize(sql);
    Parser p(tokens);
    Statement st = p.parse();
    if (p.peek().type != Token::kEnd) {
        throw SqlError("лишние токены после statement: '" + p.peek().text + "'");
    }
    return st;
}

}  // namespace db
