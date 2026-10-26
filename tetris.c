#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>
#include <stddef.h>
#include <stdint.h>
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

/* Ёмкость буфера вершин (в прямоугольниках). Замер: статический слой —
   511 прямоугольников, динамический — до ~900 (поле заполнено наполовину
   + Game Over); полностью забитое поле даёт ~1150. Буфер общий для слоёв. */
#define MAX_QUADS 1280

/* ──────────────────────────────────────────
   Фигуры (тетромино)
────────────────────────────────────────── */
typedef struct { int8_t r, c; } Cell;

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

/* Цвет вершины: 4 байта вместо 3–4 float */
typedef struct { uint8_t r, g, b, a; } Color;

#define RGBA(r, g, b, a) ((Color){ (uint8_t)((r) * 255.0f + 0.5f), (uint8_t)((g) * 255.0f + 0.5f), \
                                   (uint8_t)((b) * 255.0f + 0.5f), (uint8_t)((a) * 255.0f + 0.5f) })
#define RGB(r, g, b) RGBA(r, g, b, 1.0f)

/* Базовые цвета фигур */
static const float PIECE_COLORS[NUM_PIECES][3] = {
    {0.0f, 0.9f, 0.9f},  /* I — cyan    */
    {0.9f, 0.9f, 0.0f},  /* O — yellow  */
    {0.8f, 0.0f, 0.8f},  /* T — magenta */
    {0.0f, 0.8f, 0.0f},  /* S — green   */
    {0.9f, 0.0f, 0.0f},  /* Z — red     */
    {0.2f, 0.2f, 0.9f},  /* J — blue    */
    {0.9f, 0.5f, 0.0f},  /* L — orange  */
};

/* Палитра клетки, посчитанная один раз при старте (раньше — 12 умножений
   и 6 сравнений на каждую клетку в каждом кадре) */
typedef struct { Color base, light, dark, ghost_fill, ghost_edge; } CellPalette;
static CellPalette g_pal[NUM_PIECES];

static float clamp1(float v) { return v > 1.0f ? 1.0f : v; }

static void init_palette(void) {
    for (int i = 0; i < NUM_PIECES; i++) {
        float r = PIECE_COLORS[i][0], g = PIECE_COLORS[i][1], b = PIECE_COLORS[i][2];
        g_pal[i].base       = RGB(r, g, b);
        g_pal[i].light      = RGB(clamp1(r * 1.4f + 0.15f), clamp1(g * 1.4f + 0.15f), clamp1(b * 1.4f + 0.15f));
        g_pal[i].dark       = RGB(r * 0.5f, g * 0.5f, b * 0.5f);
        g_pal[i].ghost_fill = RGBA(r, g, b, 0.18f);
        g_pal[i].ghost_edge = RGBA(r, g, b, 0.75f);
    }
}

/* ──────────────────────────────────────────
   Игровые структуры
────────────────────────────────────────── */
typedef struct {
    uint8_t board[BOARD_H][BOARD_W];  /* 0 — пусто, 1..7 — тип фигуры + 1 */
    uint8_t row_count[BOARD_H];       /* число занятых клеток в ряду */
} Board;

typedef struct {
    int type, rot, row, col;
} Piece;

typedef struct {
    int score, level, lines_cleared;
    double play_time;                 /* секунды игры без учёта паузы */
    double fall_delay;                /* секунды между шагами гравитации */
    int game_over, paused, quit;
} GameState;

static Board     g_board;
static Piece     g_cur, g_next;
static GameState g_state;

/* Нужно ли перерисовать кадр (меняется при любом изменении состояния) */
static int g_dirty = 1;

/* ──────────────────────────────────────────
   Игровая логика
────────────────────────────────────────── */
static int piece_fits(const Piece *p) {
    const Cell *shape = PIECES[p->type][p->rot];
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int r = p->row + shape[i].r, c = p->col + shape[i].c;
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
    const Cell *shape = PIECES[g_cur.type][g_cur.rot];
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int r = g_cur.row + shape[i].r, c = g_cur.col + shape[i].c;
        if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W && !g_board.board[r][c]) {
            g_board.board[r][c] = (uint8_t)(g_cur.type + 1);
            g_board.row_count[r]++;
        }
    }
}

/* Удаление заполненных рядов за один проход снизу вверх: каждый
   незаполненный ряд копируется сразу на своё итоговое место.
   Полнота ряда проверяется за O(1) по счётчику row_count. */
