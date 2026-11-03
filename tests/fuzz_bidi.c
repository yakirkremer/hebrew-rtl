/* SPDX-License-Identifier: GPL-3.0-only */
/* Random UTF-8-ish strings through heb_bidi_visual under ASan/UBSan: no crash, and the
 * output is never longer than the input. */
#include "../bidi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *pieces[] = { "a", "Z", "7", " ", "-", "+", ".", ",", "%", "$", "(", ")",
    "[", "»", "ש", "ל", "ם", "ָ", "ׁ", "ع", "ـ", "é", "\xe2\x80\x8f", "\xff", "\xc3", "\t" };

int main(void)
{
    char in[2048], out[2048];
    srand(1);
    for (int iter = 0; iter < 200000; iter++) {
        size_t len = 0, parts = (size_t)(rand() % 60);
        for (size_t k = 0; k < parts; k++) {
            const char *p = pieces[rand() % (int)(sizeof pieces / sizeof *pieces)];
            size_t l = strlen(p);
            if (len + l >= sizeof in) break;
            memcpy(in + len, p, l);
            len += l;
        }
        in[len] = '\0';
        int n = heb_bidi_visual(in, out, len + 1);
        if (n > (int)len) { printf("grew: [%s]\n", in); return 1; }
        heb_bidi_has_rtl(in);
    }
    puts("fuzz ok");
    return 0;
}
