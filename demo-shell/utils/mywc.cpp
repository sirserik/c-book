// mywc — считает строки, слова и байты. Аналог Unix-утилиты wc.
// Если файлы не заданы — читает stdin. Если задано больше одного — выводит
// итог по каждому файлу плюс итог в конце.
//
// Опция -c считает символы UTF-8 вместо байтов. В UTF-8 ведущий байт
// многобайтного символа имеет верхние биты 11..., продолжение — 10....
// Считаем те байты, у которых старшие 2 бита НЕ 10 — это и есть «первые»
// байты символов.

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <istream>
#include <string>
#include <vector>

namespace {

struct Counts {
    long long lines = 0;
    long long words = 0;
    long long bytes = 0;
    long long chars = 0;   // UTF-8 «первые байты»
};

Counts count_stream(std::istream& in) {
    Counts c;
    char ch;
    bool in_word = false;
    while (in.get(ch)) {
        ++c.bytes;

        // UTF-8: считаем «не-продолжающие» байты. (b & 0xC0) != 0x80
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) {
            ++c.chars;
        }

        if (ch == '\n') ++c.lines;

        bool ws = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v');
        if (ws) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++c.words;
        }
    }
    return c;
}

void print_counts(const Counts& c, const std::string& name, bool show_chars) {
    std::cout << std::setw(7) << c.lines
              << " " << std::setw(7) << c.words;
    if (show_chars) std::cout << " " << std::setw(7) << c.chars;
    else            std::cout << " " << std::setw(7) << c.bytes;
    if (!name.empty()) std::cout << " " << name;
    std::cout << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    bool show_chars = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-c") show_chars = true;
        else files.push_back(a);
    }

    if (files.empty()) {
        Counts c = count_stream(std::cin);
        print_counts(c, "", show_chars);
        return 0;
    }

    Counts total;
    int rc = 0;
    for (const auto& f : files) {
        std::ifstream in(f);
        if (!in.is_open()) {
            std::cerr << "mywc: " << f << ": " << std::strerror(errno) << "\n";
            rc = 1;
            continue;
        }
        Counts c = count_stream(in);
        print_counts(c, f, show_chars);
        total.lines += c.lines;
        total.words += c.words;
        total.bytes += c.bytes;
        total.chars += c.chars;
    }
    if (files.size() > 1) {
        print_counts(total, "total", show_chars);
    }
    return rc;
}
