// Многокомнатный чат-сервер. Глава 45.
// Использует:
//   - реактор на poll (глава 43)
//   - бинарный протокол framing (глава 44)
//   - комнаты как unordered_map<string, set<int>>
//
// Клиент подключается, шлёт Hello с ником, потом Join "имя_комнаты",
// потом Message'ы — они broadcast'ятся только участникам этой комнаты.

#include "protocol.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    std::string name;       // пусто, пока не прислал Hello
    std::string room;       // пусто = не в комнате
    std::vector<std::uint8_t> recv_buf;
    std::vector<std::uint8_t> send_buf;
};

void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void enqueue_frame(ClientState& c, std::uint8_t type, const std::string& payload) {
    auto frame = chat::encode(type, payload);
    c.send_buf.insert(c.send_buf.end(), frame.begin(), frame.end());
}

void broadcast_room(std::unordered_map<int, ClientState>& clients,
                    std::unordered_map<std::string, std::unordered_set<int>>& rooms,
                    const std::string& room, int from_fd,
                    std::uint8_t type, const std::string& payload) {
    auto it = rooms.find(room);
    if (it == rooms.end()) return;
    for (int fd : it->second) {
        if (fd == from_fd) continue;
        auto ci = clients.find(fd);
        if (ci == clients.end()) continue;
        enqueue_frame(ci->second, type, payload);
    }
}

void leave_room(std::unordered_map<int, ClientState>& clients,
                std::unordered_map<std::string, std::unordered_set<int>>& rooms,
                int fd) {
    auto ci = clients.find(fd);
    if (ci == clients.end() || ci->second.room.empty()) return;
    std::string room = ci->second.room;
    rooms[room].erase(fd);
    if (rooms[room].empty()) rooms.erase(room);
    broadcast_room(clients, rooms, room, fd, chat::kNotify,
                   ci->second.name + " вышел из " + room);
    ci->second.room.clear();
}

void process_message(std::unordered_map<int, ClientState>& clients,
                     std::unordered_map<std::string, std::unordered_set<int>>& rooms,
                     int fd, std::uint8_t type, const std::string& payload) {
    auto ci = clients.find(fd);
    if (ci == clients.end()) return;
    ClientState& c = ci->second;

    switch (type) {
        case chat::kHello: {
            c.name = payload;
            enqueue_frame(c, chat::kNotify,
                          "Привет, " + c.name + ". /join <room> для входа.");
            break;
        }
        case chat::kJoin: {
            if (c.name.empty()) {
                enqueue_frame(c, chat::kNotify, "сначала Hello");
                break;
            }
            leave_room(clients, rooms, fd);
            c.room = payload;
            rooms[c.room].insert(fd);
            enqueue_frame(c, chat::kNotify, "вы в комнате " + c.room);
            broadcast_room(clients, rooms, c.room, fd, chat::kNotify,
                           c.name + " вошёл в " + c.room);
            break;
        }
        case chat::kLeave: {
            leave_room(clients, rooms, fd);
            enqueue_frame(c, chat::kNotify, "вы вышли");
            break;
        }
        case chat::kMessage: {
            if (c.room.empty()) {
                enqueue_frame(c, chat::kNotify, "не в комнате");
                break;
            }
            std::string out = "<" + c.name + "> " + payload;
            std::cout << "[" << c.room << "] " << out << "\n";
            broadcast_room(clients, rooms, c.room, fd, chat::kMessage, out);
            break;
        }
        default:
            enqueue_frame(c, chat::kNotify, "неизвестный тип");
            break;
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
    ::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(srv, 64);

    std::cout << "Rooms chat-server слушает на :" << port << "\n";

    std::unordered_map<int, ClientState> clients;
    std::unordered_map<std::string, std::unordered_set<int>> rooms;

    while (true) {
        std::vector<pollfd> pfds;
        pfds.push_back({srv, POLLIN, 0});
        for (const auto& kv : clients) {
            short ev = POLLIN;
            if (!kv.second.send_buf.empty()) ev |= POLLOUT;
            pfds.push_back({kv.first, ev, 0});
        }

        int n = ::poll(pfds.data(), pfds.size(), -1);
        if (n < 0) { if (errno == EINTR) continue; break; }

        if (pfds[0].revents & POLLIN) {
            while (true) {
                int c = ::accept(srv, nullptr, nullptr);
                if (c < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (errno == EINTR) continue;
                    break;
                }
                set_nonblocking(c);
                clients[c] = ClientState{c, "", "", {}, {}};
                std::cout << "[server] подключён fd=" << c << "\n";
            }
        }

        for (std::size_t i = 1; i < pfds.size(); ++i) {
            int fd = pfds[i].fd;
            short re = pfds[i].revents;
            auto it = clients.find(fd);
            if (it == clients.end()) continue;
            ClientState& s = it->second;

            bool drop = false;

            if (re & POLLIN) {
                char buf[1024];
                while (true) {
                    ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
                    if (r == 0) { drop = true; break; }
                    if (r < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        drop = true; break;
                    }
                    s.recv_buf.insert(s.recv_buf.end(),
                                       reinterpret_cast<std::uint8_t*>(buf),
                                       reinterpret_cast<std::uint8_t*>(buf) + r);
                }
                try {
                    std::uint8_t type;
                    std::string payload;
                    while (chat::try_decode(s.recv_buf, type, payload)) {
                        process_message(clients, rooms, fd, type, payload);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[server] proto error fd=" << fd << ": "
                              << e.what() << "\n";
                    drop = true;
                }
            }

            if ((re & POLLOUT) && !s.send_buf.empty()) {
                while (!s.send_buf.empty()) {
                    ssize_t w = ::send(fd, s.send_buf.data(), s.send_buf.size(), MSG_NOSIGNAL);
                    if (w < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        drop = true; break;
                    }
                    s.send_buf.erase(s.send_buf.begin(),
                                     s.send_buf.begin() + w);
                }
            }

            if ((re & (POLLHUP | POLLERR | POLLNVAL)) || drop) {
                std::cout << "[server] отключён fd=" << fd << "\n";
                leave_room(clients, rooms, fd);
                ::close(fd);
                clients.erase(it);
            }
        }
    }
    ::close(srv);
    return 0;
}
