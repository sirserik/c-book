#include "page_manager.h"

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace db {

PageManager::PageManager(const std::string& path, std::size_t cache_size)
    : fd_(-1), page_count_(0), cache_size_(cache_size) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw PageError("не открыт: " + path + ": " + std::strerror(errno));
    }
    struct stat st;
    if (::fstat(fd_, &st) < 0) {
        ::close(fd_);
        throw PageError(std::string("fstat: ") + std::strerror(errno));
    }
    if (st.st_size % PAGE_SIZE != 0) {
        ::close(fd_);
        throw PageError("файл не выровнен по PAGE_SIZE");
    }
    page_count_ = static_cast<PageId>(st.st_size / PAGE_SIZE);
}

PageManager::~PageManager() {
    if (fd_ >= 0) {
        // На деструкторе пытаемся синхронизироваться. Исключения проглатываем —
        // в деструкторе их бросать нельзя.
        try { sync(); } catch (...) {}
        ::close(fd_);
    }
}

void PageManager::touch(PageId pid) {
    auto it = cache_.find(pid);
    if (it == cache_.end()) return;
    lru_.erase(it->second.second);
    lru_.push_front(pid);
    it->second.second = lru_.begin();
}

void PageManager::flush_entry(PageId pid, Entry& e) {
    if (!e.dirty) return;
    off_t off = static_cast<off_t>(pid) * static_cast<off_t>(PAGE_SIZE);
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(PAGE_SIZE)) {
        ssize_t n = ::pwrite(fd_,
                             e.page.data() + written,
                             PAGE_SIZE - written,
                             off + written);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw PageError(std::string("pwrite: ") + std::strerror(errno));
        }
        ++stats_.disk_writes;
        written += n;
    }
    e.dirty = false;
}

void PageManager::evict_one_if_full() {
    if (cache_.size() < cache_size_) return;
    // Жертва — самый старый элемент (хвост LRU).
    PageId victim = lru_.back();
    auto it = cache_.find(victim);
    flush_entry(victim, it->second.first);
    ++stats_.evictions;
    lru_.pop_back();
    cache_.erase(it);
}

PageManager::Entry& PageManager::load_into_cache(PageId pid) {
    auto it = cache_.find(pid);
    if (it != cache_.end()) {
        ++stats_.hits;
        touch(pid);
        return it->second.first;
    }
    ++stats_.misses;

    evict_one_if_full();

    Entry e;
    e.page = make_page();
    e.dirty = false;

    off_t off = static_cast<off_t>(pid) * static_cast<off_t>(PAGE_SIZE);
    ssize_t read_total = 0;
    while (read_total < static_cast<ssize_t>(PAGE_SIZE)) {
        ssize_t n = ::pread(fd_,
                            e.page.data() + read_total,
                            PAGE_SIZE - read_total,
                            off + read_total);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw PageError(std::string("pread: ") + std::strerror(errno));
        }
        if (n == 0) {
            // Дочитали до конца файла — остальное оставляем нулями.
            break;
        }
        ++stats_.disk_reads;
        read_total += n;
    }

    lru_.push_front(pid);
    auto inserted = cache_.emplace(pid, std::make_pair(std::move(e), lru_.begin()));
    return inserted.first->second.first;
}

Page PageManager::read_page(PageId pid) {
    if (pid >= page_count_) {
        throw PageError("страница вне диапазона: " + std::to_string(pid));
    }
    Entry& e = load_into_cache(pid);
    return e.page;  // copy
}

void PageManager::write_page(PageId pid, const Page& page) {
    if (pid >= page_count_) {
        throw PageError("страница вне диапазона: " + std::to_string(pid));
    }
    if (page.size() != PAGE_SIZE) {
        throw PageError("неправильный размер page");
    }
    Entry& e = load_into_cache(pid);
    e.page = page;
    e.dirty = true;
}

PageId PageManager::allocate_page() {
    PageId new_id = page_count_;
    page_count_++;

    // Расширяем файл, чтобы он действительно содержал эту страницу.
    Entry e;
    e.page = make_page();
    e.dirty = true;

    evict_one_if_full();
    lru_.push_front(new_id);
    cache_.emplace(new_id, std::make_pair(std::move(e), lru_.begin()));
    return new_id;
}

void PageManager::sync() {
    // 1) сбросить все dirty-страницы.
    for (auto& kv : cache_) {
        flush_entry(kv.first, kv.second.first);
    }
    // 2) fsync — заставить ядро записать данные с буферов на диск.
    if (::fsync(fd_) < 0) {
        throw PageError(std::string("fsync: ") + std::strerror(errno));
    }
}

}  // namespace db
