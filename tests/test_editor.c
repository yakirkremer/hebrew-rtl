/* SPDX-License-Identifier: GPL-3.0-only */
/* editor_atom_x (the search field's word order) against fake TextEditor Iterator,
 * UniformTextSection and TextAtom memory laid out as EP147's. Built with the SDK headers:
 *   clang -std=gnu11 -fsanitize=address,undefined -I . -I <sdk>/include tests/test_editor.c \
 *         bidi.c glyphs.c -lm -o out/test_editor && out/test_editor */
#include "../hebrew-rtl.c"
#include <stdio.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)
struct atom { const char *text; float width; uint16_t nchars; uint16_t pad; };
_Static_assert(sizeof(struct atom) == ATOM_SIZE, "atom");
static unsigned char it[0x80], section[0x30], owned[0x18];
static struct atom atoms[200];
static void *secptr;
static void setup(int n) {
    memset(it, 0, sizeof it); memset(section, 0, sizeof section); memset(owned, 0, sizeof owned);
    secptr = section;
    *(void **)owned = &secptr; *(int *)(owned + 0x10) = 1;
    *(void **)(section + SECTION_ATOMS) = atoms; *(int *)(section + SECTION_NUM_ATOMS) = n;
    *(void **)(it + IT_SECTION) = section; *(void **)(it + IT_SECTIONS) = owned;
    *(float *)(it + IT_WRAP_WIDTH) = 100000.0f;
}
static float x_of(int k, float start) {
    float x = start; for (int i = 0; i < k; i++) x += atoms[i].width;
    *(float *)(it + IT_ATOM_X) = x; *(void **)(it + IT_ATOM) = &atoms[k];
    return editor_atom_x(it, x);
}
static const char *words[] = { "שלום", "עומר", "Adam", "2024", " ", "  ", "-", "(רמיקס)", "Remix)", "é", "😀", "״", "" };
int main(void) {
    /* pure Hebrew: reversed */
    atoms[0] = (struct atom){ "עומר", 40 }; atoms[1] = (struct atom){ " ", 5 }; atoms[2] = (struct atom){ "אדם", 30 };
    setup(3);
    CHECK(x_of(0, 10) == 10 + 35, "word 1 x %f", x_of(0, 10));
    CHECK(x_of(2, 10) == 10, "word 2 x %f", x_of(2, 10));
    /* Latin first: untouched */
    atoms[0] = (struct atom){ "Adam", 40 }; setup(3);
    CHECK(x_of(2, 10) == 10 + 45, "LTR line moved");
    /* random lines: positions tile [start, start + total] exactly once */
    srand(3);
    for (int iter = 0; iter < 200000; iter++) {
        int n = 1 + rand() % 140;
        float total = 0;
        for (int i = 0; i < n; i++) { atoms[i] = (struct atom){ words[rand() % 13], (float)(1 + rand() % 50) }; total += atoms[i].width; }
        setup(n);
        float start = (float)(rand() % 20);
        static float xs[200]; int moved = 0;
        for (int k = 0; k < n; k++) { xs[k] = x_of(k, start); float lx = start; for (int i = 0; i < k; i++) lx += atoms[i].width; moved |= xs[k] != lx; }
        if (n > EDITOR_MAX_ATOMS) { CHECK(!moved, "over the atom limit but moved"); continue; }
        /* sort by x and check they tile */
        int ord[200]; for (int i = 0; i < n; i++) ord[i] = i;
        for (int i = 1; i < n; i++) for (int j = i; j > 0 && xs[ord[j]] < xs[ord[j-1]]; j--) { int t = ord[j]; ord[j] = ord[j-1]; ord[j-1] = t; }
        float at = start;
        for (int i = 0; i < n; i++) { if (xs[ord[i]] < at - 0.01f || xs[ord[i]] > at + 0.01f) { CHECK(0, "gap or overlap at %d (n %d)", i, n); break; } at += atoms[ord[i]].width; }
    }
    /* refused shapes: two sections, a tempAtom, a newline, a too-narrow editor */
    atoms[0] = (struct atom){ "עומר", 40 }; atoms[1] = (struct atom){ " ", 5 }; atoms[2] = (struct atom){ "אדם", 30 };
    setup(3); *(int *)(owned + 0x10) = 2; CHECK(x_of(2, 10) == 10 + 45, "two sections moved");
    setup(3); *(float *)(it + IT_ATOM_X) = 5; *(void **)(it + IT_ATOM) = it + 0x58; CHECK(editor_atom_x(it, 5) == 5, "tempAtom moved");
    atoms[1] = (struct atom){ "\n", 0 }; setup(3); CHECK(x_of(2, 10) == 10 + 40, "newline moved");
    atoms[1] = (struct atom){ " ", 5 }; setup(3); *(float *)(it + IT_WRAP_WIDTH) = 50; CHECK(x_of(2, 10) == 10 + 45, "wrapped moved");
    setup(3); *(void **)(it + IT_ATOM) = (char *)&atoms[1] + 4; CHECK(editor_atom_x(it, 7) == 7, "misaligned atom moved");
    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
