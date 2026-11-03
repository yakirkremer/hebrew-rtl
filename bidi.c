/* SPDX-License-Identifier: GPL-3.0-only */
/* Rule names (P2, W1, N1, L2, ...) follow UAX #9. */
#include "bidi.h"

#include <stdint.h>
#include <string.h>

enum { L, R, EN, ES, ET, CS, NSM, WS, ON };

int heb_bidi_invisible(uint32_t c)
{
    return c == 0x061C || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||
           (c >= 0x2060 && c <= 0x2064) || (c >= 0x2066 && c <= 0x2069) || c == 0xFEFF;
}

static int bidi_class(uint32_t c)
{
    if (c < 0x80) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return L;
        if (c >= '0' && c <= '9') return EN;
        if (c == ' ' || c == '\t') return WS;
        if (c == '+' || c == '-') return ES;
        if (c == '#' || c == '$' || c == '%') return ET;
        if (c == ',' || c == '.' || c == ':' || c == '/') return CS;
        return ON;
    }
    /* Hebrew */
    if ((c >= 0x0591 && c <= 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 ||
        c == 0x05C4 || c == 0x05C5 || c == 0x05C7 || c == 0xFB1E)
        return NSM;
    if (c >= 0x0590 && c <= 0x05FF) return R;
    if (c == 0xFB29) return ES;
    if (c >= 0xFB1D && c <= 0xFB4F) return R;
    /* Arabic: ordered as R. Letter joining (shaping) is not done here. */
    if ((c >= 0x064B && c <= 0x065F) || c == 0x0670) return NSM;
    if (c >= 0x06F0 && c <= 0x06F9) return EN;
    if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) ||
        (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFE))
        return R;
    if (c == 0x200F) return R;   /* RLM */
    if (c == 0x200E) return L;   /* LRM */
    /* The embedding and isolate controls, zero-width characters and BOM: their own effect
     * (UAX #9 X1-X10) is not modelled; they count as neutral and are not drawn. */
    if (heb_bidi_invisible(c)) return ON;
    if (c >= 0x0300 && c <= 0x036F) return NSM;
    if (c == 0xA0) return CS;
    if ((c >= 0xA2 && c <= 0xA5) || c == 0xB0 || c == 0xB1 || c == 0x20AC) return ET;
    if (c >= 0x2000 && c <= 0x200A) return WS;
    if ((c >= 0xA1 && c <= 0xBF) || c == 0xD7 || c == 0xF7 ||
        (c >= 0x2010 && c <= 0x205E))
        return ON;
    return L;
}

static uint32_t mirror(uint32_t c)
{
    switch (c) {
    case '(': return ')';  case ')': return '(';
    case '[': return ']';  case ']': return '[';
    case '{': return '}';  case '}': return '{';
    case '<': return '>';  case '>': return '<';
    case 0xAB: return 0xBB; case 0xBB: return 0xAB;   /* « » */
    default: return c;
    }
}

static int is_open_bracket(uint32_t c) { return c == '(' || c == '[' || c == '{'; }
static int is_close_bracket(uint32_t c) { return c == ')' || c == ']' || c == '}'; }

/* BD16 lists pairs in the order of their opening brackets. */
static void sort_pairs(int *opens, int *closes, int np)
{
    for (int i = 1; i < np; i++)
        for (int j = i; j > 0 && opens[j - 1] > opens[j]; j--) {
            int a = opens[j]; opens[j] = opens[j - 1]; opens[j - 1] = a;
            a = closes[j]; closes[j] = closes[j - 1]; closes[j - 1] = a;
        }
}

