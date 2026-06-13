#ifndef SHELL_SIGNALS_H
#define SHELL_SIGNALS_H

namespace shell {

// Установить обработчики сигналов для shell.
//   SIGINT (Ctrl+C) — игнорируем; будет идти только в ребёнка.
//   SIGQUIT (Ctrl+\) — то же.
//   SIGPIPE — игнорируем (write вернёт EPIPE вместо смерти).
void install_shell_signal_handlers();

// Восстановить дефолтные обработчики в ребёнке после fork (до exec).
void restore_default_signal_handlers();

}  // namespace shell

#endif
