// mygrep — поиск строк по регулярному выражению. Аналог Unix-утилиты grep.
// Использует std::regex (ECMAScript-синтаксис по умолчанию — близко к POSIX
// extended). Опции:
//   -i — игнорировать регистр
//   -n — печатать номер строки
//   -v — инвертировать (показывать НЕсовпадающие)
//
// Если файлы не заданы — читает stdin.

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <istream>
#include <regex>
#include <string>
#include <vector>

namespace {

struct Options {
    bool ignore_case = false;
    bool show_line_numbers = false;
    bool invert = false;
    std::string pattern;
    std::vector<std::string> files;
};

bool parse_args(int argc, char* argv[], Options& opts) {
    int i = 1;
    while (i < argc) {
        std::string a = argv[i];
        if (a == "-i") { opts.ignore_case = true; ++i; }
        else if (a == "-n") { opts.show_line_numbers = true; ++i; }
        else if (a == "-v") { opts.invert = true; ++i; }
        else if (a == "--") { ++i; break; }
        else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "mygrep: неизвестная опция: " << a << "\n";
            return false;
        }
        else break;
    }
    if (i >= argc) {
        std::cerr << "mygrep: нужен паттерн\n";
        return false;
    }
    opts.pattern = argv[i++];
    while (i < argc) opts.files.push_back(argv[i++]);
    return true;
}

int grep_stream(std::istream& in, const std::regex& re,
                const Options& opts, const std::string& filename) {
    std::string line;
    long long line_no = 0;
    bool multi_file = opts.files.size() > 1;
    while (std::getline(in, line)) {
        ++line_no;
        bool matched = std::regex_search(line, re);
        if (matched == opts.invert) continue;

        if (multi_file && !filename.empty()) std::cout << filename << ":";
        if (opts.show_line_numbers)          std::cout << line_no << ":";
        std::cout << line << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options opts;
    if (!parse_args(argc, argv, opts)) return 2;

    auto flags = std::regex::ECMAScript;
    if (opts.ignore_case) flags |= std::regex::icase;

    std::regex re;
    try {
        re = std::regex(opts.pattern, flags);
    } catch (const std::regex_error& e) {
        std::cerr << "mygrep: bad regex '" << opts.pattern << "': " << e.what() << "\n";
        return 2;
    }

    if (opts.files.empty()) {
        return grep_stream(std::cin, re, opts, "");
    }

    int rc = 0;
    for (const auto& f : opts.files) {
        if (f == "-") {
            grep_stream(std::cin, re, opts, "<stdin>");
            continue;
        }
        std::ifstream in(f);
        if (!in.is_open()) {
            std::cerr << "mygrep: " << f << ": " << std::strerror(errno) << "\n";
            rc = 2;
            continue;
        }
        grep_stream(in, re, opts, f);
    }
    return rc;
}
