#ifndef DB_PAGE_MANAGER_H
#define DB_PAGE_MANAGER_H

#include "page.h"

#include <list>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace db {

// Ошибка PageManager.
class PageError : public std::runtime_error {
public:
    explicit PageError(const std::string& m) : std::runtime_error(m) {}
};

// Менеджер страничного файла с LRU-кэшем.
//
// Использование:
//   PageManager pm("data.db", 64);
//   PageId pid = pm.allocate_page();
//   Page p = make_page();
//   p[0] = 'A';
//   pm.write_page(pid, p);
//   pm.sync();
//
//   Page q = pm.read_page(pid);
//   // q[0] == 'A'
class PageManager {
public:
    // Открыть файл (создаст, если не существует). cache_size — сколько страниц
    // держать в памяти.
    PageManager(const std::string& path, std::size_t cache_size = 64);
    ~PageManager();

    // Запрет копирования.
    PageManager(const PageManager&) = delete;
    PageManager& operator=(const PageManager&) = delete;

    // Прочитать страницу. Бросает PageError, если pid >= page_count().
    Page read_page(PageId pid);

    // Записать страницу. Запись в кэш и пометка dirty; реальный диск — при evict
    // или sync().
    void write_page(PageId pid, const Page& page);

    // Выделить новую страницу в конце файла. Возвращает её id.
    PageId allocate_page();

    // Сколько страниц в файле.
    PageId page_count() const { return page_count_; }

    // Записать все dirty-страницы на диск и fsync.
    void sync();

private:
    struct Entry {
        Page page;
        bool dirty = false;
    };

    using LruList = std::list<PageId>;
    using LruIter = LruList::iterator;

    int fd_;
    PageId page_count_;
    std::size_t cache_size_;

    LruList lru_;   // front = самая свежая
    std::unordered_map<PageId, std::pair<Entry, LruIter>> cache_;

    void evict_one_if_full();
    void flush_entry(PageId pid, Entry& e);
    Entry& load_into_cache(PageId pid);
    void touch(PageId pid);  // переместить в начало LRU
};

}  // namespace db

#endif
