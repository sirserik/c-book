// Демо персистентности чата: append + recent.

#include "history.h"

#include <cstdio>
#include <ctime>
#include <iostream>
#include <sys/stat.h>

int main() {
    std::remove("data/chat_history.bin");
    ::mkdir("data", 0755);

    {
        chat::History h("data/chat_history.bin");
        h.append({static_cast<std::uint32_t>(std::time(nullptr)),     "Alice",  "general", "первое сообщение"});
        h.append({static_cast<std::uint32_t>(std::time(nullptr)) + 1, "Bob",    "general", "ответ"});
        h.append({static_cast<std::uint32_t>(std::time(nullptr)) + 2, "Carol",  "tech",    "что-то про код"});
        h.append({static_cast<std::uint32_t>(std::time(nullptr)) + 3, "Alice",  "general", "ещё одно"});
    }

    // Открываем заново, читаем.
    chat::History h("data/chat_history.bin");

    std::cout << "=== Последние 10 в general ===\n";
    auto general = h.recent("general", 10);
    for (const auto& r : general) {
        std::cout << "[" << r.timestamp << "] [" << r.room << "] <"
                  << r.name << "> " << r.text << "\n";
    }

    std::cout << "\n=== Все записи (по всем комнатам) ===\n";
    auto all = h.recent("", 100);
    for (const auto& r : all) {
        std::cout << "[" << r.timestamp << "] [" << r.room << "] <"
                  << r.name << "> " << r.text << "\n";
    }
    return 0;
}