static int clear_lines(void) {
    int dst = BOARD_H - 1;
    for (int src = BOARD_H - 1; src >= 0; src--) {
        if (g_board.row_count[src] == BOARD_W) continue;
        if (dst != src) {
            memcpy(g_board.board[dst], g_board.board[src], BOARD_W);
            g_board.row_count[dst] = g_board.row_count[src];
        }
        dst--;
    }
    int cleared = dst + 1;
    if (cleared) {
        memset(g_board.board, 0, (size_t)cleared * BOARD_W);
        memset(g_board.row_count, 0, (size_t)cleared);
    }
    return cleared;
}

/* Задержка падения: каждый уровень на 25% быстрее, минимум 80 мс.
   Считается только при смене уровня, а не в каждом кадре. */
static void update_fall_delay(void) {
    double d = 0.5;
    for (int i = 1; i < g_state.level && d > 0.08; i++) d *= 0.75;
    g_state.fall_delay = d < 0.08 ? 0.08 : d;
#if BENCH == 2
    g_state.fall_delay = 0.125;   /* та же скорость, что у исходной версии на 1 уровне */
#endif
}

static void add_score(int lines) {
    static const int pts[5] = {0, 100, 300, 500, 800};
    if (lines > 4) lines = 4;
    g_state.score += pts[lines] * g_state.level;
    g_state.lines_cleared += lines;
    int level = 1 + g_state.lines_cleared / 10;
    if (level != g_state.level) {
        g_state.level = level;
        update_fall_delay();
    }
}

static void spawn_next(void) {
    g_cur  = g_next;
    g_next = new_piece();
    if (!piece_fits(&g_cur)) g_state.game_over = 1;
}

static void land_piece(void) {
    lock_piece();
    add_score(clear_lines());
    spawn_next();
}

static void hard_drop(void) {
    while (1) {
        Piece tmp = g_cur; tmp.row++;
        if (!piece_fits(&tmp)) break;
        g_cur = tmp;
        g_state.score += 2;
    }
    land_piece();
}

static void reset_game(void) {
    memset(&g_board, 0, sizeof(g_board));
    memset(&g_state, 0, sizeof(g_state));
    g_state.level = 1;
    update_fall_delay();
    g_next = new_piece();
    spawn_next();
    g_dirty = 1;
}

/* ──────────────────────────────────────────
   Пакетная отрисовка
   Вместо glBegin/glEnd на каждый прямоугольник все прямоугольники
   складываются в массив вершин и рисуются одним glDrawElements.
   Слоёв два:
     static  — фон, сетка, рамки, подписи: строится один раз, лежит в VBO;
     dynamic — фигуры, числа, оверлеи: перестраивается только когда
               состояние игры изменилось (g_dirty).
────────────────────────────────────────── */
typedef struct { float x, y; Color c; } Vertex;   /* 12 байт */

static Vertex g_verts[MAX_QUADS * 4];   /* общий буфер сборки для обоих слоёв */
static int    g_nquads;

static GLuint g_vbo_static, g_vbo_dynamic, g_ibo;
static int    g_static_quads, g_dynamic_quads;

/* Функции GL 1.5 берём через GLFW: opengl32.dll в Windows их не экспортирует */
static PFNGLGENBUFFERSPROC    pglGenBuffers;
static PFNGLBINDBUFFERPROC    pglBindBuffer;
static PFNGLBUFFERDATAPROC    pglBufferData;
static PFNGLDELETEBUFFERSPROC pglDeleteBuffers;

static int load_gl(void) {
    pglGenBuffers    = (PFNGLGENBUFFERSPROC)glfwGetProcAddress("glGenBuffers");
    pglBindBuffer    = (PFNGLBINDBUFFERPROC)glfwGetProcAddress("glBindBuffer");
    pglBufferData    = (PFNGLBUFFERDATAPROC)glfwGetProcAddress("glBufferData");
    pglDeleteBuffers = (PFNGLDELETEBUFFERSPROC)glfwGetProcAddress("glDeleteBuffers");
    return pglGenBuffers && pglBindBuffer && pglBufferData && pglDeleteBuffers;
}

