/* SPDX-License-Identifier: GPL-3.0-only */
/* glyphs.c against stand-ins for EP147: a fake FreeTypeTypeface with a FreeType face and a
 * glyph table laid out as EP147's, and juce::Path / addGlyph replacements that check every
 * call. Built with -DHEB_GLYPHS_TEST under AddressSanitizer and UBSan:
 *   clang -std=gnu11 -DHEB_GLYPHS_TEST -fsanitize=address,undefined -I . \
 *         glyphs.c bidi.c tests/test_glyphs.c -lm -o out/test_glyphs && out/test_glyphs */
#include "../glyphs.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same layout as glyphs.c's struct juce_path (JUCE 5.3). */
struct juce_path {
    float *data;
    int num_allocated;
    int pad0;
    int num_used;
    int pad1;
    float bounds[4];
    bool non_zero_winding;
};

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- juce::Path stand-ins: append floats like JUCE does (a command marker, then points) */
static int open_subpath;
static void push(struct juce_path *p, float v)
{
    if (p->num_used == p->num_allocated) {
        p->num_allocated = p->num_allocated ? p->num_allocated * 2 : 32;
        p->data = realloc(p->data, (size_t)p->num_allocated * sizeof(float));
    }
    p->data[p->num_used++] = v;
}
static void check_point(float x, float y)
{
    CHECK(isfinite(x) && isfinite(y) && fabsf(x) < 2.0f && fabsf(y) < 2.0f,
          "point (%f, %f) outside the em", x, y);
}
static void m_start(struct juce_path *p, float x, float y)
{
    CHECK(!open_subpath, "startNewSubPath with a subpath still open");
    check_point(x, y); push(p, 100000.0f); push(p, x); push(p, y); open_subpath = 1;
}
static void m_line(struct juce_path *p, float x, float y)
{
    CHECK(open_subpath, "lineTo outside a subpath");
    check_point(x, y); push(p, 100001.0f); push(p, x); push(p, y);
}
static void m_quad(struct juce_path *p, float cx, float cy, float x, float y)
{
    CHECK(open_subpath, "quadraticTo outside a subpath");
    check_point(cx, cy); check_point(x, y);
    push(p, 100002.0f); push(p, cx); push(p, cy); push(p, x); push(p, y);
}
static void m_cubic(struct juce_path *p, float a, float b, float c, float d, float x, float y)
{
    CHECK(open_subpath, "cubicTo outside a subpath");
    check_point(a, b); check_point(c, d); check_point(x, y);
    push(p, 100003.0f); push(p, x); push(p, y);
}
static void m_close(struct juce_path *p)
{
    CHECK(open_subpath, "closeSubPath without a subpath");
    push(p, 100004.0f); open_subpath = 0;
}

/* ---- the fake typeface: the fields glyphs.c reads, at EP147's offsets */
struct glyph_info { uint32_t character; float width; int floats; };
static struct {
    unsigned char face[0x100];
    unsigned char wrapper[0x20];
    unsigned char typeface[0x200];
    struct glyph_info *list[512];
} F;
static int added;

static void m_add_glyph(void *typeface, uint32_t c, const struct juce_path *p, float width)
{
    CHECK(typeface == F.typeface, "addGlyph on another object");
    CHECK(!open_subpath, "addGlyph with a subpath still open");
    CHECK(isfinite(width) && width >= 0.0f && width < 2.0f, "U+%04X width %f", c, width);
    int *num = (int *)(F.typeface + 0x70);
    CHECK(*num < 512, "too many glyphs");
    if (*num >= 512) return;
    struct glyph_info *g = malloc(sizeof *g);   /* EP147's GlyphInfo starts with the character */
    g->character = c; g->width = width; g->floats = p->num_used;
    F.list[(*num)++] = g;
    added++;
}

void (*path_start)(struct juce_path *, float, float) = m_start;
void (*path_line)(struct juce_path *, float, float) = m_line;
void (*path_quadratic)(struct juce_path *, float, float, float, float) = m_quad;
void (*path_cubic)(struct juce_path *, float, float, float, float, float, float) = m_cubic;
void (*path_close)(struct juce_path *) = m_close;
void (*add_glyph)(void *, uint32_t, const struct juce_path *, float) = m_add_glyph;

