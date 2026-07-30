// page_demo — показывает работу страничного файла и LRU-кэша:
// выделение страниц, запись, переоткрытие файла, попадания и промахи кэша,
// вытеснение при маленьком кэше.
//
// Запуск: ./build/page_demo [файл]

#include "page_manager.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_stats(const char* title, const db::PageManager& pm) {
    const db::PageManager::Stats& s = pm.stats();
    std::cout << title << ": попаданий " << s.hits
              << ", промахов " << s.misses
              << ", вытеснено " << s.evictions
              << ", pread " << s.disk_reads
              << ", pwrite " << s.disk_writes << "\n";
}

void fill(db::Page& p, const std::string& text) {
    for (std::size_t i = 0; i < text.size() && i < p.size(); ++i) {
        p[i] = static_cast<unsigned char>(text[i]);
    }
}

std::string head_of(const db::Page& p, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n && i < p.size(); ++i) {
        unsigned char c = p[i];
        s += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
    }
    return s;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string path = (argc > 1) ? argv[1] : "data/page_demo.db";
    std::remove(path.c_str());

    std::cout << "=== Запись трёх страниц ===\n";
    {
        db::PageManager pm(path, /*cache_size=*/16);
        for (int i = 0; i < 3; ++i) {
            db::PageId pid = pm.allocate_page();
            db::Page p = db::make_page();
            fill(p, "page " + std::to_string(pid) + " payload");
            pm.write_page(pid, p);
        }
        std::cout << "page_count = " << pm.page_count()
                  << ", размер файла будет " << pm.page_count() * db::PAGE_SIZE
                  << " байт\n";
        pm.sync();
        print_stats("после записи", pm);
    }

    std::cout << "\n=== Переоткрытие файла ===\n";
    {
        db::PageManager pm(path, 16);
        std::cout << "page_count восстановлен из размера файла: "
                  << pm.page_count() << "\n";
        for (db::PageId i = 0; i < pm.page_count(); ++i) {
            db::Page p = pm.read_page(i);
            std::cout << "  страница " << i << ": " << head_of(p, 30) << "\n";
        }
        print_stats("три первых чтения", pm);

        // Повторное чтение тех же страниц — теперь всё в кэше.
        pm.reset_stats();
        for (int rep = 0; rep < 10; ++rep) {
            for (db::PageId i = 0; i < pm.page_count(); ++i) {
                db::Page p = pm.read_page(i);
                (void)p;
            }
        }
        print_stats("ещё 30 чтений", pm);
    }

    std::cout << "\n=== Кэш меньше рабочего набора ===\n";
    {
        db::PageManager pm(path, /*cache_size=*/4);
        for (int i = 0; i < 60; ++i) {
            db::PageId pid = pm.allocate_page();
            db::Page p = db::make_page();
            p[0] = static_cast<unsigned char>(pid);
            pm.write_page(pid, p);
        }
        pm.sync();
        pm.reset_stats();

        // Проход по кругу: 63 страницы через кэш на 4 — худший случай для LRU.
        for (int rep = 0; rep < 3; ++rep) {
            for (db::PageId i = 0; i < pm.page_count(); ++i) {
                db::Page p = pm.read_page(i);
                (void)p;
            }
        }
        print_stats("обход по кругу", pm);

        // Тот же кэш, но обращаемся к четырём горячим страницам.
        pm.reset_stats();
        for (int rep = 0; rep < 50; ++rep) {
            for (db::PageId i = 0; i < 4; ++i) {
                db::Page p = pm.read_page(i);
                (void)p;
            }
        }
        print_stats("горячие 4 страницы", pm);
    }

    std::remove(path.c_str());
    return 0;
}