/* Прямоугольник с вертикальным градиентом (top → bottom) */
static void push_quad(float x, float y, float w, float h, Color top, Color bottom) {
    if (g_nquads >= MAX_QUADS) return;
    Vertex *v = &g_verts[g_nquads++ * 4];
    v[0] = (Vertex){ x,     y,     top    };
    v[1] = (Vertex){ x + w, y,     top    };
    v[2] = (Vertex){ x + w, y + h, bottom };
    v[3] = (Vertex){ x,     y + h, bottom };
}

static void push_rect(float x, float y, float w, float h, Color c) {
    push_quad(x, y, w, h, c, c);
}

/* Рамка толщиной t пикселей (4 прямоугольника, рисуется в том же пакете) */
static void push_frame(float x, float y, float w, float h, float t, Color c) {
    push_rect(x,         y,         w, t, c);
    push_rect(x,         y + h - t, w, t, c);
    push_rect(x,         y + t,     t, h - 2 * t, c);
    push_rect(x + w - t, y + t,     t, h - 2 * t, c);
}

/* Клетка с объёмной гранью: заливка + светлый верх/лево + тёмный низ/право */
static void push_cell(float x, float y, float s, const CellPalette *p) {
    float e = s >= 24 ? 3.0f : 2.0f;
    push_quad(x, y, s, s, p->light, p->base);
    push_rect(x, y, s, e, p->light);
    push_rect(x, y + e, e, s - e, p->light);
    push_rect(x, y + s - e, s, e, p->dark);
    push_rect(x + s - e, y + e, e, s - 2 * e, p->dark);
}

static void upload_layer(GLuint vbo, int nquads) {
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    /* glBufferData с новым размером «отвязывает» старое хранилище, и драйверу
       не нужно ждать, пока GPU дорисует предыдущий кадр */
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nquads * 4 * sizeof(Vertex), g_verts,
                  vbo == g_vbo_static ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
}

static void draw_layer(GLuint vbo, int nquads) {
    if (!nquads) return;
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexPointer(2, GL_FLOAT, sizeof(Vertex), (const void *)offsetof(Vertex, x));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), (const void *)offsetof(Vertex, c));
    glDrawElements(GL_TRIANGLES, nquads * 6, GL_UNSIGNED_SHORT, NULL);
}

/* Общий индексный буфер: каждый прямоугольник — 4 вершины и 2 треугольника
   (0,1,2)(0,2,3). Строится один раз; 4 вершины вместо 6 на прямоугольник. */
static int init_buffers(void) {
    if (!load_gl()) return 0;
    GLushort *idx = malloc(MAX_QUADS * 6 * sizeof(GLushort));
    if (!idx) return 0;
    for (int q = 0; q < MAX_QUADS; q++) {
        GLushort b = (GLushort)(q * 4);
        GLushort *o = &idx[q * 6];
        o[0] = b; o[1] = b + 1; o[2] = b + 2;
        o[3] = b; o[4] = b + 2; o[5] = b + 3;
    }
    pglGenBuffers(1, &g_ibo);
    pglGenBuffers(1, &g_vbo_static);
    pglGenBuffers(1, &g_vbo_dynamic);
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    pglBufferData(GL_ELEMENT_ARRAY_BUFFER, MAX_QUADS * 6 * sizeof(GLushort), idx, GL_STATIC_DRAW);
    free(idx);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    return 1;
}

/* ──────────────────────────────────────────
   Растровый шрифт 5×7: ASCII 32 (' ') .. 96 ('`').
   Строчные буквы отображаются на заглавные при выводе, поэтому их
   копии из таблицы убраны.
────────────────────────────────────────── */
#define FONT_FIRST 32
#define FONT_COUNT 65

static const uint8_t FONT5X7[FONT_COUNT][7] = {
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
#undef _
};

/* Каждый глиф заранее разбивается на минимум прямоугольников: соседние
   точки в строке объединяются в отрезок, а одинаковые отрезки в соседних
   строках — в один высокий прямоугольник. Например, «1» — 4 прямоугольника
   вместо 12 точек. */
typedef struct { uint8_t x, y, w, h; } GlyphRect;
#define MAX_GLYPH_RECTS 12   /* максимум по таблице — 10 */
static GlyphRect g_glyph_rects[FONT_COUNT][MAX_GLYPH_RECTS];
static uint8_t   g_glyph_nrects[FONT_COUNT];

