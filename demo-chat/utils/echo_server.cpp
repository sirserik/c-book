// Минимальный TCP echo-сервер на одного клиента.
// Глава 40: показываем socket/bind/listen/accept/recv/send/close.
//
// Использование:
//   ./build/echo_server [port]
//   port по умолчанию 9001
//
// Сервер обрабатывает ПО ОДНОМУ клиенту, отвечает ему такой же строкой,
// дальше принимает следующего. Многопоточность — следующие главы.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <arpa/inet.h>     // inet_ntop
#include <netinet/in.h>    // sockaddr_in
#include <sys/socket.h>
#include <unistd.h>        // close

int main(int argc, char* argv[]) {
    int port = (argc > 1) ? std::atoi(argv[1]) : 9001;

    // 1) socket(): создаём IPv4 TCP-сокет.
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        std::cerr << "socket: " << std::strerror(errno) << "\n";
        return 1;
    }

    // 2) SO_REUSEADDR — позволяет перезапустить сервер без TIME_WAIT-задержки.
    int yes = 1;
    if (::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        std::cerr << "setsockopt: " << std::strerror(errno) << "\n";
        ::close(srv);
        return 1;
    }

    // 3) bind(): привязываем к 0.0.0.0:port.
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind: " << std::strerror(errno) << "\n";
        ::close(srv);
        return 1;
    }

    // 4) listen(): начинаем слушать. Backlog 16 — длина очереди ожидающих.
    if (::listen(srv, 16) < 0) {
        std::cerr << "listen: " << std::strerror(errno) << "\n";
        ::close(srv);
        return 1;
    }

    std::cout << "Echo-server слушает на 0.0.0.0:" << port << "\n";
    std::cout << "Подключиться: telnet localhost " << port
              << "  или ./build/echo_client localhost " << port << "\n\n";

    while (true) {
        // 5) accept(): блокируем до прихода клиента.
        sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int c = ::accept(srv, reinterpret_cast<sockaddr*>(&cli), &cli_len);
        if (c < 0) {
            if (errno == EINTR) continue;
            std::cerr << "accept: " << std::strerror(errno) << "\n";
            break;
        }

        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        std::cout << "Клиент " << ip << ":" << ntohs(cli.sin_port)
                  << " подключён\n";

        // 6) Цикл recv/send для одного клиента до закрытия.
        char buf[1024];
        while (true) {
            ssize_t n = ::recv(c, buf, sizeof(buf), 0);
            if (n == 0) { std::cout << "клиент отключился\n"; break; }
            if (n < 0) {
                if (errno == EINTR) continue;
                std::cerr << "recv: " << std::strerror(errno) << "\n";
                break;
            }
            // 7) send: эхо обратно.
            ssize_t sent = 0;
            while (sent < n) {
                ssize_t s = ::send(c, buf + sent, n - sent, 0);
                if (s < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                sent += s;
            }
        }

        ::close(c);
    }

    ::close(srv);
    return 0;
}