static void reset_typeface(uintptr_t vptr, int with_face)
{
    for (int i = 0; i < *(int *)(F.typeface + 0x70); i++) free(F.list[i]);
    memset(&F, 0, sizeof F);
    *(uint16_t *)(F.face + 0x88) = 1000;          /* units_per_EM */
    *(int16_t *)(F.face + 0x8a) = 880;            /* ascender */
    *(int16_t *)(F.face + 0x8c) = -120;           /* descender */
    *(unsigned char **)(F.wrapper + 0x10) = F.face;
    *(uintptr_t *)F.typeface = vptr;
    *(unsigned char **)(F.typeface + 0x178) = with_face ? F.wrapper : NULL;
    *(struct glyph_info ***)(F.typeface + 0x60) = F.list;
    added = 0;
}

static size_t utf8(uint32_t c, char *o)
{
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    o[2] = (char)(0x80 | (c & 0x3F)); return 3;
}

int main(void)
{
    CHECK(heb_glyphs_init() == 0, "built-in font did not load");

    /* Every code point the font covers: added once, never twice. */
    reset_typeface(0x34f6610u, 1);
    int covered = 0;
    char text[4096]; size_t len = 0;
    for (uint32_t c = 0x0590; c <= 0xFB4F; c = (c == 0x05FF) ? 0xFB1D : c + 1) {
        if (!heb_glyphs_covers(c)) continue;
        covered++;
        len += utf8(c, text + len);
        len += utf8(' ', text + len);
    }
    text[len] = '\0';
    heb_glyphs_ensure(F.typeface, text);
    CHECK(added == covered, "added %d of %d covered", added, covered);
    heb_glyphs_ensure(F.typeface, text);
    CHECK(added == covered, "second pass added again (%d)", added - covered);
    for (int i = 0; i < *(int *)(F.typeface + 0x70); i++) {
        struct glyph_info *g = F.list[i];
        if (g->character >= 0x05D0 && g->character <= 0x05EA)   /* the 27 letters */
            CHECK(g->floats > 0 && g->width > 0.2f, "letter U+%04X empty or zero width", g->character);
    }
    printf("covered %d code points, all added once\n", covered);

    /* Mixed text: only the Hebrew letters are added. */
    reset_typeface(0x34f6610u, 1);
    heb_glyphs_ensure(F.typeface, "Omer Adam - שיר מעורב (Remix 2024).mp3");
    CHECK(added == 7, "mixed line added %d, want 7 (ש י ר מ ע ו ב)", added);

    /* Invisible formatting characters get empty, zero-width glyphs, once. */
    reset_typeface(0x34f6610u, 1);
    heb_glyphs_ensure(F.typeface, "\u200e\u2068שיר\u2069 \u202c\ufeffX");
    CHECK(added == 3 + 5, "invisible test added %d, want 3 letters + 5 controls", added);
    heb_glyphs_ensure(F.typeface, "\u200e\u2068שיר\u2069 \u202c\ufeffX");
    CHECK(added == 8, "invisible controls added twice");
    for (int i = 0; i < *(int *)(F.typeface + 0x70); i++) {
        struct glyph_info *g = F.list[i];
        if (g->character >= 0x2000 || g->character == 0xFEFF)
            CHECK(g->floats == 0 && g->width == 0.0f, "control U+%04X is not empty", g->character);
    }

    /* Not a FreeTypeTypeface, or no face: nothing happens. */
    reset_typeface(0x1234u, 1);
    heb_glyphs_ensure(F.typeface, "שלום");
    CHECK(added == 0, "added to a typeface of another class");
    reset_typeface(0x34f6610u, 0);
    heb_glyphs_ensure(F.typeface, "שלום");
    CHECK(added == 0, "added without a FreeType face");
    heb_glyphs_ensure(NULL, "שלום");
    heb_glyphs_ensure(F.typeface, NULL);

    /* Random bytes: any text at all is scanned without reading past its end. */
    reset_typeface(0x34f6610u, 1);
    srand(7);
    for (int iter = 0; iter < 200000; iter++) {
        int n = rand() % 24;
        char *buf = malloc((size_t)n + 1);       /* exact size, so ASan sees any overread */
        for (int k = 0; k < n; k++) {
            static const unsigned char pick[] = { 0xD6, 0xD7, 0xEF, 0xAC, 0xAD, 0x90, 0xBF, 0x80,
                                                  0x9D, 'a', ' ', 0xC3, 0xFF };
            buf[k] = (char)pick[rand() % (int)sizeof pick];
            if (buf[k] == 0) buf[k] = 'x';
        }
        buf[n] = '\0';
        heb_glyphs_ensure(F.typeface, buf);
        free(buf);
    }
    CHECK(added <= covered, "random text added %d glyphs, more than exist", added);
    reset_typeface(0, 0);

    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