static void init_font(void) {
    for (int g = 0; g < FONT_COUNT; g++) {
        uint8_t rows[7];
        memcpy(rows, FONT5X7[g], 7);
        int n = 0;
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 5; x++) {
                uint8_t bit = (uint8_t)(0x10 >> x);
                if (!(rows[y] & bit)) continue;
                /* отрезок вправо */
                uint8_t mask = 0;
                int w = 0;
                while (x + w < 5 && (rows[y] & (0x10 >> (x + w)))) mask |= (uint8_t)(0x10 >> (x + w++));
                /* тянем вниз, пока строка ниже содержит весь отрезок */
                int h = 1;
                while (y + h < 7 && (rows[y + h] & mask) == mask) h++;
                for (int k = 0; k < h; k++) rows[y + k] &= (uint8_t)~mask;
                if (n < MAX_GLYPH_RECTS)
                    g_glyph_rects[g][n++] = (GlyphRect){ (uint8_t)x, (uint8_t)y, (uint8_t)w, (uint8_t)h };
                x += w - 1;
            }
        }
        g_glyph_nrects[g] = (uint8_t)n;
    }
}

static float text_width(const char *s, float scale) {
    size_t n = strlen(s);
    return n ? (n * 6 - 1) * scale : 0.0f;
}

static void push_text(const char *s, float px, float py, float scale, Color c) {
    for (float cx = px; *s; s++, cx += 6 * scale) {
        int ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        int idx = ch - FONT_FIRST;
        if (idx <= 0 || idx >= FONT_COUNT) continue;   /* пробел и неизвестные */
        for (int i = 0; i < g_glyph_nrects[idx]; i++) {
            const GlyphRect *r = &g_glyph_rects[idx][i];
            push_rect(cx + r->x * scale, py + r->y * scale, r->w * scale, r->h * scale, c);
        }
    }
}

/* Текст с тенью на 1 пиксель — для заголовков */
static void push_text_shadow(const char *s, float px, float py, float scale, Color c) {
    push_text(s, px + 1.5f, py + 1.5f, scale, RGBA(0, 0, 0, 0.6f));
    push_text(s, px, py, scale, c);
}

/* ──────────────────────────────────────────
   Раскладка панели
────────────────────────────────────────── */
#define PANEL_X   (BOARD_OX + BOARD_W * CELL_PX + 16)
#define PANEL_Y   BOARD_OY
#define PANEL_W   (WIN_W - PANEL_X - 8)
#define PANEL_H   (WIN_H - 2 * BOARD_OY)
#define TEXT_X    (PANEL_X + 8)
#define LH        18.0f
#define PREVIEW_PX (CELL_PX / 2)

/* y-координаты строк панели: считаются один раз и используются обоими слоями */
static float y_time, y_score, y_level, y_lines, y_next, y_controls;

