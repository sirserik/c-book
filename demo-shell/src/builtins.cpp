#include "builtins.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace shell {

namespace {

std::vector<std::string> g_history;
std::map<std::string, std::string> g_aliases;

int builtin_cd(const std::vector<std::string>& argv) {
    std::string target;
    if (argv.size() < 2) {
        const char* home = std::getenv("HOME");
        if (!home) {
            std::cerr << "cd: HOME не задана\n";
            return 1;
        }
        target = home;
    } else {
        target = argv[1];
    }
    if (chdir(target.c_str()) < 0) {
        std::cerr << "cd: " << target << ": " << std::strerror(errno) << "\n";
        return 1;
    }
    return 0;
}

int builtin_pwd() {
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) {
        std::cerr << "pwd: " << std::strerror(errno) << "\n";
        return 1;
    }
    std::cout << buf << "\n";
    return 0;
}

int builtin_export(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        // Без аргументов — печать всех переменных. Упростим: пропустим.
        std::cerr << "export: укажите KEY=VALUE\n";
        return 1;
    }
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string& kv = argv[i];
        auto eq = kv.find('=');
        if (eq == std::string::npos) {
            std::cerr << "export: ожидался формат KEY=VALUE: " << kv << "\n";
            return 1;
        }
        std::string key = kv.substr(0, eq);
        std::string val = kv.substr(eq + 1);
        if (setenv(key.c_str(), val.c_str(), 1) < 0) {
            std::cerr << "export: " << std::strerror(errno) << "\n";
            return 1;
        }
    }
    return 0;
}

int builtin_unset(const std::vector<std::string>& argv) {
    for (std::size_t i = 1; i < argv.size(); ++i) {
        if (unsetenv(argv[i].c_str()) < 0) {
            std::cerr << "unset: " << std::strerror(errno) << "\n";
            return 1;
        }
    }
    return 0;
}

int builtin_history() {
    std::size_t i = 1;
    for (const auto& line : g_history) {
        std::cout << "  " << i << "  " << line << "\n";
        ++i;
    }
    return 0;
}

bool is_builtin_name(const std::string& name);

// alias           — показать все алиасы
// alias ll="ls -l" — задать алиас
int builtin_alias(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        for (std::map<std::string, std::string>::const_iterator it = g_aliases.begin();
             it != g_aliases.end(); ++it) {
            std::cout << "alias " << it->first << "='" << it->second << "'\n";
        }
        return 0;
    }
    int rc = 0;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        std::size_t eq = argv[i].find('=');
        if (eq == std::string::npos) {
            std::map<std::string, std::string>::const_iterator it = g_aliases.find(argv[i]);
            if (it == g_aliases.end()) {
                std::cerr << "alias: " << argv[i] << ": не найден\n";
                rc = 1;
            } else {
                std::cout << "alias " << it->first << "='" << it->second << "'\n";
            }
            continue;
        }
        g_aliases[argv[i].substr(0, eq)] = argv[i].substr(eq + 1);
    }
    return rc;
}

int builtin_unalias(const std::vector<std::string>& argv) {
    for (std::size_t i = 1; i < argv.size(); ++i) {
        g_aliases.erase(argv[i]);
    }
    return 0;
}

// type — рассказать, чем окажется имя: встроенной командой или файлом из PATH.
int builtin_type(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        std::cerr << "type: укажите имя команды\n";
        return 1;
    }

    int rc = 0;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string& name = argv[i];

        if (is_builtin_name(name)) {
            std::cout << name << " — встроенная команда\n";
            continue;
        }

        // Имя с косой чертой ищем как путь, без — перебираем PATH (так же,
        // как это делает execvp).
        if (name.find('/') != std::string::npos) {
            if (access(name.c_str(), X_OK) == 0) {
                std::cout << name << " — " << name << "\n";
            } else {
                std::cerr << "type: " << name << ": не найдено\n";
                rc = 1;
            }
            continue;
        }

        const char* path = std::getenv("PATH");
        std::string dirs = path ? path : "";
        bool found = false;
        std::size_t start = 0;
        while (start <= dirs.size()) {
            std::size_t colon = dirs.find(':', start);
            std::string dir = dirs.substr(start, colon == std::string::npos
                                                     ? std::string::npos
                                                     : colon - start);
            if (dir.empty()) dir = ".";
            std::string candidate = dir + "/" + name;
            if (access(candidate.c_str(), X_OK) == 0) {
                std::cout << name << " — " << candidate << "\n";
                found = true;
                break;
            }
            if (colon == std::string::npos) break;
            start = colon + 1;
        }
        if (!found) {
            std::cerr << "type: " << name << ": не найдено\n";
            rc = 1;
        }
    }
    return rc;
}

int builtin_echo(const std::vector<std::string>& argv) {
    bool newline = true;
    std::size_t start = 1;
    if (argv.size() > 1 && argv[1] == "-n") {
        newline = false;
        start = 2;
    }
    for (std::size_t i = start; i < argv.size(); ++i) {
        if (i > start) std::cout << " ";
        std::cout << argv[i];
    }
    if (newline) std::cout << "\n";
    return 0;
}

bool is_builtin_name(const std::string& name) {
    return name == "cd" || name == "pwd" || name == "export" ||
           name == "unset" || name == "history" || name == "echo" ||
           name == "type" || name == "alias" || name == "unalias" ||
           name == "exit" || name == "quit";
}

}  // namespace

bool try_builtin(const std::vector<std::string>& argv, int& out_code) {
    if (argv.empty()) {
        out_code = 0;
        return true;
    }
    const std::string& cmd = argv[0];

    if (cmd == "cd")       { out_code = builtin_cd(argv);      return true; }
    if (cmd == "pwd")      { out_code = builtin_pwd();          return true; }
    if (cmd == "export")   { out_code = builtin_export(argv);   return true; }
    if (cmd == "unset")    { out_code = builtin_unset(argv);    return true; }
    if (cmd == "history")  { out_code = builtin_history();      return true; }
    if (cmd == "echo")     { out_code = builtin_echo(argv);     return true; }
    if (cmd == "type")     { out_code = builtin_type(argv);     return true; }
    if (cmd == "alias")    { out_code = builtin_alias(argv);    return true; }
    if (cmd == "unalias")  { out_code = builtin_unalias(argv);  return true; }

    return false;
}

std::string expand_alias(const std::string& line) {
    // Берём первое слово строки — только оно может быть алиасом.
    std::size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return line;
    std::size_t end = line.find_first_of(" \t", start);
    std::string first = line.substr(start, end == std::string::npos
                                               ? std::string::npos
                                               : end - start);

    std::map<std::string, std::string>::const_iterator it = g_aliases.find(first);
    if (it == g_aliases.end()) return line;

    std::string rest = (end == std::string::npos) ? "" : line.substr(end);
    return it->second + rest;
}

void history_add(const std::string& line) {
    if (line.empty()) return;
    if (!g_history.empty() && g_history.back() == line) return;  // не дублируем
    g_history.push_back(line);
}

void history_load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) g_history.push_back(line);
    }
}

void history_save(const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return;
    for (const auto& line : g_history) {
        out << line << "\n";
    }
}

const std::vector<std::string>& history() {
    return g_history;
}

}  // namespace shell
