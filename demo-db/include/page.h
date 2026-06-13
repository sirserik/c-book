#ifndef DB_PAGE_H
#define DB_PAGE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace db {

// Размер страницы — 4 KiB. Совпадает с размером страницы памяти в большинстве ОС
// и размером блока в большинстве файловых систем. Чтение/запись страницы — это
// один блок IO, без частичных операций.
constexpr std::size_t PAGE_SIZE = 4096;

// Адрес страницы — индекс в файле. Файл рассматривается как массив страниц:
// страница 0 это байты 0..4095, страница 1 — 4096..8191, и так далее.
using PageId = std::uint32_t;

// Страница — буфер из 4 KiB байтов. Используем vector, а не array, чтобы можно
// было std::move без копий.
using Page = std::vector<unsigned char>;

inline Page make_page() {
    return Page(PAGE_SIZE, 0);
}

}  // namespace db

#endif