static void build_static(void) {
    g_nquads = 0;

    /* Фон поля с лёгким вертикальным градиентом */
    push_quad(BOARD_OX, BOARD_OY, BOARD_W * CELL_PX, BOARD_H * CELL_PX,
              RGB(0.07f, 0.07f, 0.11f), RGB(0.03f, 0.03f, 0.05f));

    /* Сетка: 9 + 19 линий в зазорах между клетками (раньше — 200 GL_LINE_LOOP) */
    Color grid = RGB(0.13f, 0.13f, 0.18f);
    for (int c = 1; c < BOARD_W; c++)
        push_rect(BOARD_OX + c * CELL_PX - 1, BOARD_OY, 1, BOARD_H * CELL_PX, grid);
    for (int r = 1; r < BOARD_H; r++)
        push_rect(BOARD_OX, BOARD_OY + r * CELL_PX - 1, BOARD_W * CELL_PX, 1, grid);

    /* Рамка поля */
    push_frame(BOARD_OX - 3, BOARD_OY - 3, BOARD_W * CELL_PX + 5, BOARD_H * CELL_PX + 5, 2,
               RGB(0.45f, 0.45f, 0.6f));

    /* Панель */
    push_quad(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, RGB(0.10f, 0.10f, 0.16f), RGB(0.05f, 0.05f, 0.08f));
    push_frame(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 1, RGB(0.4f, 0.4f, 0.6f));

    float scale = 2.0f, ty = PANEL_Y + 8;
    Color label = RGB(0.6f, 0.6f, 0.68f);

    push_text_shadow("TETRIS", PANEL_X + (PANEL_W - text_width("TETRIS", 3.0f)) / 2, ty, 3.0f,
                     RGB(0.95f, 0.8f, 0.1f));
    ty += 21 + 8;
    push_rect(TEXT_X, ty, PANEL_W - 16, 1, RGB(0.3f, 0.3f, 0.45f));
    ty += 9;

    push_text("TIME",  TEXT_X, ty, scale, label); y_time  = ty + LH; ty += 2 * LH + 6;
    push_text("SCORE", TEXT_X, ty, scale, label); y_score = ty + LH; ty += 2 * LH + 6;
    push_text("LEVEL", TEXT_X, ty, scale, label); y_level = ty + LH; ty += 2 * LH + 6;
    push_text("LINES", TEXT_X, ty, scale, label); y_lines = ty + LH; ty += 2 * LH + 10;
    push_text("NEXT",  TEXT_X, ty, scale, label); ty += LH + 2;

    /* Рамка превью следующей фигуры */
    y_next = ty;
    push_rect(TEXT_X, y_next, PANEL_W - 16, 4 * PREVIEW_PX + 8, RGBA(0, 0, 0, 0.35f));
    push_frame(TEXT_X, y_next, PANEL_W - 16, 4 * PREVIEW_PX + 8, 1, RGB(0.25f, 0.25f, 0.38f));
    ty += 4 * PREVIEW_PX + 8 + 16;

    /* Управление */
    y_controls = ty;
    push_text("CONTROLS", TEXT_X, ty, scale, label); ty += LH + 4;
    static const char *controls[] = {
        "LFT/RGT move", "UP    rotate", "DWN   soft drop", "SPC   hard drop",
        "P     pause",  "R     restart", "Q     quit",
    };
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++, ty += 14)
        push_text(controls[i], TEXT_X, ty, 1.5f, RGB(0.5f, 0.5f, 0.58f));

    g_static_quads = g_nquads;
    upload_layer(g_vbo_static, g_nquads);
}

/* Поле, затемнённое под оверлей, и рамка с центрованным заголовком */
static void push_overlay_box(float bw, float bh, Color edge, float *bx, float *by) {
    push_rect(BOARD_OX, BOARD_OY, BOARD_W * CELL_PX, BOARD_H * CELL_PX, RGBA(0, 0, 0, 0.55f));
    *bx = BOARD_OX + (BOARD_W * CELL_PX - bw) / 2.0f;
    *by = BOARD_OY + (BOARD_H * CELL_PX - bh) / 2.0f;
    push_rect(*bx, *by, bw, bh, RGBA(0.02f, 0.02f, 0.05f, 0.92f));
    push_frame(*bx, *by, bw, bh, 2, edge);
}

static void push_centered(const char *s, float bx, float bw, float y, float scale, Color c) {
    push_text_shadow(s, bx + (bw - text_width(s, scale)) / 2.0f, y, scale, c);
}