/* Strict UTF-8: no overlongs, surrogates or code points past U+10FFFF. */
static int utf8_next(const unsigned char **p, uint32_t *out)
{
    const unsigned char *s = *p;
    uint32_t c = s[0];
    int n;
    if (c < 0x80) { *out = c; *p = s + 1; return 1; }
    if (c >= 0xC2 && c <= 0xDF) { n = 1; c &= 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 2; c &= 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 3; c &= 0x07; }
    else return 0;
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        c = (c << 6) | (s[i] & 0x3F);
    }
    if ((n == 2 && c < 0x800) || (n == 3 && (c < 0x10000 || c > 0x10FFFF)) ||
        (c >= 0xD800 && c <= 0xDFFF))
        return 0;
    *out = c;
    *p = s + n + 1;
    return 1;
}

static size_t utf8_put(char *o, uint32_t c)
{
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        o[2] = (char)(0x80 | (c & 0x3F)); return 3;
    }
    o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F)); return 4;
}

int heb_bidi_has_rtl(const char *s)
{
    /* Every RTL code point we classify is U+0590 or above, so its UTF-8 lead byte is
     * 0xD6 or above; plain Latin-1 text never gets past this test. */
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c;
        if (*p < 0xD6) { p++; continue; }
        if (!utf8_next(&p, &c)) { p++; continue; }
        if (bidi_class(c) == R) return 1;
    }
    return 0;
}

int heb_bidi_base_rtl(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c;
        if (!utf8_next(&p, &c)) return 0;
        int t = bidi_class(c);
        if (t == L) return 0;
        if (t == R) return 1;
    }
    return 0;
}

static void reverse_units(int *u, int a, int b)
{
    for (b--; a < b; a++, b--) { int t = u[a]; u[a] = u[b]; u[b] = t; }
}

