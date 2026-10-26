/* Детерминированная сцена: нижние 10 рядов заполнены «шахматно» с дыркой,
   счёт 123456 — одинаково для старой и новой версии. */
for (int r = BOARD_H / 2; r < BOARD_H; r++)
    for (int c = 0; c < BOARD_W; c++)
        if (c != (r * 3) % BOARD_W) g_board.board[r][c] = 1 + (r + c) % NUM_PIECES;
g_state.score = 123456;
