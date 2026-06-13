#ifndef DB_BTREE_H
#define DB_BTREE_H

#include "page_manager.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace db {

// B+tree, ключ int64, значение int64.
//
// Структура файла:
//   Page 0 — метаданные дерева (root_page_id).
//   Page 1..N — узлы (internal или leaf).
//
// Layout узла (4096 байт):
//   [0..1]   u16 type (0=Internal, 1=Leaf)
//   [2..3]   u16 num_keys
//   [4..7]   u32 next_leaf (только для leaf, для internal — 0)
//   [8..15]  reserved
//   [16..]   keys (int64), потом children (PageId u32) или values (int64)
//
// MAX_KEYS — компромисс между производительностью и читаемостью кода.
// Большое значение → меньше уровней дерева, меньше IO; маленькое → splits
// видны на учебных вставках.
class BPlusTree {
public:
    // Малый порядок — для демонстрации split'ов на 10-20 вставках.
    static constexpr std::size_t MAX_KEYS = 4;

    explicit BPlusTree(PageManager& pm);

    // Поиск. Возвращает true, если ключ найден, и кладёт значение в out_value.
    bool find(std::int64_t key, std::int64_t& out_value) const;

    // Вставка. Если ключ уже есть — обновляет значение.
    void insert(std::int64_t key, std::int64_t value);

    // Обход всех пар в отсортированном порядке через leaf-цепочку.
    void scan(std::function<void(std::int64_t, std::int64_t)> cb) const;

    // Для тестов/отладки: id корневой страницы.
    PageId root_page() const { return root_page_id_; }

private:
    enum NodeType : std::uint16_t { Internal = 0, Leaf = 1 };

    struct NodeData {
        NodeType type = Leaf;
        std::vector<std::int64_t> keys;
        std::vector<std::int64_t> values;
        std::vector<PageId> children;
        PageId next_leaf = 0;
    };

    NodeData read_node(PageId pid) const;
    void write_node(PageId pid, const NodeData& n);

    void load_metadata();
    void save_metadata();

    // Спуск до листа. Записывает в path id всех внутренних узлов на пути.
    PageId find_leaf(std::int64_t key, std::vector<PageId>* path) const;

    // Разделить узел: вернуть id нового правого узла и ключ-разделитель.
    PageId split_leaf(PageId leaf_pid, NodeData& leaf, std::int64_t& sep_key);
    PageId split_internal(PageId int_pid, NodeData& node, std::int64_t& sep_key);

    // Вставить (sep_key, right_child) в parent. Может рекурсивно
    // развести splits до корня.
    void insert_in_parent(const std::vector<PageId>& path,
                          std::int64_t sep_key,
                          PageId right_child);

    PageManager& pm_;
    PageId root_page_id_;
};

}  // namespace db

#endif
