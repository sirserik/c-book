#include "signals.h"

#include <csignal>

namespace shell {

void install_shell_signal_handlers() {
    // sigaction вместо signal — портабельнее и контролируемее.
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    sigaction(SIGPIPE, &sa, nullptr);
    // SIGCHLD оставляем по умолчанию — мы синхронно waitpid'аем.
    // Иначе дети-зомби были бы проблемой.
}

void restore_default_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    sigaction(SIGPIPE, &sa, nullptr);
}

}  // namespace shell
