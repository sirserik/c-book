// Тесты токенизатора и парсера пайплайна.
// Сборка и запуск:  make tests && ./build/tests/test_tokenize
#include "exec_runner.h"
#include "pipeline.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

std::string join(const std::vector<std::string>& v) {
    std::string r;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) r += " | ";
        r += "[" + v[i] + "]";
    }
    return r;
}

void expect_tokens(const std::string& line,
                   const std::vector<std::string>& expected) {
    ++checks;
    std::vector<std::string> got = shell::tokenize(line);
    if (got != expected) {
        ++failures;
        std::cout << "FAIL  tokenize(\"" << line << "\")\n"
                  << "  ждали:  " << join(expected) << "\n"
                  << "  выдало: " << join(got) << "\n";
    }
}

void expect_throw(const std::string& line) {
    ++checks;
    try {
        shell::tokenize(line);
        ++failures;
        std::cout << "FAIL  tokenize(\"" << line << "\") — ждали исключение\n";
    } catch (const std::runtime_error&) {
        // как и задумано
    }
}

void expect_pipeline(const std::string& line, std::size_t commands,
                     std::size_t redirects_in_first) {
    ++checks;
    try {
        shell::Pipeline p = shell::parse_pipeline(line);
        if (p.size() != commands ||
            p.commands[0].redirects.size() != redirects_in_first) {
            ++failures;
            std::cout << "FAIL  parse_pipeline(\"" << line << "\"): команд "
                      << p.size() << " (ждали " << commands << "), редиректов у первой "
                      << p.commands[0].redirects.size() << " (ждали "
                      << redirects_in_first << ")\n";
        }
    } catch (const std::exception& e) {
        ++failures;
        std::cout << "FAIL  parse_pipeline(\"" << line << "\") бросил: "
                  << e.what() << "\n";
    }
}

void expect_parse_error(const std::string& line) {
    ++checks;
    try {
        shell::parse_pipeline(line);
        ++failures;
        std::cout << "FAIL  parse_pipeline(\"" << line << "\") — ждали ошибку\n";
    } catch (const std::runtime_error&) {
        // как и задумано
    }
}

}  // namespace

int main() {
    // Простые слова.
    expect_tokens("echo hello", {"echo", "hello"});
    expect_tokens("  ls   -l  ", {"ls", "-l"});
    expect_tokens("", {});

    // Кавычки.
    expect_tokens("echo \"hello world\"", {"echo", "hello world"});
    expect_tokens("echo 'hello world'", {"echo", "hello world"});
    expect_tokens("echo 'one \"two\"'", {"echo", "one \"two\""});
    expect_tokens("echo \"a'b\"", {"echo", "a'b"});
    expect_tokens("echo \"\"", {"echo"});          // пустое слово теряется — как в главе 29

    // Escape.
    expect_tokens("echo a\\ b", {"echo", "a b"});
    expect_tokens("echo \"a\\\"b\"", {"echo", "a\"b"});
    expect_tokens("echo 'a\\b'", {"echo", "a\\b"});  // внутри '' escape не работает

    // Спец-токены.
    expect_tokens("ls | wc", {"ls", "|", "wc"});
    expect_tokens("ls|wc", {"ls", "|", "wc"});
    expect_tokens("cmd > file.txt", {"cmd", ">", "file.txt"});
    expect_tokens("cmd >> file.txt", {"cmd", ">>", "file.txt"});
    expect_tokens("cmd < in.txt", {"cmd", "<", "in.txt"});
    expect_tokens("cmd 2> err.txt", {"cmd", "2>", "err.txt"});
    expect_tokens("cmd 2>> err.txt", {"cmd", "2>>", "err.txt"});
    expect_tokens("cmd 2>&1", {"cmd", "2>&1"});

    // Цифра 2 в середине слова — не редирект.
    expect_tokens("cat file2.txt", {"cat", "file2.txt"});

    // Незакрытые кавычки.
    expect_throw("echo \"no end");
    expect_throw("echo 'no end");

    // Парсер.
    expect_pipeline("ls", 1, 0);
    expect_pipeline("ls | wc -l", 2, 0);
    expect_pipeline("ls > a.txt", 1, 1);
    expect_pipeline("ls > a.txt 2>&1", 1, 2);
    expect_pipeline("grep \"hello world\" f.txt > out.txt", 1, 1);
    expect_parse_error("| ls");
    expect_parse_error("ls |");
    expect_parse_error("ls >");
    expect_parse_error("ls > | wc");

    std::cout << (failures ? "ЕСТЬ ОШИБКИ: " : "все проверки прошли: ")
              << (checks - failures) << "/" << checks << "\n";
    return failures ? 1 : 0;
}
