#include "btree.h"
#include "binary.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace db {

namespace {

constexpr std::size_t HEADER_BYTES = 16;

// Метка формата в странице 0: "MDB" + версия. Проверяется при открытии, чтобы
// не пытаться читать как дерево чужой или устаревший файл.
constexpr unsigned char MAGIC_0 = 'M';
constexpr unsigned char MAGIC_1 = 'D';
constexpr unsigned char MAGIC_2 = 'B';
constexpr unsigned char FORMAT_VERSION = 1;

void write_u16_at(Page& p, std::size_t off, std::uint16_t v) {
    p[off]     = static_cast<unsigned char>(v & 0xFF);
    p[off + 1] = static_cast<unsigned char>((v >> 8) & 0xFF);
}
std::uint16_t read_u16_at(const Page& p, std::size_t off) {
    return static_cast<std::uint16_t>(p[off]) |
           (static_cast<std::uint16_t>(p[off + 1]) << 8);
}
void write_u32_at(Page& p, std::size_t off, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[off + i] = static_cast<unsigned char>((v >> (i * 8)) & 0xFF);
    }
}
std::uint32_t read_u32_at(const Page& p, std::size_t off) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(p[off + i]) << (i * 8);
    }
    return v;
}
void write_i64_at(Page& p, std::size_t off, std::int64_t v) {
    std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        p[off + i] = static_cast<unsigned char>((u >> (i * 8)) & 0xFF);
    }
}
std::int64_t read_i64_at(const Page& p, std::size_t off) {
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= static_cast<std::uint64_t>(p[off + i]) << (i * 8);
    }
    return static_cast<std::int64_t>(u);
}

}  // namespace

BPlusTree::BPlusTree(PageManager& pm) : pm_(pm), root_page_id_(0) {
    if (pm_.page_count() == 0) {
        // Свежий файл: создаём метаданные (page 0) и пустой корень-лист (page 1).
        pm_.allocate_page();   // page 0 — метаданные
        PageId root = pm_.allocate_page();
        NodeData empty_leaf;
        empty_leaf.type = Leaf;
        write_node(root, empty_leaf);
        root_page_id_ = root;
        save_metadata();
        pm_.sync();
    } else {
        load_metadata();
    }
}

void BPlusTree::load_metadata() {
    Page meta = pm_.read_page(0);
    if (meta[4] != MAGIC_0 || meta[5] != MAGIC_1 || meta[6] != MAGIC_2) {
        throw PageError("это не файл mydb (нет метки MDB в странице 0)");
    }
    if (meta[7] != FORMAT_VERSION) {
        throw PageError("версия формата " + std::to_string(static_cast<int>(meta[7])) +
                        " не поддерживается (нужна " +
                        std::to_string(static_cast<int>(FORMAT_VERSION)) + ")");
    }
    root_page_id_ = read_u32_at(meta, 0);
}

void BPlusTree::save_metadata() {
    Page meta = make_page();
    write_u32_at(meta, 0, root_page_id_);
    meta[4] = MAGIC_0;
    meta[5] = MAGIC_1;
    meta[6] = MAGIC_2;
    meta[7] = FORMAT_VERSION;
    pm_.write_page(0, meta);
}

BPlusTree::NodeData BPlusTree::read_node(PageId pid) const {
    Page p = pm_.read_page(pid);
    NodeData n;
    n.type = static_cast<NodeType>(read_u16_at(p, 0));
    std::uint16_t nk = read_u16_at(p, 2);
    n.next_leaf = read_u32_at(p, 4);

    std::size_t off = HEADER_BYTES;
    n.keys.resize(nk);
    for (std::uint16_t i = 0; i < nk; ++i) {
        n.keys[i] = read_i64_at(p, off);
        off += 8;
    }
    if (n.type == Leaf) {
        n.values.resize(nk);
        for (std::uint16_t i = 0; i < nk; ++i) {
            n.values[i] = read_i64_at(p, off);
            off += 8;
        }
    } else {
        n.children.resize(static_cast<std::size_t>(nk) + 1);
        for (std::uint16_t i = 0; i <= nk; ++i) {
            n.children[i] = read_u32_at(p, off);
            off += 4;
        }
    }
    return n;
}

