/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Hebrew letters for the deck's fonts, which have none.
 *
 * EP147 (JUCE 5.3.2) asks a font for a character the first time it draws it, through
 * juce::FreeTypeTypeface::loadGlyphIfPossible: FreeType loads the outline, JUCE replays it
 * into a juce::Path, scaled to the font's height, and stores it with
 * CustomTypeface::addGlyph. From then on the letter is cached and drawn like any other.
 * For a Hebrew letter the deck's font has nothing to load, so it draws a box.
 *
 * Here the same steps run on the built-in Noto Sans Hebrew (heb_font.h), read with
 * stb_truetype: the outline is replayed into a juce::Path with EP147's own Path functions
 * and handed to EP147's own addGlyph, so JUCE caches and draws it exactly as it does the
 * font's own letters. No file is read or written.
 */
#include "glyphs.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_assert(x) ((void)0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"   /* only the outline reader is used */
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#include "heb_font.h"
#include "bidi.h"

/* ep147-9e9454e357389ece: the Path functions and CustomTypeface::addGlyph exactly as
 * FreeTypeTypeface::loadGlyphIfPossible (0x25620a0) calls them. */
#define ADDR_PATH_START_SUBPATH 0x2518440u   /* startNewSubPath(float x, float y) */
#define ADDR_PATH_LINE_TO       0x2518590u   /* lineTo(float x, float y) */
#define ADDR_PATH_QUADRATIC_TO  0x2518f20u   /* quadraticTo(cx, cy, x, y) */
#define ADDR_PATH_CUBIC_TO      0x25190f0u   /* cubicTo(c1x, c1y, c2x, c2y, x, y) */
#define ADDR_PATH_CLOSE         0x25194b0u   /* closeSubPath() */
#define ADDR_ADD_GLYPH          0x2546180u   /* CustomTypeface::addGlyph(char, const Path&, float width) */

/* juce::FreeTypeTypeface's vtable pointer (the address point of its vtable at 0x34f6600:
 * offset-to-top, type info, then the destructors at 0x34f6610). Only such a typeface has a
 * FreeType face behind it. */
#define FREETYPE_TYPEFACE_VPTR 0x34f6610u
/* CustomTypeface's OwnedArray<GlyphInfo> glyphs, as addGlyph appends to it: the element
 * pointers at +0x60, their count at +0x70. A GlyphInfo starts with its character. */
#define TYPEFACE_GLYPHS       0x60
#define TYPEFACE_NUM_GLYPHS   0x70

/* What loadGlyphIfPossible reads: the FreeType face behind the typeface, and the face's
 * size fields (FT_FaceRec, FreeType 2). */
#define TYPEFACE_FACE_WRAPPER 0x178          /* FreeTypeTypeface::faceWrapper */
#define WRAPPER_FACE          0x10           /* FTFaceWrapper::face */
#define FACE_UNITS_PER_EM     0x88
#define FACE_ASCENDER         0x8a
#define FACE_DESCENDER        0x8c

/* The steps below mirror juce::FreeTypeTypeface::loadGlyphIfPossible (0x25620a0). Its own
 * entry cannot be hooked: an inline hook cannot relocate the cbz it starts with, and its
 * vtable slot lies in the code segment, where the core does not swap slots. */

/* Hebrew letters stand as tall as the deck fonts' capitals: 0.700 em in UCGothic, 0.747 em
 * in TsukuGo. */
#define CAP_HEIGHT_EM 0.72f

/* juce::Path (JUCE 5.3): an Array<float> of commands and points, the bounds, and the
 * winding rule; loadGlyphIfPossible builds one on its stack the same way. */
struct juce_path {
    float *data;
    int num_allocated;
    int pad0;
    int num_used;
    int pad1;
    float bounds[4];
    bool non_zero_winding;
};
_Static_assert(offsetof(struct juce_path, bounds) == 0x18 &&
               offsetof(struct juce_path, non_zero_winding) == 0x28, "JUCE 5.3 Path layout");

typedef void (*path_point_fn)(struct juce_path *p, float x, float y);
typedef void (*path_quadratic_fn)(struct juce_path *p, float cx, float cy, float x, float y);
typedef void (*path_cubic_fn)(struct juce_path *p, float c1x, float c1y, float c2x, float c2y,
                              float x, float y);
typedef void (*path_close_fn)(struct juce_path *p);
typedef void (*add_glyph_fn)(void *typeface, uint32_t c, const struct juce_path *p, float width);

#ifndef HEB_GLYPHS_TEST
static const path_point_fn path_start = (path_point_fn)ADDR_PATH_START_SUBPATH;
static const path_point_fn path_line = (path_point_fn)ADDR_PATH_LINE_TO;
static const path_quadratic_fn path_quadratic = (path_quadratic_fn)ADDR_PATH_QUADRATIC_TO;
static const path_cubic_fn path_cubic = (path_cubic_fn)ADDR_PATH_CUBIC_TO;
static const path_close_fn path_close = (path_close_fn)ADDR_PATH_CLOSE;
static const add_glyph_fn add_glyph = (add_glyph_fn)ADDR_ADD_GLYPH;
#else   /* tests/test_glyphs.c stands in for EP147's functions */
extern path_point_fn path_start, path_line;
extern path_quadratic_fn path_quadratic;
extern path_cubic_fn path_cubic;
extern path_close_fn path_close;
extern add_glyph_fn add_glyph;
#endif

