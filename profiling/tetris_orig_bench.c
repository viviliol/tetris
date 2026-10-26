#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ──────────────────────────────────────────
   Константы
────────────────────────────────────────── */
#define BOARD_W   10
#define BOARD_H   20
#define NUM_PIECES   7
#define NUM_ROTATIONS 4
#define CELLS_PER_PIECE 4

/* Размер одной клетки в пикселях */
#define CELL_PX   36

/* Размеры окна */
#define WIN_W  (BOARD_W * CELL_PX + 200)
#define WIN_H  (BOARD_H * CELL_PX + 40)

/* Смещение поля */
#define BOARD_OX  20
#define BOARD_OY  20

/* ──────────────────────────────────────────
   Фигуры (тетромино) — без изменений
────────────────────────────────────────── */
typedef struct { int r, c; } Cell;

static const Cell PIECES[NUM_PIECES][NUM_ROTATIONS][CELLS_PER_PIECE] = {
    /* I */
    {{{0,0},{1,0},{2,0},{3,0}},{{0,0},{0,1},{0,2},{0,3}},{{0,0},{1,0},{2,0},{3,0}},{{0,0},{0,1},{0,2},{0,3}}},
    /* O */
    {{{0,0},{0,1},{1,0},{1,1}},{{0,0},{0,1},{1,0},{1,1}},{{0,0},{0,1},{1,0},{1,1}},{{0,0},{0,1},{1,0},{1,1}}},
    /* T */
    {{{0,1},{1,0},{1,1},{1,2}},{{0,0},{1,0},{1,1},{2,0}},{{0,0},{0,1},{0,2},{1,1}},{{0,1},{1,0},{1,1},{2,1}}},
    /* S */
    {{{0,1},{0,2},{1,0},{1,1}},{{0,0},{1,0},{1,1},{2,1}},{{0,1},{0,2},{1,0},{1,1}},{{0,0},{1,0},{1,1},{2,1}}},
    /* Z */
    {{{0,0},{0,1},{1,1},{1,2}},{{0,1},{1,0},{1,1},{2,0}},{{0,0},{0,1},{1,1},{1,2}},{{0,1},{1,0},{1,1},{2,0}}},
    /* J */
    {{{0,0},{1,0},{1,1},{1,2}},{{0,0},{0,1},{1,0},{2,0}},{{0,0},{0,1},{0,2},{1,2}},{{0,2},{1,2},{2,1},{2,2}}},
    /* L */
    {{{0,2},{1,0},{1,1},{1,2}},{{0,0},{1,0},{2,0},{2,1}},{{0,0},{0,1},{0,2},{1,0}},{{0,0},{0,1},{1,1},{2,1}}},
};

/* Цвета RGB для каждой фигуры */
static const float PIECE_COLORS[NUM_PIECES][3] = {
    {0.0f, 0.9f, 0.9f},  /* I — cyan    */
    {0.9f, 0.9f, 0.0f},  /* O — yellow  */
    {0.8f, 0.0f, 0.8f},  /* T — magenta */
    {0.0f, 0.8f, 0.0f},  /* S — green   */
    {0.9f, 0.0f, 0.0f},  /* Z — red     */
    {0.2f, 0.2f, 0.9f},  /* J — blue    */
    {0.9f, 0.5f, 0.0f},  /* L — orange  */
};

/* ──────────────────────────────────────────
   Игровые структуры — без изменений
────────────────────────────────────────── */
typedef struct { int board[BOARD_H][BOARD_W]; } Board;

typedef struct {
    int type, rot, row, col;
} Piece;

typedef struct {
    int score, level, lines_cleared;
    time_t start_time, end_time;
    int game_over, paused;
} GameState;

static Board     g_board;
static Piece     g_cur, g_next;
static GameState g_state;

