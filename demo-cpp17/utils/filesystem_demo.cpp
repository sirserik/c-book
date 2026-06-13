// std::filesystem — кросс-платформенная работа с файлами/папками.
// Заменяет ручные POSIX-вызовы opendir/stat/mkdir и Windows WinAPI.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main() {
    std::cout << "Current dir: " << fs::current_path() << "\n\n";

    // 1. Создать иерархию каталогов.
    fs::path base = "/tmp/fs_demo";
    fs::remove_all(base);   // очистим
    fs::create_directories(base / "subdir1" / "deeper");

    // 2. Создать файлы.
    std::ofstream(base / "a.txt") << "hello a";
    std::ofstream(base / "subdir1" / "b.txt") << "hello b";
    std::ofstream(base / "subdir1" / "deeper" / "c.txt") << "hello c";

    // 3. Обход дерева — recursive_directory_iterator.
    std::cout << "=== Все файлы в " << base << " ===\n";
    for (const auto& entry : fs::recursive_directory_iterator(base)) {
        if (entry.is_regular_file()) {
            std::cout << entry.path().string()
                      << "  (" << entry.file_size() << " bytes)\n";
        } else if (entry.is_directory()) {
            std::cout << entry.path().string() << "/\n";
        }
    }

    // 4. Запрос свойств.
    fs::path file = base / "a.txt";
    std::cout << "\n=== Свойства " << file << " ===\n";
    std::cout << "exists: " << fs::exists(file) << "\n";
    std::cout << "is_regular_file: " << fs::is_regular_file(file) << "\n";
    std::cout << "file_size: " << fs::file_size(file) << "\n";

    // 5. Манипуляция: copy, rename, remove.
    fs::copy_file(file, base / "a_copy.txt");
    fs::rename(base / "a_copy.txt", base / "a_renamed.txt");
    std::cout << "\nПосле copy+rename:\n";
    for (const auto& entry : fs::directory_iterator(base)) {
        std::cout << "  " << entry.path().filename().string() << "\n";
    }

    // 6. Path API: разбор пути.
    fs::path p = "/Users/alice/docs/report.pdf";
    std::cout << "\n=== Разбор " << p << " ===\n";
    std::cout << "parent_path: " << p.parent_path() << "\n";
    std::cout << "filename:    " << p.filename() << "\n";
    std::cout << "stem:        " << p.stem() << "\n";
    std::cout << "extension:   " << p.extension() << "\n";

    // 7. Конкатенация: оператор /.
    fs::path data_dir = "data";
    fs::path config = data_dir / "config" / "app.toml";
    std::cout << "\nСостав: " << config << "\n";

    // 8. Очистка.
    fs::remove_all(base);
    std::cout << "\nОчищено " << base << "\n";

    return 0;
}