static stbtt_fontinfo heb;
static int heb_ready;
static float heb_letter_height;      /* the top of HE (U+05D4), in the font's units */

int heb_glyphs_init(void)
{
    int x0, y0, x1, y1;
    if (!stbtt_InitFont(&heb, heb_font, 0)) return -1;
    int he = stbtt_FindGlyphIndex(&heb, 0x05D4);
    if (!he || !stbtt_GetGlyphBox(&heb, he, &x0, &y0, &x1, &y1) || y1 <= 0) return -1;
    heb_letter_height = (float)y1;
    heb_ready = 1;
    return 0;
}

int heb_glyphs_covers(uint32_t c)
{
    if (!heb_ready) return 0;
    if (!((c >= 0x0590 && c <= 0x05FF) || (c >= 0xFB1D && c <= 0xFB4F))) return 0;
    return stbtt_FindGlyphIndex(&heb, (int)c) != 0;
}

bool heb_glyphs_add(void *typeface, uint32_t c)
{
    const char *wrapper = *(const char *const *)((const char *)typeface + TYPEFACE_FACE_WRAPPER);
    if (!heb_ready || !wrapper) return false;
    const char *face = *(const char *const *)(wrapper + WRAPPER_FACE);
    if (!face) return false;
    int upm = *(const uint16_t *)(face + FACE_UNITS_PER_EM);
    int height = *(const int16_t *)(face + FACE_ASCENDER) - *(const int16_t *)(face + FACE_DESCENDER);
    int g = stbtt_FindGlyphIndex(&heb, (int)c);
    if (upm <= 0 || height <= 0 || !g) return false;

    /* JUCE keeps glyphs in units of the font's height (ascender - descender), y down. */
    float k = CAP_HEIGHT_EM * (float)upm / heb_letter_height / (float)height;
    stbtt_vertex *v = NULL;
    int n = stbtt_GetGlyphShape(&heb, g, &v);
    if (n < 0) return false;

    struct juce_path p;
    memset(&p, 0, sizeof p);
    p.non_zero_winding = true;
    bool open = false;
    for (int i = 0; i < n; i++) {
        float x = v[i].x * k, y = -v[i].y * k;
        switch (v[i].type) {
        case STBTT_vmove:
            if (open) path_close(&p);
            path_start(&p, x, y);
            open = true;
            break;
        case STBTT_vline:
            path_line(&p, x, y);
            break;
        case STBTT_vcurve:
            path_quadratic(&p, v[i].cx * k, -v[i].cy * k, x, y);
            break;
        case STBTT_vcubic:
            path_cubic(&p, v[i].cx * k, -v[i].cy * k, v[i].cx1 * k, -v[i].cy1 * k, x, y);
            break;
        }
    }
    if (open) path_close(&p);
    stbtt_FreeShape(&heb, v);

    int advance, lsb;
    stbtt_GetGlyphHMetrics(&heb, g, &advance, &lsb);
    add_glyph(typeface, c, &p, (float)advance * k);   /* copies the path */
    free(p.data);                                      /* juce::HeapBlock: malloc'd */
    return true;
}

static int has_glyph(const char *typeface, uint32_t c)
{
    const uint32_t *const *glyphs = *(const uint32_t *const *const *)(typeface + TYPEFACE_GLYPHS);
    int n = *(const int *)(typeface + TYPEFACE_NUM_GLYPHS);
    for (int i = 0; i < n; i++)
        if (glyphs[i] && *glyphs[i] == c) return 1;
    return 0;
}

/* An invisible formatting character gets an empty glyph with no width, so JUCE draws
 * nothing for it instead of the font's box. */
static void add_invisible(void *typeface, uint32_t c)
{
    struct juce_path p;
    memset(&p, 0, sizeof p);
    p.non_zero_winding = true;
    add_glyph(typeface, c, &p, 0.0f);
}

void heb_glyphs_ensure(void *typeface, const char *utf8)
{
    if (!heb_ready || !typeface || !utf8) return;
    if (*(const uintptr_t *)typeface != FREETYPE_TYPEFACE_VPTR) return;
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p) {
        /* Only two kinds of sequence matter: 0xD6..0xD8 + 1 (Hebrew, U+061C) and
         * 0xE2 / 0xEF + 2 (U+20xx controls, U+FB1D..U+FB4F, U+FEFF). Anything else is
         * skipped a byte at a time. */
        uint32_t c;
        if (p[0] >= 0xD6 && p[0] <= 0xD8 && (p[1] & 0xC0) == 0x80) {
            c = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((p[0] == 0xE2 || p[0] == 0xEF) && (p[1] & 0xC0) == 0x80 &&
                   (p[2] & 0xC0) == 0x80) {
            c = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            p++;
            continue;
        }
        if (has_glyph(typeface, c)) continue;
        if (heb_bidi_invisible(c)) add_invisible(typeface, c);
        else if (heb_glyphs_covers(c)) heb_glyphs_add(typeface, c);
    }
}
