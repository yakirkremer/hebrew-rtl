/* SPDX-License-Identifier: GPL-3.0-only */
/* Hebrew letters for the deck's fonts, which have none (glyphs.c). */
#ifndef HEB_GLYPHS_H
#define HEB_GLYPHS_H

#include <stdbool.h>
#include <stdint.h>

/* Reads the built-in Hebrew font. 0 on success; without it heb_glyphs_add adds nothing. */
int heb_glyphs_init(void);

/* Nonzero for a code point the built-in Hebrew font draws. */
int heb_glyphs_covers(uint32_t c);

/* Adds the letter `c`, drawn from the built-in font and sized to `typeface` (a
 * juce::FreeTypeTypeface), to that typeface's glyph table. False when it did not. */
bool heb_glyphs_add(void *typeface, uint32_t c);

/* Before a juce::CustomTypeface measures or lays out `utf8`: adds every Hebrew letter in it
 * that `typeface` does not have yet. Does nothing unless `typeface` is a FreeTypeTypeface. */
void heb_glyphs_ensure(void *typeface, const char *utf8);

#endif
