#include "builtins.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace shell {

namespace {

std::vector<std::string> g_history;

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

    return false;
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