/* ──────────────────────────────────────────
   Игровая логика — без изменений
────────────────────────────────────────── */
static void piece_cells(const Piece *p, Cell out[CELLS_PER_PIECE]) {
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        out[i].r = p->row + PIECES[p->type][p->rot][i].r;
        out[i].c = p->col + PIECES[p->type][p->rot][i].c;
    }
}

static int piece_fits(const Piece *p) {
    Cell cells[CELLS_PER_PIECE];
    piece_cells(p, cells);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int r = cells[i].r, c = cells[i].c;
        if (c < 0 || c >= BOARD_W) return 0;
        if (r >= BOARD_H)          return 0;
        if (r >= 0 && g_board.board[r][c]) return 0;
    }
    return 1;
}

static Piece new_piece(void) {
    Piece p;
    p.type = rand() % NUM_PIECES;
    p.rot  = 0;
    p.row  = 0;
    p.col  = BOARD_W / 2 - 1;
    return p;
}

static void lock_piece(void) {
    Cell cells[CELLS_PER_PIECE];
    piece_cells(&g_cur, cells);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int r = cells[i].r, c = cells[i].c;
        if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W)
            g_board.board[r][c] = g_cur.type + 1;
    }
}

static int clear_lines(void) {
    int cleared = 0;
    for (int r = BOARD_H - 1; r >= 0; r--) {
        int full = 1;
        for (int c = 0; c < BOARD_W; c++)
            if (!g_board.board[r][c]) { full = 0; break; }
        if (full) {
            for (int rr = r; rr > 0; rr--)
                memcpy(g_board.board[rr], g_board.board[rr-1], sizeof(g_board.board[0]));
            memset(g_board.board[0], 0, sizeof(g_board.board[0]));
            r++;
            cleared++;
        }
    }
    return cleared;
}

static void add_score(int lines) {
    static const int pts[5] = {0, 100, 300, 500, 800};
    if (lines > 4) lines = 4;
    g_state.score += pts[lines] * (g_state.level + 1);
    g_state.lines_cleared += lines;
    g_state.level = g_state.lines_cleared / 10;
}

static int fall_delay_ms(void) {
    /* Каждый уровень ~25% быстрее предыдущего, минимум 80 мс */
    int d = (int)(500.0f * 0.75f * g_state.level);
    if (d > 500) d = 500;
    d = 500 - d;
    return d < 80 ? 80 : d;
}

static void spawn_next(void) {
    g_cur  = g_next;
    g_next = new_piece();
    if (!piece_fits(&g_cur)) {
        g_state.game_over = 1;
        g_state.end_time  = time(NULL);
    }
}

static void hard_drop(void) {
    while (1) {
        Piece tmp = g_cur; tmp.row++;
        if (!piece_fits(&tmp)) break;
        g_cur = tmp;
        g_state.score += 2;
    }
    lock_piece();
    add_score(clear_lines());
    spawn_next();
}

/* ──────────────────────────────────────────
   OpenGL: вспомогательные функции рисования
────────────────────────────────────────── */

/* Перевести координаты пикселей (y вниз) в OpenGL NDC */
static void px_to_ndc(float px, float py, float *nx, float *ny) {
    *nx =  2.0f * px / WIN_W - 1.0f;
    *ny = -2.0f * py / WIN_H + 1.0f;
}

/* Закрашенный прямоугольник */
static void fill_rect(float x, float y, float w, float h,
                       float r, float g, float b, float a) {
    float x1, y1, x2, y2;
    px_to_ndc(x,     y,     &x1, &y1);
    px_to_ndc(x + w, y + h, &x2, &y2);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
        glVertex2f(x1, y1);
        glVertex2f(x2, y1);
        glVertex2f(x2, y2);
        glVertex2f(x1, y2);
    glEnd();
}

/* Обводка прямоугольника */
static void stroke_rect(float x, float y, float w, float h,
                         float r, float g, float b) {
    float x1, y1, x2, y2;
    px_to_ndc(x,     y,     &x1, &y1);
    px_to_ndc(x + w, y + h, &x2, &y2);
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x1, y1);
        glVertex2f(x2, y1);
        glVertex2f(x2, y2);
        glVertex2f(x1, y2);
    glEnd();
}

