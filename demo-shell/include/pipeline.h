#ifndef SHELL_PIPELINE_H
#define SHELL_PIPELINE_H

#include <string>
#include <vector>

namespace shell {

struct Redirect {
    enum Type {
        Input,        // < file
        Output,       // > file
        Append,       // >> file
        ErrOutput,    // 2> file
        ErrAppend,    // 2>> file
        ErrToOut,     // 2>&1 (без файла)
    };
    Type type;
    std::string target;  // имя файла; пусто для ErrToOut
};

struct Command {
    std::vector<std::string> argv;
    std::vector<Redirect> redirects;
};

struct Pipeline {
    std::vector<Command> commands;

    bool empty() const { return commands.empty(); }
    std::size_t size() const { return commands.size(); }
};

// Парсит строку через tokenize и собирает Pipeline. Бросает std::runtime_error
// при синтаксических ошибках (пустая команда, редирект без цели и т.д.).
Pipeline parse_pipeline(const std::string& line);

int run_pipeline(const Pipeline& p);

}  // namespace shell

#endif