void BPlusTree::write_node(PageId pid, const NodeData& n) {
    Page p = make_page();
    write_u16_at(p, 0, static_cast<std::uint16_t>(n.type));
    write_u16_at(p, 2, static_cast<std::uint16_t>(n.keys.size()));
    write_u32_at(p, 4, n.next_leaf);

    std::size_t off = HEADER_BYTES;
    for (auto k : n.keys) {
        write_i64_at(p, off, k);
        off += 8;
    }
    if (n.type == Leaf) {
        for (auto v : n.values) {
            write_i64_at(p, off, v);
            off += 8;
        }
    } else {
        for (auto c : n.children) {
            write_u32_at(p, off, c);
            off += 4;
        }
    }
    pm_.write_page(pid, p);
}

PageId BPlusTree::find_leaf(std::int64_t key, std::vector<PageId>* path,
                            bool lower) const {
    PageId pid = root_page_id_;
    while (true) {
        NodeData n = read_node(pid);
        if (n.type == Leaf) return pid;
        if (path) path->push_back(pid);

        // upper_bound: keys[i-1] <= key < keys[i] — обычный спуск.
        // lower_bound: уходим в самое левое поддерево, где ключ ещё возможен;
        // при дубликатах, разъехавшихся через разделитель, это единственный
        // способ не пропустить левую половину.
        auto it = lower ? std::lower_bound(n.keys.begin(), n.keys.end(), key)
                        : std::upper_bound(n.keys.begin(), n.keys.end(), key);
        std::size_t idx = static_cast<std::size_t>(it - n.keys.begin());
        pid = n.children[idx];
    }
}

bool BPlusTree::find(std::int64_t key, std::int64_t& out_value) const {
    PageId leaf_pid = find_leaf(key, nullptr);
    NodeData leaf = read_node(leaf_pid);
    auto it = std::lower_bound(leaf.keys.begin(), leaf.keys.end(), key);
    if (it == leaf.keys.end() || *it != key) return false;
    out_value = leaf.values[static_cast<std::size_t>(it - leaf.keys.begin())];
    return true;
}

PageId BPlusTree::split_leaf(PageId leaf_pid, NodeData& leaf,
                             std::int64_t& sep_key) {
    // Разделяем: левый получает first half, правый — second half.
    std::size_t mid = leaf.keys.size() / 2;

    NodeData right;
    right.type = Leaf;
    right.keys.assign(leaf.keys.begin() + mid, leaf.keys.end());
    right.values.assign(leaf.values.begin() + mid, leaf.values.end());
    right.next_leaf = leaf.next_leaf;

    leaf.keys.resize(mid);
    leaf.values.resize(mid);

    PageId right_pid = pm_.allocate_page();
    leaf.next_leaf = right_pid;

    write_node(leaf_pid, leaf);
    write_node(right_pid, right);

    sep_key = right.keys.front();   // первый ключ правого — separator
    return right_pid;
}

PageId BPlusTree::split_internal(PageId int_pid, NodeData& node,
                                 std::int64_t& sep_key) {
    // В internal middle ключ ВЫНОСИТСЯ в parent, а не копируется.
    std::size_t mid = node.keys.size() / 2;
    sep_key = node.keys[mid];

    NodeData right;
    right.type = Internal;
    right.keys.assign(node.keys.begin() + mid + 1, node.keys.end());
    right.children.assign(node.children.begin() + mid + 1, node.children.end());

    node.keys.resize(mid);
    node.children.resize(mid + 1);

    PageId right_pid = pm_.allocate_page();
    write_node(int_pid, node);
    write_node(right_pid, right);
    return right_pid;
}

