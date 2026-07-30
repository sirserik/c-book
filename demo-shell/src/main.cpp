#include "builtins.h"
#include "pipeline.h"
#include "signals.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace {

std::string history_path() {
    const char* home = std::getenv("HOME");
    if (!home) return "./.myshell_history";
    return std::string(home) + "/.myshell_history";
}

}  // namespace

int main() {
    shell::install_shell_signal_handlers();
    shell::history_load(history_path());

    std::cout << "=== myshell ===\n";
    std::cout << "Builtins: cd, pwd, export, unset, history, echo, type, alias, exit.\n";
    std::cout << "Кавычки, пайпы, редиректы (>, >>, <, 2>, 2>&1).\n\n";

    while (true) {
        std::cout << "myshell$ ";
        std::cout.flush();

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
        if (line.empty()) continue;

        // Алиас раскрываем до разбора: `ll -a` → `ls -l -a`.
        std::string expanded = shell::expand_alias(line);

        shell::Pipeline p;
        try {
            p = shell::parse_pipeline(expanded);
        } catch (const std::exception& e) {
            std::cerr << "syntax: " << e.what() << "\n";
            continue;
        }
        if (p.empty()) continue;

        shell::history_add(line);

        const auto& first = p.commands[0].argv;
        if (!first.empty() && (first[0] == "exit" || first[0] == "quit")) {
            break;
        }

        // Builtin — только если одна команда и без редиректов (упрощение).
        if (p.commands.size() == 1 && p.commands[0].redirects.empty()) {
            int code = 0;
            if (shell::try_builtin(first, code)) {
                if (code != 0) std::cerr << "[exit " << code << "]\n";
                continue;
            }
        }

        int rc = shell::run_pipeline(p);
        if (rc != 0) {
            std::cerr << "[exit " << rc << "]\n";
        }
    }

    shell::history_save(history_path());
    return 0;
}