static void build_dynamic(void) {
    g_nquads = 0;

    /* Зафиксированные клетки */
    for (int r = 0; r < BOARD_H; r++) {
        if (!g_board.row_count[r]) continue;   /* пустые ряды пропускаем целиком */
        for (int c = 0; c < BOARD_W; c++) {
            int t = g_board.board[r][c];
            if (t)
                push_cell(BOARD_OX + c * CELL_PX, BOARD_OY + r * CELL_PX, CELL_PX - 1, &g_pal[t - 1]);
        }
    }

    if (!g_state.game_over) {
        /* Тень фигуры (место приземления) */
        Piece ghost = g_cur;
        while (1) {
            Piece tmp = ghost; tmp.row++;
            if (!piece_fits(&tmp)) break;
            ghost = tmp;
        }
        const CellPalette *pal = &g_pal[g_cur.type];
        const Cell *shape = PIECES[g_cur.type][g_cur.rot];
        if (ghost.row != g_cur.row) {
            for (int i = 0; i < CELLS_PER_PIECE; i++) {
                int r = ghost.row + shape[i].r, c = ghost.col + shape[i].c;
                if (r < 0) continue;
                float x = BOARD_OX + c * CELL_PX, y = BOARD_OY + r * CELL_PX, s = CELL_PX - 1;
                push_rect(x, y, s, s, pal->ghost_fill);
                push_frame(x, y, s, s, 1, pal->ghost_edge);
            }
        }

        /* Текущая фигура */
        for (int i = 0; i < CELLS_PER_PIECE; i++) {
            int r = g_cur.row + shape[i].r, c = g_cur.col + shape[i].c;
            if (r >= 0)
                push_cell(BOARD_OX + c * CELL_PX, BOARD_OY + r * CELL_PX, CELL_PX - 1, pal);
        }
    }

    /* Значения на панели */
    char buf[32];
    int secs = (int)g_state.play_time;
    snprintf(buf, sizeof(buf), "%02d:%02d", secs / 60, secs % 60);
    push_text(buf, TEXT_X, y_time, 2.0f, RGB(1.0f, 1.0f, 1.0f));
    snprintf(buf, sizeof(buf), "%d", g_state.score);
    push_text(buf, TEXT_X, y_score, 2.0f, RGB(1.0f, 1.0f, 0.3f));
    snprintf(buf, sizeof(buf), "%d", g_state.level);
    push_text(buf, TEXT_X, y_level, 2.0f, RGB(0.3f, 1.0f, 0.5f));
    snprintf(buf, sizeof(buf), "%d", g_state.lines_cleared);
    push_text(buf, TEXT_X, y_lines, 2.0f, RGB(0.5f, 0.8f, 1.0f));

    /* Превью следующей фигуры — по центру рамки */
    {
        const Cell *shape = PIECES[g_next.type][0];
        int maxr = 0, maxc = 0;
        for (int i = 0; i < CELLS_PER_PIECE; i++) {
            if (shape[i].r > maxr) maxr = shape[i].r;
            if (shape[i].c > maxc) maxc = shape[i].c;
        }
        float box_w = PANEL_W - 16, box_h = 4 * PREVIEW_PX + 8;
        float ox = TEXT_X + (box_w - (maxc + 1) * PREVIEW_PX) / 2.0f;
        float oy = y_next + (box_h - (maxr + 1) * PREVIEW_PX) / 2.0f;
        for (int i = 0; i < CELLS_PER_PIECE; i++)
            push_cell(ox + shape[i].c * PREVIEW_PX, oy + shape[i].r * PREVIEW_PX, PREVIEW_PX - 1,
                      &g_pal[g_next.type]);
    }

    /* Оверлеи */
    float bx, by;
    if (g_state.game_over) {
        float bw = 260, bh = 116;
        push_overlay_box(bw, bh, RGB(0.8f, 0.15f, 0.15f), &bx, &by);
        push_centered("GAME OVER", bx, bw, by + 14, 3.0f, RGB(1.0f, 0.25f, 0.25f));
        snprintf(buf, sizeof(buf), "SCORE %d", g_state.score);
        push_centered(buf, bx, bw, by + 50, 2.0f, RGB(1.0f, 1.0f, 0.3f));
        push_centered("R RESTART   Q QUIT", bx, bw, by + 84, 1.5f, RGB(0.7f, 0.7f, 0.7f));
    } else if (g_state.paused) {
        float bw = 200, bh = 76;
        push_overlay_box(bw, bh, RGB(0.9f, 0.75f, 0.1f), &bx, &by);
        push_centered("PAUSED", bx, bw, by + 14, 3.0f, RGB(1.0f, 0.8f, 0.0f));
        push_centered("P TO RESUME", bx, bw, by + 48, 1.5f, RGB(0.7f, 0.7f, 0.7f));
    }

    g_dynamic_quads = g_nquads;
    upload_layer(g_vbo_dynamic, g_nquads);
}

static void render(void) {
    glClear(GL_COLOR_BUFFER_BIT);
    draw_layer(g_vbo_static, g_static_quads);
    draw_layer(g_vbo_dynamic, g_dynamic_quads);
}

/* ──────────────────────────────────────────
   Обработка окна и клавиатуры (GLFW callbacks)
────────────────────────────────────────── */
static void key_callback(GLFWwindow *win, int key, int scancode,
                         int action, int mods) {
    (void)win; (void)scancode; (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_Q || key == GLFW_KEY_ESCAPE) { g_state.quit = 1; return; }
    if (key == GLFW_KEY_R) { reset_game(); return; }
    if (g_state.game_over) return;
    if (key == GLFW_KEY_P) { g_state.paused = !g_state.paused; g_dirty = 1; return; }
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
        case GLFW_KEY_UP: {
            /* Поворот со сдвигом от стены: 0, -1, +1, -2, +2, -3
               (последние — для I, которой у правой стены нужно 3 клетки) */
            static const int kicks[] = { 0, -1, 1, -2, 2, -3 };
            tmp.rot = (tmp.rot + 1) % NUM_ROTATIONS;
            for (size_t i = 0; i < sizeof(kicks) / sizeof(kicks[0]); i++) {
                Piece k = tmp; k.col += kicks[i];
                if (piece_fits(&k)) { g_cur = k; break; }
            }
            break;
        }
        case GLFW_KEY_SPACE:
            hard_drop(); break;
        default:
            return;
    }
    g_dirty = 1;
}

