#ifndef SHELL_EXEC_RUNNER_H
#define SHELL_EXEC_RUNNER_H

#include <string>
#include <vector>

namespace shell {

// Токенизатор с поддержкой:
//   — двойные кавычки "..." (внутри обычные пробелы — часть слова),
//   — одинарные кавычки '...' (буквально, никакого escape),
//   — escape через \,
//   — спец-токены |, <, >, >>, 2>, 2>>, 2>&1 как отдельные строки.
// Бросает std::runtime_error при незакрытой кавычке.
std::vector<std::string> tokenize(const std::string& line);

}  // namespace shell

#endif