void BPlusTree::insert_in_parent(const std::vector<PageId>& path,
                                 std::int64_t sep_key,
                                 PageId right_child) {
    if (path.empty()) {
        // Root split — создаём новый root.
        NodeData new_root;
        new_root.type = Internal;
        new_root.keys.push_back(sep_key);
        new_root.children.push_back(root_page_id_);
        new_root.children.push_back(right_child);
        PageId new_root_pid = pm_.allocate_page();
        write_node(new_root_pid, new_root);
        root_page_id_ = new_root_pid;
        save_metadata();
        return;
    }

    PageId parent_pid = path.back();
    NodeData parent = read_node(parent_pid);

    auto it = std::upper_bound(parent.keys.begin(), parent.keys.end(), sep_key);
    std::size_t idx = static_cast<std::size_t>(it - parent.keys.begin());
    parent.keys.insert(parent.keys.begin() + idx, sep_key);
    parent.children.insert(parent.children.begin() + idx + 1, right_child);

    if (parent.keys.size() <= MAX_KEYS) {
        write_node(parent_pid, parent);
        return;
    }

    // Parent переполнен — split рекурсивно.
    std::int64_t new_sep = 0;
    PageId new_right = split_internal(parent_pid, parent, new_sep);
    std::vector<PageId> grand_path(path.begin(), path.end() - 1);
    insert_in_parent(grand_path, new_sep, new_right);
}

void BPlusTree::insert(std::int64_t key, std::int64_t value) {
    std::vector<PageId> path;
    PageId leaf_pid = find_leaf(key, &path);
    NodeData leaf = read_node(leaf_pid);

    auto it = std::lower_bound(leaf.keys.begin(), leaf.keys.end(), key);
    std::size_t idx = static_cast<std::size_t>(it - leaf.keys.begin());

    if (it != leaf.keys.end() && *it == key) {
        // Replace.
        leaf.values[idx] = value;
        write_node(leaf_pid, leaf);
        return;
    }

    leaf.keys.insert(leaf.keys.begin() + idx, key);
    leaf.values.insert(leaf.values.begin() + idx, value);

    if (leaf.keys.size() <= MAX_KEYS) {
        write_node(leaf_pid, leaf);
        return;
    }

    // Split.
    std::int64_t sep = 0;
    PageId right = split_leaf(leaf_pid, leaf, sep);
    insert_in_parent(path, sep, right);
}

int BPlusTree::height() const {
    int h = 1;
    PageId pid = root_page_id_;
    while (true) {
        NodeData n = read_node(pid);
        if (n.type == Leaf) return h;
        ++h;
        pid = n.children.front();
    }
}

void BPlusTree::insert_dup(std::int64_t key, std::int64_t value) {
    std::vector<PageId> path;
    PageId leaf_pid = find_leaf(key, &path);
    NodeData leaf = read_node(leaf_pid);

    // upper_bound — новая запись встаёт ПОСЛЕ уже имеющихся с тем же ключом.
    auto it = std::upper_bound(leaf.keys.begin(), leaf.keys.end(), key);
    std::size_t idx = static_cast<std::size_t>(it - leaf.keys.begin());

    leaf.keys.insert(leaf.keys.begin() + idx, key);
    leaf.values.insert(leaf.values.begin() + idx, value);

    if (leaf.keys.size() <= MAX_KEYS) {
        write_node(leaf_pid, leaf);
        return;
    }

    std::int64_t sep = 0;
    PageId right = split_leaf(leaf_pid, leaf, sep);
    insert_in_parent(path, sep, right);
}

void BPlusTree::find_all(std::int64_t key,
                         std::function<void(std::int64_t)> cb) const {
    PageId pid = find_leaf(key, nullptr, /*lower=*/true);
    while (pid != 0) {
        NodeData leaf = read_node(pid);
        for (std::size_t i = 0; i < leaf.keys.size(); ++i) {
            if (leaf.keys[i] < key) continue;
            if (leaf.keys[i] > key) return;   // отсортировано — дальше только больше
            cb(leaf.values[i]);
        }
        pid = leaf.next_leaf;
    }
}

void BPlusTree::scan(std::function<void(std::int64_t, std::int64_t)> cb) const {
    // Идём в самый левый лист.
    PageId pid = root_page_id_;
    while (true) {
        NodeData n = read_node(pid);
        if (n.type == Leaf) break;
        pid = n.children.front();
    }
    while (pid != 0) {
        NodeData leaf = read_node(pid);
        for (std::size_t i = 0; i < leaf.keys.size(); ++i) {
            cb(leaf.keys[i], leaf.values[i]);
        }
        pid = leaf.next_leaf;
    }
}

}  // namespace db
