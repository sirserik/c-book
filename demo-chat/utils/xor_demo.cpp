// XOR pre-key — учебное шифрование. НЕ ИСПОЛЬЗОВАТЬ В PRODUCTION.
// XOR с фиксированным ключом легко ломается через известный plaintext
// (атака crib) или статистический анализ.

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

void xor_in_place(std::string& data, const std::string& key) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % key.size()];
    }
}

void hexdump(const std::string& s) {
    for (unsigned char c : s) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(c) << " ";
    }
    std::cout << std::dec << "\n";
}

}  // namespace

int main() {
    std::string key = "secret123";
    std::string plain = "Hello, this is a confidential message!";

    std::cout << "Plain:     '" << plain << "'\n";
    std::cout << "Plain hex: "; hexdump(plain);

    std::string cipher = plain;
    xor_in_place(cipher, key);
    std::cout << "\nCipher hex: "; hexdump(cipher);

    // Decrypt — тот же XOR.
    std::string decrypted = cipher;
    xor_in_place(decrypted, key);
    std::cout << "\nDecrypted: '" << decrypted << "'\n";

    // Демонстрация атаки: если известен plaintext (например "Hello, "),
    // мы восстанавливаем ключ:
    std::cout << "\n=== Атака: known-plaintext ===\n";
    std::string known = "Hello, ";
    std::string recovered_key;
    for (std::size_t i = 0; i < known.size(); ++i) {
        recovered_key.push_back(known[i] ^ cipher[i]);
    }
    std::cout << "Зная начало plaintext, восстановили ключ:\n"
              << "  Восстановленный: '" << recovered_key << "'\n"
              << "  Реальный:        '" << key.substr(0, recovered_key.size()) << "'\n";

    return 0;
}
