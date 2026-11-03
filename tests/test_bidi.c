/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bidi.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void check(const char *in, const char *want)
{
    char out[4096];
    int n = heb_bidi_visual(in, out, sizeof out);
    if (n < 0 || strcmp(out, want) != 0) {
        printf("FAIL  in:   [%s]\n      want: [%s]\n      got:  [%s] (%d)\n", in, want,
               n < 0 ? "" : out, n);
        fails++;
    }
}

int main(void)
{
    /* Plain text is untouched. */
    check("Artist - Song (Remix)", "Artist - Song (Remix)");
    check("", "");

    /* Pure Hebrew reverses. */
    check("שלום", "םולש");
    check("שיר אהבה", "הבהא ריש");

    /* Numbers keep their own order inside Hebrew. */
    check("שיר 2026", "2026 ריש");
    check("124 BPM שיר", "124 BPM ריש");          /* first strong letter is Latin: LTR line */
    check("גרסה 1.30", "1.30 הסרג");
    check("מחיר 50%", "50% ריחמ");

    /* Mixed: Latin runs keep their order, line follows the first strong letter. */
    check("עופר לוי - אני חוזר (Buskilaz Live Remix)",
          "(Buskilaz Live Remix) רזוח ינא - יול רפוע");
    check("Remix by שמעון אלבז", "Remix by זבלא ןועמש");

    /* Brackets mirror inside RTL text. */
    check("שיר (רמיקס)", "(סקימר) ריש");

    /* N0: a bracket pair stays a pair across directions. */
    check("עופר לוי - אני חוזר (Buskilaz Live Remix).mp3",
          "mp3.(Buskilaz Live Remix) רזוח ינא - יול רפוע");
    check("Song (רמיקס)", "Song (סקימר)");
    check("שיר [Extended Mix] (Edit)", "(Edit) [Extended Mix] ריש");
    check("Artist - Song (Remix) [2024]", "Artist - Song (Remix) [2024]");
    check("שיר (לא סגור", "רוגס אל) ריש");

    /* Niqqud stays after its letter. */
    check("שָׁלוֹם", "םוֹלשָׁ");

    /* Invisible formatting characters (as in the USB's titles) steer but are not drawn. */
    check("\u200e\u2068אודיה - עובד בכפר (Buskilaz Remix)\u2069",
          "רפכב דבוע - הידוא (Buskilaz Remix)");                  /* LRM first: an LTR line */
    check("\u2068עומר אדם - ידיים למעלה\u2069", "הלעמל םיידי - םדא רמוע");
    check("ILAY ATTIAS  \u202c\u200eX MYRØ", "ILAY ATTIAS  X MYRØ");
    check("\u200fשיר\ufeff", "ריש");

    /* Trailing spaces stay at the end. */
    check("שלום  ", "םולש  ");

    /* Bad input is refused, so the caller draws it as is. */
    char out[16];
    if (heb_bidi_visual("\xff\xfe", out, sizeof out) != -1) { puts("FAIL invalid utf8"); fails++; }
    if (heb_bidi_visual("שלום", out, 4) != -1) { puts("FAIL small buffer"); fails++; }

    if (!heb_bidi_has_rtl("שיר") || heb_bidi_has_rtl("Café Del Mar")) { puts("FAIL has_rtl"); fails++; }

    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