/* Одна клетка тетриса */
static void draw_cell_gl(int row, int col,
                          float r, float g, float b, float alpha) {
    float x = BOARD_OX + col * CELL_PX;
    float y = BOARD_OY + row * CELL_PX;
    float s = CELL_PX - 1.0f;

    /* Заливка */
    fill_rect(x, y, s, s, r, g, b, alpha);

    /* Светлая грань (сверху/слева) */
    fill_rect(x, y, s, 3, r*1.4f>1?1:r*1.4f, g*1.4f>1?1:g*1.4f, b*1.4f>1?1:b*1.4f, alpha);
    fill_rect(x, y, 3, s, r*1.4f>1?1:r*1.4f, g*1.4f>1?1:g*1.4f, b*1.4f>1?1:b*1.4f, alpha);

    /* Тёмная грань (снизу/справа) */
    fill_rect(x, y+s-3, s, 3, r*0.5f, g*0.5f, b*0.5f, alpha);
    fill_rect(x+s-3, y, 3, s, r*0.5f, g*0.5f, b*0.5f, alpha);
}

/* Простой 5×7 растровый шрифт для цифр и букв A-Z + ':' */
static const unsigned char FONT5X7[95][7] = {
    /* ASCII 32 (' ') .. 126 ('~'), 5 бит на строку */
#define _ 0
    {_,_,_,_,_,_,_},  /* space */
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* # */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* $ */
    {0x18,0x19,0x02,0x04,0x13,0x03,0x00}, /* % */
    {0x08,0x14,0x14,0x08,0x15,0x12,0x0D}, /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, /* . */
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* 2 */
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x04,0x00,0x00,0x04,0x00,0x00}, /* : */
    {0x00,0x04,0x00,0x00,0x04,0x08,0x00}, /* ; */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* ? */
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* @ */
    {0x04,0x0A,0x11,0x11,0x1F,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x09,0x09,0x09,0x09,0x09,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x06,0x04,0x04,0x04,0x04,0x04,0x06}, /* [ */
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, /* \ */
    {0x06,0x02,0x02,0x02,0x02,0x02,0x06}, /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* _ */
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
    /* строчные совпадают с заглавными для простоты */
    {0x04,0x0A,0x11,0x11,0x1F,0x11,0x11}, /* a=A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* b=B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* c=C */
    {0x1E,0x09,0x09,0x09,0x09,0x09,0x1E}, /* d=D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* e=E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* f=F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, /* g=G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* h=H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* i=I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* j=J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* k=K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* l=L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* m=M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* n=N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* o=O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* p=P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* q=Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* r=R */
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* s=S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* t=T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* u=U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* v=V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* w=W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* x=X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* y=Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* z=Z */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, /* { */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, /* | */
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, /* } */
    {0x08,0x15,0x02,0x00,0x00,0x00,0x00}, /* ~ */
#undef _
};

/* Нарисовать текст масштабом scale (пиксели на точку = scale) */
static void draw_text(const char *s, float px, float py,
                       float scale,
                       float r, float g, float b) {
    float cx = px;
    for (; *s; s++) {
        int idx = (unsigned char)*s - 32;
        if (idx < 0 || idx >= 95) { cx += 6 * scale; continue; }
        const unsigned char *glyph = FONT5X7[idx];
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (0x10 >> col)) {
                    fill_rect(cx + col * scale,
                              py  + row * scale,
                              scale, scale,
                              r, g, b, 1.0f);
                }
            }
        }
        cx += 6 * scale;
    }
}

