#!/bin/sh
# Создаёт tetris_orig_bench.c — оригинал + режимы для замеров:
#   -DBENCH=1 : фиксированная сцена (поле заполнено наполовину), гравитация выключена
#   -DBENCH=2 : обычная игра с фиксированным зерном rand()
# Сама отрисовка/логика оригинала не меняется.
set -e
cd "$(dirname "$0")"
python3 - <<'PY'
src = open('../original/tetris.c').read()
src = src.replace('    srand((unsigned)time(NULL));',
'''#ifdef BENCH
    srand(12345);
#else
    srand((unsigned)time(NULL));
#endif''')
src = src.replace('''    g_next = new_piece();
    spawn_next();
''', '''    g_next = new_piece();
    spawn_next();
#if BENCH == 1
#include "bench_fill.inc"
#endif
''')
src = src.replace('        if (!g_state.paused && elapsed_ms >= fall_delay_ms()) {',
'''#if BENCH == 1
        (void)elapsed_ms;
        if (0) {
#else
        if (!g_state.paused && elapsed_ms >= fall_delay_ms()) {
#endif''')
open('tetris_orig_bench.c', 'w').write(src)
PY
