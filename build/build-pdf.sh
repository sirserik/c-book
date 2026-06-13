#!/usr/bin/env bash
# Собирает PDF учебника из docs/tutorial/*.md.
# Использование: ./build/build-pdf.sh [output.pdf] [файл1.md файл2.md ...]

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/CPP-from-scratch-Tutorial.pdf}"
shift || true

if [ "$#" -gt 0 ]; then
  FILES=("$@")
else
  FILES=(
    "$ROOT/docs/tutorial/00-intro.md"
    "$ROOT/docs/tutorial/01-what-is-computer.md"
    "$ROOT/docs/tutorial/02-bits-bytes-text.md"
    "$ROOT/docs/tutorial/03-program-and-os.md"
    "$ROOT/docs/tutorial/04-algorithmic-thinking.md"
    "$ROOT/docs/tutorial/05-terminal-and-files.md"
    "$ROOT/docs/tutorial/06-toolchain.md"
    "$ROOT/docs/tutorial/07-compilation-model.md"
    "$ROOT/docs/tutorial/08-types-and-variables.md"
    "$ROOT/docs/tutorial/09-control-flow.md"
    "$ROOT/docs/tutorial/10-functions.md"
    "$ROOT/docs/tutorial/11-memory-and-raii.md"
    "$ROOT/docs/tutorial/12-strings-and-io.md"
    "$ROOT/docs/tutorial/13-stl-containers.md"
    "$ROOT/docs/tutorial/14-stl-algorithms.md"
    "$ROOT/docs/tutorial/15-rpg-design.md"
    "$ROOT/docs/tutorial/16-rpg-classes.md"
    "$ROOT/docs/tutorial/17-rpg-inheritance.md"
    "$ROOT/docs/tutorial/18-rpg-smart-pointers.md"
    "$ROOT/docs/tutorial/19-rpg-move-semantics.md"
    "$ROOT/docs/tutorial/20-rpg-templates.md"
    "$ROOT/docs/tutorial/21-rpg-lambdas.md"
    "$ROOT/docs/tutorial/22-rpg-exceptions.md"
    "$ROOT/docs/tutorial/23-rpg-parser.md"
    "$ROOT/docs/tutorial/24-rpg-save-load.md"
    "$ROOT/docs/tutorial/25-rpg-final.md"
    "$ROOT/docs/tutorial/26-shell-processes.md"
    "$ROOT/docs/tutorial/27-shell-pipes.md"
    "$ROOT/docs/tutorial/28-shell-signals.md"
    "$ROOT/docs/tutorial/29-shell-parser.md"
    "$ROOT/docs/tutorial/30-shell-builtins.md"
    "$ROOT/docs/tutorial/31-shell-utils.md"
    "$ROOT/docs/tutorial/32-db-binary-format.md"
    "$ROOT/docs/tutorial/33-db-page-cache.md"
    "$ROOT/docs/tutorial/34-db-btree.md"
    "$ROOT/docs/tutorial/35-db-wal.md"
    "$ROOT/docs/tutorial/36-db-mini-sql.md"
    "$ROOT/docs/tutorial/37-db-indexes.md"
    "$ROOT/docs/tutorial/38-db-repl.md"
    "$ROOT/docs/tutorial/39-db-final.md"
    "$ROOT/docs/tutorial/40-chat-sockets.md"
    "$ROOT/docs/tutorial/41-chat-threads.md"
    "$ROOT/docs/tutorial/42-chat-atomics.md"
    "$ROOT/docs/tutorial/43-chat-reactor.md"
    "$ROOT/docs/tutorial/44-chat-protocol.md"
    "$ROOT/docs/tutorial/45-chat-rooms.md"
    "$ROOT/docs/tutorial/46-chat-encryption.md"
    "$ROOT/docs/tutorial/47-chat-deploy.md"
    "$ROOT/docs/tutorial/48-cpp17-optional-variant.md"
    "$ROOT/docs/tutorial/49-cpp17-filesystem.md"
    "$ROOT/docs/tutorial/50-cpp17-string-view.md"
    "$ROOT/docs/tutorial/51-whats-next.md"
  )
fi

EXISTING=()
for f in "${FILES[@]}"; do
  if [ -f "$f" ]; then
    EXISTING+=("$f")
  else
    echo "skip: $f (нет файла)" >&2
  fi
done

echo "Сборка PDF: ${#EXISTING[@]} глав → $OUT"

# Генерим .tex через pandoc, потом гоним xelatex 3 раза подряд —
# чтобы TOC, ссылки и номера страниц сошлись после любого объёма правок.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pandoc \
  -s \
  "$ROOT/build/metadata.yaml" \
  "${EXISTING[@]}" \
  --pdf-engine=xelatex \
  --top-level-division=chapter \
  --highlight-style=tango \
  --listings=false \
  -o "$TMP/book.tex"

cd "$TMP"
for i in 1 2 3; do
  echo "  xelatex pass $i/3..."
  xelatex -interaction=batchmode -halt-on-error book.tex >/dev/null 2>&1 || {
    xelatex -interaction=nonstopmode book.tex 2>&1 | tail -30
    exit 1
  }
done

mv "$TMP/book.pdf" "$OUT"
cd "$ROOT"

echo "Готово: $OUT ($(du -h "$OUT" | cut -f1))"