/* ──────────────────────────────────────────
   Отрисовка игры
────────────────────────────────────────── */
static void draw_board_gl(void) {
    /* Фон поля */
    fill_rect(BOARD_OX, BOARD_OY,
              BOARD_W * CELL_PX, BOARD_H * CELL_PX,
              0.05f, 0.05f, 0.05f, 1.0f);

    /* Сетка */
    for (int r = 0; r < BOARD_H; r++)
        for (int c = 0; c < BOARD_W; c++) {
            stroke_rect(BOARD_OX + c * CELL_PX,
                        BOARD_OY + r * CELL_PX,
                        CELL_PX - 1, CELL_PX - 1,
                        0.15f, 0.15f, 0.15f);
        }

    /* Зафиксированные клетки */
    for (int r = 0; r < BOARD_H; r++)
        for (int c = 0; c < BOARD_W; c++) {
            int t = g_board.board[r][c];
            if (t) {
                const float *col = PIECE_COLORS[t - 1];
                draw_cell_gl(r, c, col[0], col[1], col[2], 1.0f);
            }
        }

    /* Рамка поля */
    stroke_rect(BOARD_OX - 1, BOARD_OY - 1,
                BOARD_W * CELL_PX + 2, BOARD_H * CELL_PX + 2,
                0.5f, 0.5f, 0.5f);
}

static void draw_ghost_gl(void) {
    Piece ghost = g_cur;
    while (1) {
        Piece tmp = ghost; tmp.row++;
        if (!piece_fits(&tmp)) break;
        ghost = tmp;
    }
    Cell cells[CELLS_PER_PIECE];
    piece_cells(&ghost, cells);
    const float *col = PIECE_COLORS[g_cur.type];
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int r = cells[i].r, c = cells[i].c;
        if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W
                && !g_board.board[r][c]) {
            float x = BOARD_OX + c * CELL_PX;
            float y = BOARD_OY + r * CELL_PX;
            float s = CELL_PX - 1.0f;
            fill_rect(x, y, s, s, col[0]*0.25f, col[1]*0.25f, col[2]*0.25f, 1.0f);
            stroke_rect(x, y, s, s, col[0]*0.6f, col[1]*0.6f, col[2]*0.6f);
        }
    }
}

static void draw_piece_gl(const Piece *p) {
    Cell cells[CELLS_PER_PIECE];
    piece_cells(p, cells);
    const float *col = PIECE_COLORS[p->type];
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int r = cells[i].r, c = cells[i].c;
        if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W)
            draw_cell_gl(r, c, col[0], col[1], col[2], 1.0f);
    }
}

