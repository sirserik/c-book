// Демонстрация move-семантики на простом примере. Класс Buffer владеет
// массивом на куче; копирование дорогое, перемещение дешёвое.

#include <iostream>
#include <string>
#include <utility>
#include <vector>

class Buffer {
public:
    explicit Buffer(std::size_t n, const std::string& tag)
        : data_(new int[n]), size_(n), tag_(tag) {
        std::cout << "[" << tag_ << "] ctor (size=" << size_ << ")\n";
    }

    ~Buffer() {
        std::cout << "[" << tag_ << "] dtor\n";
        delete[] data_;
    }

    // Копи-конструктор: новая память + копирование байтов.
    Buffer(const Buffer& other)
        : data_(new int[other.size_]), size_(other.size_), tag_(other.tag_ + "_copy") {
        std::cout << "[" << tag_ << "] copy ctor from " << other.tag_ << "\n";
        for (std::size_t i = 0; i < size_; ++i) data_[i] = other.data_[i];
    }

    // Move-конструктор: забираем чужой указатель, обнуляем источник.
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_), tag_(std::move(other.tag_) + "_moved") {
        std::cout << "[" << tag_ << "] move ctor\n";
        other.data_ = nullptr;
        other.size_ = 0;
    }

    Buffer& operator=(const Buffer&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    std::size_t size() const { return size_; }

private:
    int* data_;
    std::size_t size_;
    std::string tag_;
};

int main() {
    std::cout << "--- 1. Копирование ---\n";
    Buffer a(5, "a");
    Buffer b = a;   // copy ctor

    std::cout << "\n--- 2. Перемещение ---\n";
    Buffer c = std::move(a);   // move ctor; a после этого пустой

    std::cout << "\n--- 3. Push в vector с резервом ---\n";
    std::vector<Buffer> v;
    v.reserve(2);
    v.push_back(Buffer(10, "x"));   // временный → move
    v.push_back(Buffer(20, "y"));

    std::cout << "\n--- 4. Конец main ---\n";
    return 0;
}
