#!/bin/sh
# CPU-время процесса (user+sys, все потоки) в окнах «игра» [2;7] с и
# «экран Game Over» [10;15] с, плюс пиковый RSS. Без нажатий клавиш
# (фиксированное зерно, Game Over наступает на ~8.3 с).
# Использование: ./cpu_window.sh ./orig_bench2
TCK=$(getconf CLK_TCK)
"$@" >/dev/null 2>&1 &
PID=$!
cpu() { awk '{print $14 + $15}' /proc/$PID/stat; }
sleep 2;  A=$(cpu)
sleep 5;  B=$(cpu)
sleep 3;  C=$(cpu)
sleep 5;  D=$(cpu)
HWM=$(grep VmHWM /proc/$PID/status | awk '{print $2}')
ANON=$(grep RssAnon /proc/$PID/status | awk '{print $2}')
kill $PID
awk -v a=$A -v b=$B -v c=$C -v d=$D -v t=$TCK -v h=$HWM -v n=$ANON -v p="$1" 'BEGIN {
  printf "%-16s игра: %5.1f мс CPU/с (%4.1f%% ядра) | Game Over: %5.1f мс CPU/с (%4.1f%% ядра) | VmHWM %d kB, RssAnon %d kB\n",
    p, (b-a)/t/5*1000, (b-a)/t/5*100, (d-c)/t/5*1000, (d-c)/t/5*100, h, n }'
