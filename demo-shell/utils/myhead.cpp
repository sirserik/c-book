// myhead — печатает первые N строк (по умолчанию 10). Аналог Unix-утилиты head.
// Опции:
//   -n N  — сколько строк печатать
// Если файлы не заданы — читает stdin. Важное свойство: как только напечатано
// N строк, программа завершается и НЕ дочитывает источник до конца.

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <istream>
#include <string>
#include <vector>

namespace {

// Печатает не больше limit строк. Возвращает, сколько напечатала.
long long head_stream(std::istream& in, long long limit) {
    std::string line;
    long long printed = 0;
    while (printed < limit && std::getline(in, line)) {
        std::cout << line << "\n";
        ++printed;
    }
    return printed;
}

}  // namespace

int main(int argc, char* argv[]) {
    long long limit = 10;
    std::vector<std::string> files;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-n") {
            if (i + 1 >= argc) {
                std::cerr << "myhead: у -n нет значения\n";
                return 2;
            }
            char* end = nullptr;
            long long v = std::strtoll(argv[++i], &end, 10);
            if (!end || *end != '\0' || v < 0) {
                std::cerr << "myhead: плохое число строк: " << argv[i] << "\n";
                return 2;
            }
            limit = v;
        } else if (a.size() > 2 && a.compare(0, 2, "-n") == 0) {
            // Слитная форма: -n5
            char* end = nullptr;
            long long v = std::strtoll(a.c_str() + 2, &end, 10);
            if (!end || *end != '\0' || v < 0) {
                std::cerr << "myhead: плохое число строк: " << a << "\n";
                return 2;
            }
            limit = v;
        } else {
            files.push_back(a);
        }
    }

    if (files.empty()) {
        head_stream(std::cin, limit);
        return 0;
    }

    int rc = 0;
    bool many = files.size() > 1;
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::ifstream in(files[i]);
        if (!in.is_open()) {
            std::cerr << "myhead: " << files[i] << ": " << std::strerror(errno) << "\n";
            rc = 1;
            continue;
        }
        if (many) {
            if (i > 0) std::cout << "\n";
            std::cout << "==> " << files[i] << " <==\n";
        }
        head_stream(in, limit);
    }
    return rc;
}