static void draw_panel_gl(void) {
    float px = BOARD_OX + BOARD_W * CELL_PX + 16;
    float py = BOARD_OY;
    float pw = WIN_W - px - 8;

    /* Фон панели */
    fill_rect(px, py, pw, WIN_H - 2*BOARD_OY, 0.08f, 0.08f, 0.12f, 1.0f);
    stroke_rect(px, py, pw, WIN_H - 2*BOARD_OY, 0.4f, 0.4f, 0.6f);

    float scale = 2.0f;
    float tx = px + 6, ty = py + 6;
    float lh = 18;

    draw_text("TETRIS", tx, ty, scale, 0.9f, 0.8f, 0.0f); ty += lh + 4;

    /* Время — замораживаем при game over */
    time_t ref     = (g_state.end_time ? g_state.end_time : time(NULL));
    time_t elapsed = ref - g_state.start_time;
    int mm = (int)(elapsed / 60), ss = (int)(elapsed % 60);
    char buf[32];
    draw_text("TIME",  tx, ty, scale, 0.7f, 0.7f, 0.7f); ty += lh;
    snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
    draw_text(buf, tx, ty, scale, 1.0f, 1.0f, 1.0f);     ty += lh + 6;

    draw_text("SCORE", tx, ty, scale, 0.7f, 0.7f, 0.7f); ty += lh;
    snprintf(buf, sizeof(buf), "%d", g_state.score);
    draw_text(buf, tx, ty, scale, 1.0f, 1.0f, 0.3f);     ty += lh + 6;

    draw_text("LEVEL", tx, ty, scale, 0.7f, 0.7f, 0.7f); ty += lh;
    snprintf(buf, sizeof(buf), "%d", g_state.level);
    draw_text(buf, tx, ty, scale, 0.3f, 1.0f, 0.5f);     ty += lh + 6;

    draw_text("LINES", tx, ty, scale, 0.7f, 0.7f, 0.7f); ty += lh;
    snprintf(buf, sizeof(buf), "%d", g_state.lines_cleared);
    draw_text(buf, tx, ty, scale, 0.5f, 0.8f, 1.0f);     ty += lh + 10;

    /* Превью следующей фигуры */
    draw_text("NEXT",  tx, ty, scale, 0.7f, 0.7f, 0.7f); ty += lh + 4;

    /* Маленькие клетки превью (масштаб CELL_PX/2) */
    int prev_sz = CELL_PX / 2;
    float preview_ox = tx;
    float preview_oy = ty;
    const Cell (*shape)[CELLS_PER_PIECE] = PIECES[g_next.type];
    const float *col = PIECE_COLORS[g_next.type];
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int pr = shape[0][i].r;
        int pc = shape[0][i].c;
        float cx = preview_ox + pc * prev_sz;
        float cy = preview_oy + pr * prev_sz;
        fill_rect(cx, cy, prev_sz-1, prev_sz-1, col[0], col[1], col[2], 1.0f);
    }
    ty += 3 * prev_sz + 12;

    /* Управление */
    draw_text("CONTROLS", tx, ty, scale, 0.6f, 0.6f, 0.6f); ty += lh + 4;
    float cs = 1.5f;
    float ch = 14;
    draw_text("LFT/RGT move",   tx, ty, cs, 0.6f,0.6f,0.6f); ty += ch;
    draw_text("UP    rotate",   tx, ty, cs, 0.6f,0.6f,0.6f); ty += ch;
    draw_text("DWN   soft drop",tx, ty, cs, 0.6f,0.6f,0.6f); ty += ch;
    draw_text("SPC   hard drop",tx, ty, cs, 0.6f,0.6f,0.6f); ty += ch;
    draw_text("P     pause",    tx, ty, cs, 0.6f,0.6f,0.6f); ty += ch;
    draw_text("Q     quit",     tx, ty, cs, 0.6f,0.6f,0.6f);
}

static void draw_all_gl(void) {
    glClear(GL_COLOR_BUFFER_BIT);
    draw_board_gl();
    draw_ghost_gl();
    draw_piece_gl(&g_cur);
    draw_panel_gl();

    if (g_state.paused) {
        float cx = BOARD_OX + (BOARD_W * CELL_PX) / 2.0f - 30;
        float cy = BOARD_OY + (BOARD_H * CELL_PX) / 2.0f - 10;
        fill_rect(cx-4, cy-4, 76, 20, 0.0f, 0.0f, 0.0f, 0.8f);
        draw_text("PAUSED", cx, cy, 2.5f, 1.0f, 0.8f, 0.0f);
    }
}

static void draw_game_over_gl(GLFWwindow *win) {
    while (!glfwWindowShouldClose(win)) {
        glClear(GL_COLOR_BUFFER_BIT);
        draw_board_gl();
        draw_panel_gl();

        /* Окно Game Over: фиксированный размер, точно по центру поля */
        float bw = 148.0f, bh = 78.0f;
        float bx = BOARD_OX + (BOARD_W * CELL_PX - bw) / 2.0f;
        float by = BOARD_OY + (BOARD_H * CELL_PX - bh) / 2.0f;
        fill_rect(bx, by, bw, bh, 0.0f, 0.0f, 0.0f, 0.90f);
        stroke_rect(bx, by, bw, bh, 0.7f, 0.1f, 0.1f);
        /* строки: выравниваем по центру ящика */
        float scale1 = 2.2f, scale2 = 2.0f, scale3 = 1.8f;
        char buf[32];
        snprintf(buf, sizeof(buf), "SCORE %d", g_state.score);
        /* ширина текста: len * 6 * scale */
        float w1 = 9 * 6 * scale1, w2 = (float)strlen(buf) * 6 * scale2, w3 = 7 * 6 * scale3;
        float cx, cy = by + 10;
        cx = bx + (bw - w1) / 2.0f;
        draw_text("GAME OVER", cx, cy, scale1, 1.0f, 0.2f, 0.2f); cy += 24;
        cx = bx + (bw - w2) / 2.0f;
        draw_text(buf,         cx, cy, scale2, 1.0f, 1.0f, 0.3f); cy += 22;
        cx = bx + (bw - w3) / 2.0f;
        draw_text("PRESS Q",   cx, cy, scale3, 0.7f, 0.7f, 0.7f);

        glfwSwapBuffers(win);
        glfwPollEvents();

        /* Закрыть по Q или Escape */
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS ||
            glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            break;
    }
}

