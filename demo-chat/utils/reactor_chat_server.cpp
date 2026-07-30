// Чат-сервер на реакторе через poll(). Глава 43.
//
// Один поток обслуживает всех клиентов через non-blocking IO. Сравнительно
// с threaded_chat_server: один поток вместо N, нет мьютексов на shared state,
// масштабируется на тысячи соединений.
//
// poll() работает на Linux/macOS/*BSD. На Linux production — epoll, на BSD/
// macOS — kqueue. Их интерфейс другой, но идея та же.

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct ClientState {
    int fd;
    std::string name;
    std::string recv_buf;   // буфер для частичных recv
    std::string send_buf;   // что мы хотим послать (пока не отправлено)
};

void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void broadcast(std::unordered_map<int, ClientState>& clients,
               int from_fd, const std::string& msg) {
    for (auto& kv : clients) {
        if (kv.first == from_fd) continue;
        kv.second.send_buf += msg;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    int port = (argc > 1) ? std::atoi(argv[1]) : 9001;

    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    set_nonblocking(srv);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind: " << std::strerror(errno) << "\n";
        return 1;
    }
    if (::listen(srv, 64) < 0) {
        std::cerr << "listen: " << std::strerror(errno) << "\n";
        return 1;
    }

    std::cout << "Reactor chat-server слушает на :" << port << "\n";

    std::unordered_map<int, ClientState> clients;
    int next_id = 1;

    while (true) {
        // Собираем массив pollfd на эту итерацию.
        std::vector<pollfd> pfds;
        pfds.reserve(clients.size() + 1);
        pfds.push_back({srv, POLLIN, 0});
        for (const auto& kv : clients) {
            short events = POLLIN;
            if (!kv.second.send_buf.empty()) events |= POLLOUT;
            pfds.push_back({kv.first, events, 0});
        }

        int n = ::poll(pfds.data(), pfds.size(), -1 /*ms, infinite*/);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll: " << std::strerror(errno) << "\n";
            break;
        }

        // 1) listening socket — новый клиент
        if (pfds[0].revents & POLLIN) {
            while (true) {
                int c = ::accept(srv, nullptr, nullptr);
                if (c < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (errno == EINTR) continue;
                    break;
                }
                set_nonblocking(c);

                ClientState st;
                st.fd = c;
                st.name = "user_" + std::to_string(next_id++);
                clients.emplace(c, std::move(st));
                std::cout << "[server] " << clients[c].name << " подключён\n";
                broadcast(clients, c, "* " + clients[c].name + " вошёл\n");
                clients[c].send_buf += "Привет, " + clients[c].name + "\n";
            }
        }

        // 2) клиенты
        for (std::size_t i = 1; i < pfds.size(); ++i) {
            int fd = pfds[i].fd;
            short re = pfds[i].revents;
            auto it = clients.find(fd);
            if (it == clients.end()) continue;
            ClientState& s = it->second;

            // Можно читать
            if (re & POLLIN) {
                char buf[1024];
                bool gone = false;
                while (true) {
                    ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
                    if (r == 0) { gone = true; break; }
                    if (r < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        gone = true; break;
                    }
                    s.recv_buf.append(buf, static_cast<std::size_t>(r));
                }
                // Парсим сообщения по \n
                std::size_t pos;
                while ((pos = s.recv_buf.find('\n')) != std::string::npos) {
                    std::string line = s.recv_buf.substr(0, pos);
                    s.recv_buf.erase(0, pos + 1);
                    if (line == "/quit") { gone = true; break; }
                    std::string out = "<" + s.name + "> " + line + "\n";
                    std::cout << "[server] " << out;
                    broadcast(clients, fd, out);
                }
                if (gone) {
                    std::cout << "[server] " << s.name << " отключился\n";
                    std::string bye = "* " + s.name + " вышел\n";
                    ::close(fd);
                    clients.erase(it);
                    broadcast(clients, -1, bye);
                    continue;
                }
            }

            // Можно писать. Осторожно: если отправка сорвалась, клиента надо
            // удалить — и после этого ссылка s указывает в никуда, поэтому
            // выходим сразу, а не продолжаем работать с ней.
            if ((re & POLLOUT) && !s.send_buf.empty()) {
                bool dropped = false;
                while (!s.send_buf.empty()) {
                    ssize_t w = ::send(fd, s.send_buf.data(), s.send_buf.size(), MSG_NOSIGNAL);
                    if (w < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        std::cout << "[server] " << s.name << ": send: "
                                  << std::strerror(errno) << "\n";
                        ::close(fd);
                        clients.erase(it);
                        dropped = true;
                        break;
                    }
                    s.send_buf.erase(0, static_cast<std::size_t>(w));
                }
                if (dropped) continue;
            }

            // Хост-отключение/ошибка
            if (re & (POLLHUP | POLLERR | POLLNVAL)) {
                std::cout << "[server] " << s.name << " (hup/err)\n";
                ::close(fd);
                clients.erase(it);
            }
        }
    }

    ::close(srv);
    return 0;
}
