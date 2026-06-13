// Минимальный TCP-клиент к echo-серверу.
//
// Использование:
//   ./build/echo_client [host] [port]
//
// Клиент: подключается, в цикле читает stdin, шлёт серверу, читает ответ,
// печатает. На пустой строке завершает.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netdb.h>      // getaddrinfo
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char* port = (argc > 2) ? argv[2] : "9001";

    // getaddrinfo резолвит host:port в список sockaddr (IPv4/IPv6).
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // и IPv4, и IPv6
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        std::cerr << "getaddrinfo: " << ::gai_strerror(rc) << "\n";
        return 1;
    }

    // Пробуем все возвращённые адреса до первого успешного connect.
    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        std::cerr << "не удалось подключиться к " << host << ":" << port << "\n";
        return 1;
    }

    std::cout << "Подключён к " << host << ":" << port
              << ". Пустая строка — выход.\n\n";

    std::string line;
    while (true) {
        std::cout << "> ";
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) break;
        line += '\n';

        // send
        ssize_t sent = 0;
        while (sent < static_cast<ssize_t>(line.size())) {
            ssize_t s = ::send(fd, line.data() + sent, line.size() - sent, 0);
            if (s < 0) {
                if (errno == EINTR) continue;
                std::cerr << "send: " << std::strerror(errno) << "\n";
                ::close(fd);
                return 1;
            }
            sent += s;
        }

        // recv
        char buf[1024];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            std::cerr << "сервер закрыл соединение\n";
            break;
        }
        std::cout << "<- " << std::string(buf, n);
    }

    ::close(fd);
    return 0;
}