/* ──────────────────────────────────────────
   Обработка клавиатуры (GLFW callback)
────────────────────────────────────────── */
static void key_callback(GLFWwindow *win, int key, int scancode,
                          int action, int mods) {
    (void)scancode; (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_Q || key == GLFW_KEY_ESCAPE) {
        g_state.game_over = 1; return;
    }
    if (key == GLFW_KEY_P) { g_state.paused = !g_state.paused; return; }
    if (g_state.paused) return;

    Piece tmp = g_cur;
    switch (key) {
        case GLFW_KEY_LEFT:
            tmp.col--; if (piece_fits(&tmp)) g_cur = tmp; break;
        case GLFW_KEY_RIGHT:
            tmp.col++; if (piece_fits(&tmp)) g_cur = tmp; break;
        case GLFW_KEY_DOWN:
            tmp.row++;
            if (piece_fits(&tmp)) { g_cur = tmp; g_state.score++; } break;
        case GLFW_KEY_UP:
            tmp.rot = (tmp.rot + 1) % NUM_ROTATIONS;
            if (!piece_fits(&tmp)) tmp.col--;
            if (!piece_fits(&tmp)) tmp.col += 2;
            if (!piece_fits(&tmp)) tmp.col--;
            if (piece_fits(&tmp)) g_cur = tmp;
            break;
        case GLFW_KEY_SPACE:
            hard_drop(); break;
    }
}

/* ──────────────────────────────────────────
   main
────────────────────────────────────────── */
int main(void) {
#ifdef BENCH
    srand(12345);
#else
    srand((unsigned)time(NULL));
#endif

    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }

    GLFWwindow *win = glfwCreateWindow(WIN_W, WIN_H, "Tetris", NULL, NULL);
    if (!win) { glfwTerminate(); fprintf(stderr, "window creation failed\n"); return 1; }

    glfwMakeContextCurrent(win);
    glfwSetKeyCallback(win, key_callback);
    glfwSwapInterval(1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);

    /* Инициализация игры */
    memset(&g_board, 0, sizeof(g_board));
    memset(&g_state, 0, sizeof(g_state));
    g_state.start_time = time(NULL);
    g_state.level = 1;
    g_next = new_piece();
    spawn_next();
#if BENCH == 1
#include "bench_fill.inc"
#endif

    double last = glfwGetTime();

    while (!glfwWindowShouldClose(win) && !g_state.game_over) {
        glfwPollEvents();

        double now = glfwGetTime();
        double elapsed_ms = (now - last) * 1000.0;

#if BENCH == 1
        (void)elapsed_ms;
        if (0) {
#else
        if (!g_state.paused && elapsed_ms >= fall_delay_ms()) {
#endif
            last = now;
            Piece tmp = g_cur; tmp.row++;
            if (piece_fits(&tmp)) {
                g_cur = tmp;
            } else {
                lock_piece();
                add_score(clear_lines());
                spawn_next();
            }
        }

        draw_all_gl();
        glfwSwapBuffers(win);
    }

    if (!glfwWindowShouldClose(win))
        draw_game_over_gl(win);

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
