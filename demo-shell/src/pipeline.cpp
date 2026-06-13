#include "pipeline.h"
#include "exec_runner.h"
#include "signals.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace shell {

namespace {

bool is_redirect_token(const std::string& t) {
    return t == "<" || t == ">" || t == ">>" ||
           t == "2>" || t == "2>>" || t == "2>&1";
}

Redirect::Type redirect_type_for(const std::string& t) {
    if (t == "<")    return Redirect::Input;
    if (t == ">")    return Redirect::Output;
    if (t == ">>")   return Redirect::Append;
    if (t == "2>")   return Redirect::ErrOutput;
    if (t == "2>>")  return Redirect::ErrAppend;
    if (t == "2>&1") return Redirect::ErrToOut;
    throw std::runtime_error("не редирект: " + t);
}

// Превратить плоский список токенов между | в Command.
Command parse_command(std::vector<std::string>::iterator begin,
                      std::vector<std::string>::iterator end) {
    Command cmd;
    for (auto it = begin; it != end; ++it) {
        if (is_redirect_token(*it)) {
            Redirect r;
            r.type = redirect_type_for(*it);
            if (r.type == Redirect::ErrToOut) {
                // 2>&1 — без аргумента-файла
            } else {
                if (it + 1 == end || is_redirect_token(*(it + 1)) || *(it + 1) == "|") {
                    throw std::runtime_error("редирект '" + *it + "' без файла");
                }
                ++it;
                r.target = *it;
            }
            cmd.redirects.push_back(std::move(r));
        } else {
            cmd.argv.push_back(*it);
        }
    }
    return cmd;
}

void close_all_pipes(std::vector<std::array<int, 2>>& pipes) {
    for (auto& p : pipes) {
        if (p[0] >= 0) { close(p[0]); p[0] = -1; }
        if (p[1] >= 0) { close(p[1]); p[1] = -1; }
    }
}

std::vector<char*> to_c_argv(const std::vector<std::string>& args) {
    std::vector<char*> r;
    r.reserve(args.size() + 1);
    for (const auto& s : args) r.push_back(const_cast<char*>(s.c_str()));
    r.push_back(nullptr);
    return r;
}

// Применить редиректы в текущем процессе (вызывается в ребёнке).
// При ошибке завершается с _exit.
void apply_redirects(const std::vector<Redirect>& redirects) {
    for (const auto& r : redirects) {
        switch (r.type) {
            case Redirect::Input: {
                int fd = open(r.target.c_str(), O_RDONLY);
                if (fd < 0) {
                    std::cerr << r.target << ": " << std::strerror(errno) << "\n";
                    _exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
                break;
            }
            case Redirect::Output: {
                int fd = open(r.target.c_str(),
                              O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    std::cerr << r.target << ": " << std::strerror(errno) << "\n";
                    _exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
                break;
            }
            case Redirect::Append: {
                int fd = open(r.target.c_str(),
                              O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd < 0) {
                    std::cerr << r.target << ": " << std::strerror(errno) << "\n";
                    _exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
                break;
            }
            case Redirect::ErrOutput: {
                int fd = open(r.target.c_str(),
                              O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    std::cerr << r.target << ": " << std::strerror(errno) << "\n";
                    _exit(1);
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
                break;
            }
            case Redirect::ErrAppend: {
                int fd = open(r.target.c_str(),
                              O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd < 0) {
                    std::cerr << r.target << ": " << std::strerror(errno) << "\n";
                    _exit(1);
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
                break;
            }
            case Redirect::ErrToOut: {
                // stderr → туда же, куда stdout сейчас идёт.
                dup2(STDOUT_FILENO, STDERR_FILENO);
                break;
            }
        }
    }
}

}  // namespace

Pipeline parse_pipeline(const std::string& line) {
    Pipeline p;
    auto tokens = tokenize(line);
    if (tokens.empty()) return p;

    // Разбиваем по `|` на сегменты, каждый — Command.
    auto seg_start = tokens.begin();
    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        if (*it == "|") {
            if (seg_start == it) {
                throw std::runtime_error("пустая команда перед '|'");
            }
            p.commands.push_back(parse_command(seg_start, it));
            seg_start = it + 1;
        }
    }
    if (seg_start != tokens.end()) {
        p.commands.push_back(parse_command(seg_start, tokens.end()));
    } else {
        throw std::runtime_error("пустая команда после '|'");
    }

    // Проверка: каждая команда имеет хотя бы один аргумент.
    for (const auto& cmd : p.commands) {
        if (cmd.argv.empty()) {
            throw std::runtime_error("команда без названия");
        }
    }
    return p;
}

int run_pipeline(const Pipeline& p) {
    if (p.empty()) return 0;
    const int n = static_cast<int>(p.commands.size());

    std::vector<std::array<int, 2>> pipes(n > 0 ? n - 1 : 0);
    for (int i = 0; i < n - 1; ++i) {
        if (pipe(pipes[i].data()) < 0) {
            std::cerr << "pipe failed: " << std::strerror(errno) << "\n";
            close_all_pipes(pipes);
            return -1;
        }
    }

    std::vector<pid_t> children(n);
    for (int i = 0; i < n; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "fork failed: " << std::strerror(errno) << "\n";
            close_all_pipes(pipes);
            return -1;
        }
        if (pid == 0) {
            restore_default_signal_handlers();

            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < n - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            close_all_pipes(pipes);

            // Редиректы применяются ПОСЛЕ pipe-dup2, поэтому они «победнее».
            // То есть `cmd > file | other` — stdout cmd идёт в file, не в pipe.
            apply_redirects(p.commands[i].redirects);

            auto argv = to_c_argv(p.commands[i].argv);
            execvp(argv[0], argv.data());
            std::cerr << "exec '" << argv[0] << "' failed: "
                      << std::strerror(errno) << "\n";
            _exit(127);
        }
        children[i] = pid;
    }

    close_all_pipes(pipes);

    int last_status = 0;
    for (int i = 0; i < n; ++i) {
        int status = 0;
        if (waitpid(children[i], &status, 0) < 0) {
            std::cerr << "waitpid: " << std::strerror(errno) << "\n";
            continue;
        }
        if (i == n - 1) {
            if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
            else last_status = -1;
        }
    }
    return last_status;
}

}  // namespace shell
