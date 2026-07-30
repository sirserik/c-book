// Многопоточный broadcast-сервер чата. Глава 41.
//
// На каждого клиента — отдельный std::thread (detached). Сообщения от одного
// клиента рассылаются всем остальным. Список клиентов защищён std::mutex.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct Client {
    int fd;
    std::string name;
};

std::mutex g_mutex;
std::vector<Client> g_clients;

// Счётчик имён. Его увеличивают потоки-обработчики, значит, обычный int
// здесь — гонка данных: два клиента, подключившиеся одновременно, могли бы
// получить одно имя. atomic делает инкремент неделимым (глава 42).
std::atomic<int> g_next_id(1);

void broadcast(int from_fd, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& c : g_clients) {
        if (c.fd == from_fd) continue;
        // MSG_NOSIGNAL — не получать SIGPIPE, если клиент уже отключился.
        ::send(c.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
    }
}

void register_client(int fd, const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_clients.push_back({fd, name});
}

void unregister_client(int fd) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_clients.erase(
        std::remove_if(g_clients.begin(), g_clients.end(),
                       [fd](const Client& c) { return c.fd == fd; }),
        g_clients.end());
}

void handle_client(int fd) {
    std::string name = "user_" + std::to_string(g_next_id.fetch_add(1));
    register_client(fd, name);
    std::cout << "[server] " << name << " присоединился\n";

    std::string greet = "* " + name + " вошёл в чат\n";
    broadcast(fd, greet);

    // Приветствие самому клиенту.
    std::string hello = "Привет, " + name + ". Пишите сообщения. /quit для выхода.\n";
    ::send(fd, hello.data(), hello.size(), MSG_NOSIGNAL);

    char buf[1024];
    std::string acc;
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; break; }
        acc.append(buf, static_cast<std::size_t>(n));

        // Разбираем сообщения по '\n'.
        std::size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);

            if (line == "/quit") {
                goto disconnect;
            }
            std::string out = "<" + name + "> " + line + "\n";
            std::cout << "[server] " << out;
            broadcast(fd, out);
        }
    }

disconnect:
    unregister_client(fd);
    ::close(fd);
    std::string bye = "* " + name + " вышел из чата\n";
    broadcast(/*from_fd=*/-1, bye);   // не исключаем никого
    std::cout << "[server] " << name << " отключился\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    int port = (argc > 1) ? std::atoi(argv[1]) : 9001;

    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind: " << std::strerror(errno) << "\n";
        return 1;
    }
    if (::listen(srv, 16) < 0) {
        std::cerr << "listen: " << std::strerror(errno) << "\n";
        return 1;
    }

    std::cout << "Chat-server слушает на :" << port << "\n";

    while (true) {
        sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int c = ::accept(srv, reinterpret_cast<sockaddr*>(&cli), &cli_len);
        if (c < 0) { if (errno == EINTR) continue; break; }

        // Поток-обработчик. detach — fire-and-forget, поток сам уберётся.
        std::thread(handle_client, c).detach();
    }
    ::close(srv);
    return 0;
}
