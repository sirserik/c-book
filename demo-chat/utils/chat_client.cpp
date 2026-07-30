// chat_client — клиент к rooms_chat_server: понимает бинарный протокол
// из главы 44 и работает в двух потоках (один читает клавиатуру, другой сокет).
//
// Использование:
//   ./build/chat_client [host] [port] [ник]
//
// Команды:
//   /join <комната>   войти в комнату
//   /leave            выйти из комнаты
//   /quit             выход
//   любой другой текст — сообщение в текущую комнату

#include "protocol.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::mutex g_out_mutex;          // вывод из двух потоков нужно защищать
std::atomic<bool> g_running(true);

void say(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_out_mutex);
    std::cout << line << "\n";
    std::cout.flush();
}

bool send_all(int fd, const std::vector<std::uint8_t>& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t w = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<std::size_t>(w);
    }
    return true;
}

bool send_frame(int fd, std::uint8_t type, const std::string& payload) {
    return send_all(fd, chat::encode(type, payload));
}

// Поток приёма: читает сокет, разбирает кадры, печатает.
void receiver(int fd) {
    std::vector<std::uint8_t> acc;
    char buf[1024];

    while (g_running) {
        ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
        if (r == 0) { say("[соединение закрыто сервером]"); break; }
        if (r < 0) {
            if (errno == EINTR) continue;
            say(std::string("[recv: ") + std::strerror(errno) + "]");
            break;
        }
        acc.insert(acc.end(), reinterpret_cast<std::uint8_t*>(buf),
                   reinterpret_cast<std::uint8_t*>(buf) + r);

        try {
            std::uint8_t type = 0;
            std::string payload;
            while (chat::try_decode(acc, type, payload)) {
                if (type == chat::kMessage)      say(payload);
                else if (type == chat::kNotify)  say("* " + payload);
                else                             say("[тип " + std::to_string(type) +
                                                     "] " + payload);
            }
        } catch (const std::exception& e) {
            say(std::string("[ошибка протокола: ") + e.what() + "]");
            break;
        }
    }
    g_running = false;
}

int connect_to(const char* host, const char* port) {
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        std::cerr << "getaddrinfo: " << ::gai_strerror(rc) << "\n";
        return -1;
    }

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    return fd;
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char* port = (argc > 2) ? argv[2] : "9001";
    std::string nick = (argc > 3) ? argv[3] : "guest";

    int fd = connect_to(host, port);
    if (fd < 0) {
        std::cerr << "не удалось подключиться к " << host << ":" << port << "\n";
        return 1;
    }

    say("Подключён как " + nick + ". /join <комната>, /leave, /quit.");
    if (!send_frame(fd, chat::kHello, nick)) {
        std::cerr << "не удалось отправить Hello\n";
        ::close(fd);
        return 1;
    }

    std::thread rx(receiver, fd);

    std::string line;
    while (g_running && std::getline(std::cin, line)) {
        if (line.empty()) continue;

        bool ok = true;
        if (line == "/quit") {
            break;
        } else if (line.compare(0, 6, "/join ") == 0) {
            ok = send_frame(fd, chat::kJoin, line.substr(6));
        } else if (line == "/leave") {
            ok = send_frame(fd, chat::kLeave, "");
        } else {
            ok = send_frame(fd, chat::kMessage, line);
        }
        if (!ok) { say("[не удалось отправить]"); break; }
    }

    g_running = false;
    ::shutdown(fd, SHUT_RDWR);   // разбудит receiver, зависший в recv
    rx.join();
    ::close(fd);
    return 0;
}
