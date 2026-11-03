/* SPDX-License-Identifier: GPL-3.0-only */
/* Visual reordering of Hebrew/Latin text for a renderer that draws left to right.
 *
 * Implements the parts of the Unicode Bidirectional Algorithm (UAX #9) that track,
 * artist and folder names need: one paragraph, no explicit embeddings or isolates,
 * Hebrew and Arabic letters as R, digits and their separators/terminators, neutral
 * resolution, trailing-whitespace reset and bracket mirroring. Combining marks
 * (niqqud, cantillation) stay after their base letter in the output, which is the
 * order a left-to-right glyph renderer needs. */
#ifndef HEB_BIDI_H
#define HEB_BIDI_H

#include <stddef.h>
#include <stdint.h>

/* Nonzero if the UTF-8 string contains a right-to-left letter. Cheap; use it to skip
 * the reorder for plain Latin text. */
int heb_bidi_has_rtl(const char *s);

/* Nonzero for an invisible formatting character: the direction marks and the embedding
 * and isolate controls, zero-width spaces and joiners, the word joiner and the byte order
 * mark. Tools wrap Hebrew titles in them; the deck's fonts have no glyph for them. */
int heb_bidi_invisible(uint32_t c);

/* Nonzero if the line reads right to left: its first strong letter is Hebrew or Arabic
 * (UAX #9 P2/P3). Such a line starts at its right end, so it is cut on the left. */
int heb_bidi_base_rtl(const char *s);

/* Writes the visual-order form of `in` (UTF-8, NUL-terminated) to `out`, NUL-terminated.
 * Invisible formatting characters (heb_bidi_invisible) steer the order but are left out of
 * the output. Returns the number of bytes written without the NUL, or -1 if `out` is too small
 * (the output is never longer than the input), the input is longer than
 * HEB_BIDI_MAX code points, or it is not valid UTF-8. On -1 draw the input as is. */
#define HEB_BIDI_MAX 1024
int heb_bidi_visual(const char *in, char *out, size_t outsz);

#endif