/* Окно нужно перерисовать (перекрыто другим окном, развёрнуто и т.п.) */
static void refresh_callback(GLFWwindow *win) {
    (void)win;
    g_dirty = 1;
}

/* Проекция в пикселях окна: координаты переводит GPU, а не процессор.
   Viewport — по размеру framebuffer, чтобы на HiDPI картинка не
   занимала четверть окна. */
static void framebuffer_callback(GLFWwindow *win, int w, int h) {
    (void)win;
    glViewport(0, 0, w, h);
    g_dirty = 1;
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

    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow *win = glfwCreateWindow(WIN_W, WIN_H, "Tetris", NULL, NULL);
    if (!win) { glfwTerminate(); fprintf(stderr, "window creation failed\n"); return 1; }

    glfwMakeContextCurrent(win);
    glfwSetKeyCallback(win, key_callback);
    glfwSetWindowRefreshCallback(win, refresh_callback);
    glfwSetFramebufferSizeCallback(win, framebuffer_callback);
    glfwSwapInterval(1);

    if (!init_buffers()) {
        fprintf(stderr, "OpenGL 1.5 buffer objects are not supported\n");
        glfwDestroyWindow(win); glfwTerminate(); return 1;
    }

    int fbw, fbh;
    glfwGetFramebufferSize(win, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WIN_W, WIN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);

    init_palette();
    init_font();
    reset_game();
    build_static();

#if BENCH == 1
#include "bench_fill.inc"
    for (int r = 0; r < BOARD_H; r++) {
        g_board.row_count[r] = 0;
        for (int c = 0; c < BOARD_W; c++) g_board.row_count[r] += g_board.board[r][c] != 0;
    }
#endif

    double prev = glfwGetTime();
    double next_fall = prev + g_state.fall_delay;
    int shown_sec = -1;

    while (!glfwWindowShouldClose(win) && !g_state.quit) {
        /* Ждём событие или ближайший момент, когда картинка должна
           измениться (шаг гравитации / смена секунды на таймере).
           Пока ничего не происходит, процесс спит, а не крутит цикл. */
        int running = !g_state.paused && !g_state.game_over;
#if BENCH == 1
        g_dirty = 1;
        running = 0;
#endif
        if (g_dirty) {
            glfwPollEvents();
        } else if (running) {
            double now = glfwGetTime();
            double to_fall = next_fall - now;
            double to_tick = (shown_sec + 1) - g_state.play_time;
            double t = to_fall < to_tick ? to_fall : to_tick;
            glfwWaitEventsTimeout(t > 0 ? t : 0);
        } else {
            glfwWaitEvents();
        }

        double now = glfwGetTime();
        int was_running = running;
        running = !g_state.paused && !g_state.game_over;
#if BENCH == 1
        running = 0;
#endif
        if (running && was_running) {
            g_state.play_time += now - prev;
            if (now >= next_fall) {
                next_fall = now + g_state.fall_delay;
                Piece tmp = g_cur; tmp.row++;
                if (piece_fits(&tmp)) g_cur = tmp;
                else land_piece();
                g_dirty = 1;
            }
        } else if (running) {
            /* сняли с паузы / начали заново — гравитация с начала интервала */
            next_fall = now + g_state.fall_delay;
        }
        prev = now;

        if ((int)g_state.play_time != shown_sec) {
            shown_sec = (int)g_state.play_time;
            g_dirty = 1;
        }

        if (g_dirty) {
            build_dynamic();
            render();
            glfwSwapBuffers(win);
            g_dirty = 0;
        }
    }

    pglDeleteBuffers(1, &g_vbo_static);
    pglDeleteBuffers(1, &g_vbo_dynamic);
    pglDeleteBuffers(1, &g_ibo);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