int heb_bidi_visual(const char *in, char *out, size_t outsz)
{
    static __thread uint32_t cp[HEB_BIDI_MAX];
    static __thread unsigned char t[HEB_BIDI_MAX], orig[HEB_BIDI_MAX], lv[HEB_BIDI_MAX];
    static __thread int unit[HEB_BIDI_MAX];
    size_t inlen = strlen(in);
    int n = 0;

    if (outsz < inlen + 1) return -1;
    for (const unsigned char *p = (const unsigned char *)in; *p; n++) {
        if (n == HEB_BIDI_MAX || !utf8_next(&p, &cp[n])) return -1;
        orig[n] = t[n] = (unsigned char)bidi_class(cp[n]);
    }

    /* P2/P3: the first strong letter sets the paragraph direction. */
    int base = 0;
    for (int i = 0; i < n; i++)
        if (t[i] == L || t[i] == R) { base = t[i] == R; break; }
    int sos = base ? R : L;

    /* W1: a mark takes the type of what it follows. */
    for (int i = 0; i < n; i++)
        if (t[i] == NSM) t[i] = i ? t[i - 1] : sos;
    /* W4: one separator between two numbers joins them. */
    for (int i = 1; i + 1 < n; i++)
        if ((t[i] == ES || t[i] == CS) && t[i - 1] == EN && t[i + 1] == EN) t[i] = EN;
    /* W5: currency and percent signs next to a number belong to it. */
    for (int i = 0; i < n; i++) {
        if (t[i] != ET) continue;
        int j = i;
        while (j < n && t[j] == ET) j++;
        if ((i > 0 && t[i - 1] == EN) || (j < n && t[j] == EN))
            for (int k = i; k < j; k++) t[k] = EN;
        i = j - 1;
    }
    /* W6: what is left of the separators is neutral. */
    for (int i = 0; i < n; i++)
        if (t[i] == ES || t[i] == ET || t[i] == CS) t[i] = ON;
    /* W7: a number in left-to-right context is just left-to-right text. */
    for (int i = 0, strong = sos; i < n; i++) {
        if (t[i] == L || t[i] == R) strong = t[i];
        else if (t[i] == EN && strong == L) t[i] = L;
    }
    /* N0: a bracket pair takes one direction for both brackets, so "(Remix)" after
     * Hebrew stays a matched pair. Inside text of the paragraph direction wins; inside
     * text of only the other direction wins when the text before the pair agrees. */
    {
        enum { PAIRS = 63 };
        int stack[PAIRS], depth = 0, opens[PAIRS], closes[PAIRS], np = 0;
        for (int i = 0; i < n && np < PAIRS; i++) {
            if (t[i] != ON) continue;
            if (is_open_bracket(cp[i])) {
                if (depth == PAIRS) break;          /* BD16: stop pairing on overflow */
                stack[depth++] = i;
            } else if (is_close_bracket(cp[i])) {
                for (int d = depth - 1; d >= 0; d--) {
                    if (mirror(cp[stack[d]]) == cp[i]) {
                        opens[np] = stack[d];
                        closes[np++] = i;
                        depth = d;
                        break;
                    }
                }
            }
        }
        sort_pairs(opens, closes, np);
        for (int p = 0; p < np; p++) {
            int o = opens[p], c = closes[p], found = -1;
            for (int k = o + 1; k < c; k++) {
                int s = t[k] == L ? L : (t[k] == R || t[k] == EN) ? R : -1;
                if (s == sos) { found = sos; break; }
                if (s >= 0) found = s;
            }
            if (found < 0) continue;
            int d = found;
            if (found != sos) {
                int before = sos;
                for (int k = o - 1; k >= 0; k--)
                    if (t[k] == L || t[k] == R || t[k] == EN) { before = t[k] == L ? L : R; break; }
                d = before == found ? found : sos;
            }
            t[o] = t[c] = (unsigned char)d;
        }
    }
    /* N1/N2: neutrals between two runs of the same direction take it (numbers count
     * as R); otherwise they take the paragraph direction. */
    for (int i = 0; i < n; i++) {
        if (t[i] != WS && t[i] != ON) continue;
        int j = i;
        while (j < n && (t[j] == WS || t[j] == ON)) j++;
        int before = i ? (t[i - 1] == L ? L : R) : sos;
        int after = j < n ? (t[j] == L ? L : R) : sos;
        int d = before == after ? before : sos;
        for (int k = i; k < j; k++) t[k] = (unsigned char)d;
        i = j - 1;
    }
    /* I1/I2: embedding levels. */
    int maxlv = base;
    for (int i = 0; i < n; i++) {
        if (base == 0) lv[i] = t[i] == R ? 1 : t[i] == EN ? 2 : 0;
        else lv[i] = t[i] == L || t[i] == EN ? 2 : 1;
        if (lv[i] > maxlv) maxlv = lv[i];
    }
    /* L1, adapted: UAX #9 gives trailing whitespace the paragraph level, which in a
     * right-to-left line moves it to the left edge. EP147 left-aligns every line, so
     * that would indent the text; level 0 keeps it at the right end instead. */
    for (int i = n - 1; i >= 0 && (orig[i] == WS || orig[i] == NSM); i--) lv[i] = 0;

    /* A unit is a base character with the marks that follow it; units are what gets
     * reversed, so marks keep following their letter in the output. */
    int nu = 0;
    for (int i = 0; i < n; i++)
        if (i == 0 || orig[i] != NSM) unit[nu++] = i;
    /* L2: from the highest level down to the lowest odd one, reverse every run at
     * that level or above. */
    for (int k = maxlv; k >= 1; k--) {
        for (int a = 0; a < nu;) {
            if (lv[unit[a]] < k) { a++; continue; }
            int b = a;
            while (b < nu && lv[unit[b]] >= k) b++;
            reverse_units(unit, a, b);
            a = b;
        }
    }

    size_t o = 0;
    for (int u = 0; u < nu; u++) {
        int i = unit[u];
        do {
            uint32_t c = lv[i] & 1 ? mirror(cp[i]) : cp[i];   /* L4 */
            if (!heb_bidi_invisible(c)) o += utf8_put(out + o, c);
            i++;
        } while (i < n && orig[i] == NSM);
    }
    out[o] = '\0';
    return (int)o;
}
