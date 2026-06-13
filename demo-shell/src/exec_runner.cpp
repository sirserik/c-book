#include "exec_runner.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace shell {

namespace {

// Распознать на позиции i спец-токен. Возвращает длину поглощённых символов
// и сам токен. 0 — не спец.
std::size_t match_special(const std::string& s, std::size_t i, std::string& out) {
    if (i >= s.size()) return 0;
    char c = s[i];

    // 2>&1 / 2>> / 2> — только если это начало слова. Проверка делается на
    // вызывающей стороне (current.empty()).
    if (c == '|') { out = "|"; return 1; }
    if (c == '<') { out = "<"; return 1; }
    if (c == '>') {
        if (i + 1 < s.size() && s[i + 1] == '>') { out = ">>"; return 2; }
        out = ">"; return 1;
    }
    return 0;
}

}  // namespace

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_dq = false;   // double quote
    bool in_sq = false;   // single quote

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    };

    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (in_sq) {
            if (c == '\'') in_sq = false;
            else current += c;
            continue;
        }

        if (in_dq) {
            if (c == '"') {
                in_dq = false;
            } else if (c == '\\' && i + 1 < line.size() &&
                       (line[i + 1] == '"' || line[i + 1] == '\\' ||
                        line[i + 1] == '$' || line[i + 1] == '`')) {
                current += line[++i];
            } else {
                current += c;
            }
            continue;
        }

        // Вне кавычек.
        if (c == '\'') { in_sq = true; continue; }
        if (c == '"')  { in_dq = true; continue; }

        if (c == '\\' && i + 1 < line.size()) {
            current += line[++i];
            continue;
        }

        if (c == ' ' || c == '\t') {
            flush();
            continue;
        }

        // 2>, 2>>, 2>&1 — только в начале слова.
        if (current.empty() && c == '2' && i + 1 < line.size() && line[i + 1] == '>') {
            if (i + 3 < line.size() &&
                line[i + 2] == '&' && line[i + 3] == '1') {
                tokens.push_back("2>&1");
                i += 3;
                continue;
            }
            if (i + 2 < line.size() && line[i + 2] == '>') {
                tokens.push_back("2>>");
                i += 2;
                continue;
            }
            tokens.push_back("2>");
            i += 1;
            continue;
        }

        std::string spec;
        std::size_t consumed = match_special(line, i, spec);
        if (consumed > 0) {
            flush();
            tokens.push_back(spec);
            i += consumed - 1;
            continue;
        }

        current += c;
    }

    if (in_sq || in_dq) {
        throw std::runtime_error("незакрытая кавычка");
    }

    flush();
    return tokens;
}

}  // namespace shell
