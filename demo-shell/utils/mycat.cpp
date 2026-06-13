// mycat — простая копия Unix-утилиты cat.
// Читает файлы (или stdin), пишет в stdout.
// Поддержка: один или несколько файлов, "-" как stdin.

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace {

// Скопировать всё из fd в stdout. Возвращает 0 при успехе, 1 при ошибке.
int cat_fd(int fd, const std::string& name) {
    char buf[4096];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0) return 0;            // EOF
        if (n < 0) {
            if (errno == EINTR) continue;  // прервало сигналом — попробуем снова
            std::cerr << "mycat: " << name << ": " << std::strerror(errno) << "\n";
            return 1;
        }
        // Записываем всё, что прочитали (с учётом short writes).
        ssize_t written = 0;
        while (written < n) {
            ssize_t m = write(STDOUT_FILENO, buf + written, n - written);
            if (m < 0) {
                if (errno == EINTR) continue;
                std::cerr << "mycat: write: " << std::strerror(errno) << "\n";
                return 1;
            }
            written += m;
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return cat_fd(STDIN_FILENO, "<stdin>");
    }

    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        std::string name = argv[i];
        if (name == "-") {
            if (cat_fd(STDIN_FILENO, "<stdin>") != 0) rc = 1;
            continue;
        }
        int fd = open(name.c_str(), O_RDONLY);
        if (fd < 0) {
            std::cerr << "mycat: " << name << ": " << std::strerror(errno) << "\n";
            rc = 1;
            continue;
        }
        if (cat_fd(fd, name) != 0) rc = 1;
        close(fd);
    }
    return rc;
}
