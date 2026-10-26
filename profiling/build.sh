#!/bin/sh
# Сборка всех вариантов для замеров. Запускать из каталога profiling/.
set -e
cd "$(dirname "$0")"
GL="$(pkg-config --cflags --libs glfw3) -lGL"
./make_orig_bench.sh
gcc -O2 -shared -fPIC -o glcount.so glcount.c -ldl
gcc -O2 -g -DBENCH=1 -I. -o orig_bench1 tetris_orig_bench.c $GL
gcc -O2 -g -DBENCH=2 -I. -o orig_bench2 tetris_orig_bench.c $GL
gcc -O2 -g -Wall -Wextra -DBENCH=1 -I. -o new_bench1 ../tetris.c $GL
gcc -O2 -g -Wall -Wextra -DBENCH=2 -I. -o new_bench2 ../tetris.c $GL
echo "build ok"
