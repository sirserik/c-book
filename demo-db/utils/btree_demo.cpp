// btree_demo — показывает, как растёт B+дерево: сплиты листьев, появление
// корня-внутреннего узла, цепочка листьев, число обращений к страницам.
//
// Запуск: ./build/btree_demo [файл]

#include "btree.h"
#include "page_manager.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

void show_stats(const char* title, const db::PageManager& pm) {
    const db::PageManager::Stats& s = pm.stats();
    std::cout << title << ": страниц в файле " << pm.page_count()
              << ", обращений к страницам " << (s.hits + s.misses)
              << " (промахов " << s.misses << ")\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string path = (argc > 1) ? argv[1] : "data/btree_demo.db";
    std::remove(path.c_str());

    db::PageManager pm(path, 64);
    db::BPlusTree tree(pm);

    std::cout << "MAX_KEYS = " << db::BPlusTree::MAX_KEYS
              << " (в учебном дереве нарочно мало, чтобы сплиты были видны)\n\n";

    std::cout << "вставка   страниц   корень\n";
    for (int key = 1; key <= 16; ++key) {
        db::PageId root_before = tree.root_page();
        db::PageId pages_before = pm.page_count();

        tree.insert(key, key * 100);

        db::PageId root_after = tree.root_page();
        db::PageId pages_after = pm.page_count();

        std::cout << "  " << (key < 10 ? " " : "") << key
                  << "        " << pages_after << "        " << root_after;
        if (pages_after != pages_before) std::cout << "   ← сплит, новых страниц: "
                                                   << (pages_after - pages_before);
        if (root_after != root_before) std::cout << "   ← новый корень";
        std::cout << "\n";
    }

    std::cout << "\nВысота дерева: ";
    std::cout << tree.height() << "\n";

    std::cout << "\nОбход по цепочке листьев: ";
    tree.scan([](std::int64_t k, std::int64_t) { std::cout << k << " "; });
    std::cout << "\n\n";

    pm.reset_stats();
    std::int64_t v = 0;
    tree.find(13, v);
    show_stats("поиск ключа 13", pm);

    pm.reset_stats();
    int found = 0;
    for (int key = 1; key <= 16; ++key) {
        if (tree.find(key, v)) ++found;
    }
    show_stats("поиск всех 16 ключей", pm);
    std::cout << "найдено " << found << " из 16\n";

    pm.reset_stats();
    int scanned = 0;
    tree.scan([&](std::int64_t, std::int64_t) { ++scanned; });
    show_stats("полный обход", pm);
    std::cout << "обошли " << scanned << " записей\n";

    pm.sync();
    std::remove(path.c_str());
    return 0;
}
