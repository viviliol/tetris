/*
 * glcount.so — LD_PRELOAD-перехватчик для замеров.
 * Не требует изменений в коде игры: подменяет функции OpenGL/GLFW,
 * считает вызовы и время кадра, печатает статистику при выходе.
 *
 * Переменные окружения:
 *   GLCOUNT_NOVSYNC=1   — принудительно glfwSwapInterval(0) (замер пропускной способности)
 *   GLCOUNT_FRAMES=N    — завершить программу после N кадров (после 100 кадров прогрева)
 *   GLCOUNT_SHOT=f.ppm  — сохранить N-й кадр (GLCOUNT_SHOT_FRAME, по умолчанию 1) в PPM и выйти
 *
 * Сборка: gcc -O2 -shared -fPIC -o glcount.so glcount.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <GL/gl.h>

typedef struct GLFWwindow GLFWwindow;

#define WARMUP 100

static unsigned long long n_begin, n_vertex, n_color, n_draw_arrays, n_draw_verts,
                          n_call_list, n_other;
static unsigned long long frames, frames_measured;
static double t_start, t_prev, t_min = 1e9, t_max;
static long   limit = -1;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void print_mem(void) {
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    if (!f) return;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "VmRSS", 5) || !strncmp(line, "VmHWM", 5) ||
            !strncmp(line, "RssAnon", 7) || !strncmp(line, "VmData", 6))
            fputs(line, stderr);
    fclose(f);
}

static void report(void) {
    if (frames_measured == 0) return;
    print_mem();
    double total = t_prev - t_start;
    unsigned long long f = frames ? frames : 1;
    fprintf(stderr,
        "\n=== glcount ===\n"
        "frames total         : %llu (measured %llu)\n"
        "avg frame time       : %.4f ms   (%.0f FPS)\n"
        "min / max frame time : %.4f / %.4f ms\n"
        "per frame: draw calls (glBegin+glDraw*+glCallList) : %.1f\n"
        "per frame: glBegin           : %.1f\n"
        "per frame: glVertex*         : %.1f\n"
        "per frame: glColor*          : %.1f\n"
        "per frame: glDrawArrays/Elems: %.1f (verts/indices %.1f)\n"
        "per frame: glCallList        : %.1f\n"
        "per frame: GL API calls total: %.1f\n",
        frames, frames_measured,
        total / frames_measured * 1000.0, frames_measured / total,
        t_min * 1000.0, t_max * 1000.0,
        (double)(n_begin + n_draw_arrays + n_call_list) / f,
        (double)n_begin / f, (double)n_vertex / f, (double)n_color / f,
        (double)n_draw_arrays / f, (double)n_draw_verts / f,
        (double)n_call_list / f,
        (double)(n_begin * 2 + n_vertex + n_color + n_draw_arrays + n_call_list + n_other) / f);
}

__attribute__((constructor)) static void init(void) {
    const char *e = getenv("GLCOUNT_FRAMES");
    if (e) limit = atol(e);
    atexit(report);
}

#define NEXT(ret, name, args) \
    static ret (*real) args; \
    if (!real) real = (ret (*) args)dlsym(RTLD_NEXT, #name);

/* ---- immediate mode ---- */
void glBegin(GLenum m)            { NEXT(void, glBegin, (GLenum)); n_begin++; real(m); }
void glEnd(void)                  { NEXT(void, glEnd, (void)); real(); }
void glVertex2f(GLfloat x, GLfloat y) { NEXT(void, glVertex2f, (GLfloat, GLfloat)); n_vertex++; real(x, y); }
void glColor3f(GLfloat r, GLfloat g, GLfloat b) { NEXT(void, glColor3f, (GLfloat, GLfloat, GLfloat)); n_color++; real(r, g, b); }
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { NEXT(void, glColor4f, (GLfloat, GLfloat, GLfloat, GLfloat)); n_color++; real(r, g, b, a); }

/* ---- vertex arrays / display lists ---- */
void glDrawArrays(GLenum m, GLint first, GLsizei count) {
    NEXT(void, glDrawArrays, (GLenum, GLint, GLsizei));
    n_draw_arrays++; n_draw_verts += count; real(m, first, count);
}
void glDrawElements(GLenum m, GLsizei count, GLenum type, const void *idx) {
    NEXT(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *));
    n_draw_arrays++; n_draw_verts += count; real(m, count, type, idx);
}
void glCallList(GLuint l) { NEXT(void, glCallList, (GLuint)); n_call_list++; real(l); }
void glVertexPointer(GLint s, GLenum t, GLsizei st, const void *p) {
    NEXT(void, glVertexPointer, (GLint, GLenum, GLsizei, const void *)); n_other++; real(s, t, st, p);
}
void glColorPointer(GLint s, GLenum t, GLsizei st, const void *p) {
    NEXT(void, glColorPointer, (GLint, GLenum, GLsizei, const void *)); n_other++; real(s, t, st, p);
}
void glClear(GLbitfield m) { NEXT(void, glClear, (GLbitfield)); n_other++; real(m); }

/* ---- GLFW ---- */
void glfwSwapInterval(int i) {
    NEXT(void, glfwSwapInterval, (int));
    if (getenv("GLCOUNT_NOVSYNC")) i = 0;
    real(i);
}

static void shot(const char *path) {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2], h = vp[3];
    unsigned char *px = malloc((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (int y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
    fclose(f);
    free(px);
}

void glfwSwapBuffers(GLFWwindow *w) {
    NEXT(void, glfwSwapBuffers, (GLFWwindow *));
    static long shot_n = 0;
    const char *sp = getenv("GLCOUNT_SHOT");
    if (sp) {
        const char *sf = getenv("GLCOUNT_SHOT_FRAME");
        if (++shot_n >= (sf ? atol(sf) : 1)) { shot(sp); _exit(0); }
    }
    real(w);
    double t = now_s();
    static int warm = 0;
    frames++;
    if (!warm && frames == WARMUP) {
        warm = 1;
        t_start = t;
        /* счётчики вызовов тоже считаем только по измеряемым кадрам */
        n_begin = n_vertex = n_color = n_draw_arrays = n_draw_verts = n_call_list = n_other = 0;
        frames = 0;
    } else if (t_start > 0) {
        double dt = t - t_prev;
        if (dt < t_min) t_min = dt;
        if (dt > t_max) t_max = dt;
        frames_measured++;
    }
    t_prev = t;
    if (limit > 0 && (long)frames_measured >= limit) exit(0);
}
