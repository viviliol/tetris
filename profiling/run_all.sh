#!/bin/sh
# Все замеры «до/после» одной командой. Запуск: ./profiling/run_all.sh [--callgrind]
# Во время замеров на экране будут открываться окна игры — это нормально.
#
# Режимы (см. make_orig_bench.sh и #if BENCH в tetris.c):
#   *_bench1 — фиксированная сцена (поле заполнено наполовину), гравитация выключена,
#              кадр перестраивается целиком каждый раз  → стоимость одного кадра;
#   *_bench2 — обычная игра с фиксированным зерном, без нажатий клавиш → реальная нагрузка.
set -e
cd "$(dirname "$0")"
./build.sh >/dev/null

# LD_PRELOAD — относительный путь: в абсолютном мешает пробел в «tetris lb»
P=./glcount.so

echo "================ 1. Один кадр, vsync выключен (5000 кадров, 3 прогона)"
for b in orig_bench1 new_bench1; do
    for i in 1 2 3; do
        GLCOUNT_NOVSYNC=1 GLCOUNT_FRAMES=5000 LD_PRELOAD=$P ./$b 2>&1 | grep 'avg frame' | sed "s/^/$b  /"
    done
done

echo
echo "================ 2. Вызовы OpenGL за кадр"
for b in orig_bench1 new_bench1; do
    echo "--- $b"
    GLCOUNT_NOVSYNC=1 GLCOUNT_FRAMES=1000 LD_PRELOAD=$P ./$b 2>&1 | grep 'per frame'
done

echo
echo "================ 3. CPU процесса на 5000 кадров (perf stat)"
for b in orig_bench1 new_bench1; do
    GLCOUNT_NOVSYNC=1 GLCOUNT_FRAMES=5000 LD_PRELOAD=$P perf stat -e task-clock ./$b 2>&1 \
        | grep -E 'task-clock|seconds user|seconds sys' | tr -s ' ' | tr '\n' ' ' | sed "s/^/$b /"
    echo
done

echo
echo "================ 4. Реальная игра, vsync включён (CPU в окнах 2–7 с и 10–15 с)"
for i in 1 2; do
    ./cpu_window.sh ./orig_bench2
    ./cpu_window.sh ./new_bench2
done

echo
echo "================ 5. Где тратится CPU (perf record, по библиотекам)"
for b in orig_bench1 new_bench1; do
    echo "--- $b"
    GLCOUNT_NOVSYNC=1 GLCOUNT_FRAMES=5000 LD_PRELOAD=$P perf record -q -F 4000 -e cpu-clock:u -o perf_$b.data ./$b >/dev/null 2>&1
    perf report -i perf_$b.data --sort dso --stdio 2>/dev/null | grep '%' | head -5
done

echo
echo "================ 6. Размер секций программы"
size ../original/tetris ../tetris

if [ "$1" = "--callgrind" ]; then
    echo
    echo "================ 7. Инструкции CPU в функциях отрисовки (callgrind, 300 кадров, ~5 мин)"
    GL="$(pkg-config --cflags --libs glfw3) -lGL"
    gcc -O2 -g -fno-inline -DBENCH=1 -I. -o orig_cg tetris_orig_bench.c $GL
    gcc -O2 -g -fno-inline -DBENCH=1 -I. -o new_cg ../tetris.c $GL
    GLCOUNT_NOVSYNC=1 GLCOUNT_FRAMES=200 LD_PRELOAD=$P valgrind --tool=callgrind --toggle-collect=draw_all_gl \
        --callgrind-out-file=callgrind_orig.out ./orig_cg >/dev/null 2>&1 &
    GLCOUNT_NOVSYNC=1 GLCOUNT_FRAMES=200 LD_PRELOAD=$P valgrind --tool=callgrind --toggle-collect=build_dynamic \
        --toggle-collect=render --callgrind-out-file=callgrind_new.out ./new_cg >/dev/null 2>&1 &
    wait
    for f in callgrind_orig.out callgrind_new.out; do
        echo "--- $f (на 300 кадров)"
        callgrind_annotate --inclusive=yes $f 2>/dev/null | grep -E '\.c:[a-z_]+ \[' | head -8
    done
fi
