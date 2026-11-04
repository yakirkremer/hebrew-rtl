/* SPDX-License-Identifier: GPL-3.0-only
 *
 * hebrew-rtl: Hebrew track, artist and folder names read right to left on the XDJ-AZ.
 *
 * EP147 lays out every line of text it draws (drawText, drawFittedText, labels) through
 * juce::GlyphArrangement::addCurtailedLineOfText, which places the characters strictly
 * left to right. This mod hooks that one function: when the line holds a Hebrew (or
 * Arabic) letter, it hands EP147 the same text in visual order (bidi.c), so the glyphs
 * come out right to left. Lines without one pass straight through.
 *
 * The letters themselves come from the deck fonts with Hebrew glyphs added
 * (tools/merge_hebrew.py); without them the reordered text still shows as boxes.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "azmod-mod.h"
#include "bidi.h"
#include "glyphs.h"
#include "kbd_icons.h"

#define MOD_ID "hebrew-rtl"

/* juce::GlyphArrangement::addCurtailedLineOfText(const Font&, const String& text,
 * float xOffset, float yOffset, float maxWidthPixels, bool useEllipsis) on
 * ep147-9e9454e357389ece (XDJ-AZ 2.00 / AZ-MODS 2.01). Found as the function that calls
 * both insertEllipsis (the only builder of String("..")) and Font::getGlyphPositions. */
#define ADDR_ADD_CURTAILED_LINE 0x2554ee0u
/* juce::Font::getGlyphPositions(const String&, Array<int>& glyphs, Array<float>& xOffsets)
 * const: the call addCurtailedLineOfText makes first, with the same font and text. */
#define ADDR_FONT_GLYPH_POSITIONS 0x25548b0u
/* juce::Graphics::drawText(const String&, Rectangle<float> area, Justification, bool
 * useEllipses) const, area and justification passed by reference: lays the text out with
 * addCurtailedLineOfText at area's width, then justifies and draws it. EP147's browse
 * rows pass an area as wide as the whole text and let the row's clip cut it. */
#define ADDR_GRAPHICS_DRAW_TEXT 0x2557600u
/* LowLevelGraphicsContext::getClipBounds() const, vtable slot 12: drawText calls slot 11
 * (clipRegionIntersects, +0x58) and slot 28 (getFont, +0xe0) of the same table. */
#define CTX_SLOT_GET_CLIP_BOUNDS 12
/* juce::GlyphArrangement::addFittedText(const Font&, const String&, float x, float y,
 * float width, float height, Justification, int maximumLines, float minimumHorizontalScale):
 * lays the line out unbounded, then for one line too wide calls fitLineIntoSpace, which
 * cuts the right end behind "...". The deck strips' track titles come this way. */
#define ADDR_ADD_FITTED_TEXT 0x25583f0u
/* juce::GlyphArrangement::addLineOfText(const Font&, const String&, float x, float y): a
 * thunk into addCurtailedLineOfText with no width limit. juce::TextEditor::drawContent
 * (0x25eccd0, the search field's paint) lays out each word ("atom") with it at the x its
 * left-to-right Iterator gives, so a Hebrew query shows its words in the wrong order. */
#define ADDR_ADD_LINE_OF_TEXT 0x25552a0u
/* drawContent's own frame: x29 is its sp after the prologue, its Iterator lives at +0xd0,
 * and the atom's text (atom->getTrimmedText(), returned through x8) is the String at +0xb8
 * that it passes to addLineOfText. That address identifies the call. */
#define DRAW_CONTENT_RETURN   0x25ed250u
#define DRAW_CONTENT_ITERATOR 0xd0
#define DRAW_CONTENT_TEXT     0xb8
/* juce::TextEditor::Iterator, from its constructor (0x2659120) and next() (0x2658b40). */
#define IT_ATOM_X     0x14  /* float: where the current atom starts */
#define IT_ATOM       0x20  /* const TextAtom*: &section->atoms[atom_index - 1] */
#define IT_SECTION    0x28  /* const UniformTextSection* */
#define IT_SECTIONS   0x30  /* const OwnedArray<UniformTextSection>*: data +0, size +0x10 */
#define IT_WRAP_WIDTH 0x48  /* float */
/* UniformTextSection: Array<TextAtom> atoms at +0x10 (data +0x10, size +0x20). TextAtom:
 * String atomText +0, float width +8, uint16 numChars +0xc, 16 bytes. */
#define SECTION_ATOMS     0x10
#define SECTION_NUM_ATOMS 0x20
#define ATOM_SIZE         16
#define ATOM_WIDTH        8
#define EDITOR_MAX_ATOMS  128
/* juce::CustomTypeface::getStringWidth(const String&) and getGlyphPositions(const String&,
 * Array<int>&, Array<float>&): every piece of text is measured or laid out through one of
 * them (FreeTypeTypeface's vtable, slots 6 and 7), and both look characters up with an
 * inlined findGlyph that loads a missing one from the font, or a box when the font has none. */
#define ADDR_TYPEFACE_STRING_WIDTH    0x2553220u
#define ADDR_TYPEFACE_GLYPH_POSITIONS 0x2553570u
/* gui::SoftwareKeyboardWidgetController::EventSender::sendCharacter(const juce::String&, int):
 * the search keyboard's two key handlers (0x1dc7310, 0x1dc7450) call it with the text of the
 * key that was pressed, which 0x1dc1bd8 looks up by the key's component name. */
#define ADDR_KBD_SEND_CHARACTER 0x1dc71d0u
/* gui::SearchSoftwareKeyboardWidgetController: its constructor (made once, when the search
 * screen first opens) and its two destructors. The controller keeps its keyboard widget at
 * +0x28 (its setVisible, 0x1dc5da8, calls the widget's), and a juce::Component keeps its
 * visible flag in bit 1 of the byte at +0xc0 (juce::Component::setVisible, 0x260af90). The
 * keyboard starts out visible without a setVisible call and the deck hides it while the
 * results list has the knob, so the flag itself is read. */
#define ADDR_SEARCH_KBD_CTOR      0x1dc17d8u
#define ADDR_SEARCH_KBD_DTOR      0x1dc16f0u
#define ADDR_SEARCH_KBD_DTOR_DEL  0x1dc1718u
#define KBD_CONTROLLER_WIDGET     0x28
#define COMPONENT_FLAGS           0xc0
#define COMPONENT_VISIBLE_BIT     0x02
#define COMPONENT_NAME            0x08    /* juce::String componentName */
#define COMPONENT_PARENT          0x18    /* Component* parentComponent */
/* The keyboard widget keeps its keys in juce::Arrays (as its setEnabled, 0x1dc2f10, walks
 * them): upper case, lower case, two digit/symbol sets. The lower-case keys, named "q".."m",
 * show only while the letter layout does (123 hides their page, ABC shows it again). */
#define KBD_WIDGET_LOWER_KEYS     0x140   /* Array<Component*>: data, then the count at +0x10 */
/* The controller's ISoftwareKeyboardWidget::IListener base sits at +0x08 and its
 * EventSender at +0x48 from there (the key handler 0x1dc7450 passes this+0x48 to
 * sendCharacter): a Hebrew key of our own sends its letter through it. */
#define KBD_CONTROLLER_SENDER     0x50

/* A juce::String is one pointer to its UTF-8 text, which sits 16 bytes into a
 * StringHolder { int refCount; size_t allocatedNumBytes; char text[]; }. EP147 frees a
 * holder when its count drops below zero; ours starts so high that it never does, and
 * addCurtailedLineOfText only reads the text during the call, so one buffer per thread
 * is enough. */
#define LINE_MAX_BYTES 4096
struct juce_string_holder {
    int ref_count;
    size_t allocated;
    char text[LINE_MAX_BYTES + 1];
};
_Static_assert(offsetof(struct juce_string_holder, text) == 16, "JUCE 5 StringHolder layout");

/* A juce::Array (JUCE 5.3): the element block from malloc, its capacity, then the count,
 * each padded to 8 bytes; addCurtailedLineOfText zeroes two of them at sp+0xb0 and +0xc8. */
struct juce_array {
    void *data;
    int num_allocated;
    int pad0;
    int num_used;
    int pad1;
};
_Static_assert(sizeof(struct juce_array) == 24 && offsetof(struct juce_array, num_used) == 16,
               "JUCE 5.3 Array layout");

typedef void (*add_line_fn)(void *arrangement, const void *font, char *const *text,
                            float x, float y, float max_width, bool ellipsis);
typedef void (*add_fitted_fn)(void *arrangement, const void *font, char *const *text,
                              float x, float y, float width, float height,
                              const int *justification, int max_lines, float min_scale);
typedef void (*draw_text_fn)(void *graphics, char *const *text, const float *area,
                             const int *justification, bool ellipsis);
/* juce::Rectangle<int> has a user-provided copy constructor, so C++ returns it through
 * the hidden result pointer in x8, not in x0/x1. A C struct over 16 bytes is returned
 * the same way; the padding is only there to make the compiler pass x8. */
struct rect_i { int x, y, w, h; char pad[16]; };
typedef struct rect_i (*clip_bounds_fn)(void *context);
typedef void (*glyph_positions_fn)(const void *font, char *const *text,
                                   struct juce_array *glyphs, struct juce_array *x_offsets);

static const az_core_v1 *C;
static add_line_fn orig_add_line;
static draw_text_fn orig_draw_text;
typedef void (*add_line_of_text_fn)(void *arrangement, const void *font, char *const *text,
                                    float x, float y);
static add_line_of_text_fn orig_add_line_of_text;
static add_fitted_fn orig_add_fitted;
typedef float (*string_width_fn)(void *typeface, char *const *text);
typedef void (*typeface_positions_fn)(void *typeface, char *const *text, void *glyphs,
                                      void *x_offsets);
static string_width_fn orig_string_width;
static typeface_positions_fn orig_typeface_positions;
static int glyphs_on = 1;
typedef void (*send_character_fn)(void *sender, char *const *text, int arg);
static send_character_fn orig_send_character;
static int kbd_hebrew;              /* the search keyboard types Hebrew; read on the UI thread */
static int kbd_hooked;
static az_surface *globe_key;
typedef void (*kbd_ctor_fn)(void *controller, void *arg);
typedef void (*kbd_dtor_fn)(void *controller);
static kbd_ctor_fn orig_kbd_ctor;
static kbd_dtor_fn orig_kbd_dtor, orig_kbd_dtor_del;
static void *kbd_controller;        /* the live search keyboard controller, or NULL */
static int globe_shown;
/* Set by the outer hooks (drawText, addFittedText) around a right-to-left line: the
 * width the line really has, which addCurtailedLineOfText is not told, and whether a cut
 * should show "...". line_limit is -1 when no outer hook knows better. */
static __thread float line_limit = -1.0f;
static __thread bool line_ellipsis;
static const glyph_positions_fn glyph_positions = (glyph_positions_fn)ADDR_FONT_GLYPH_POSITIONS;
static int enabled = 1;

static void holder_set(struct juce_string_holder *h)
{
    h->ref_count = 0x3fffffff;
    h->allocated = sizeof h->text;
}

/* Lays out `h`'s text in `font` the way EP147 does: xs[0..n] are the left edges of the
 * n glyphs and the line's right end. Returns n, or -1 if it does not fit in xs. */
static int measure(const void *font, struct juce_string_holder *h, float *xs, int cap)
{
    struct juce_array glyphs = { 0 }, offsets = { 0 };
    char *text = h->text;
    holder_set(h);
    glyph_positions(font, &text, &glyphs, &offsets);
    int n = glyphs.num_used;
    int ok = n >= 0 && offsets.num_used == n + 1 && n + 1 <= cap;
    if (ok) memcpy(xs, offsets.data, (size_t)(n + 1) * sizeof(float));
    free(glyphs.data);              /* juce::HeapBlock allocates with malloc */
    free(offsets.data);
    return ok ? n : -1;
}

static int utf8_count(const char *s)
{
    int n = 0;
    for (; *s; s++) n += (*s & 0xC0) != 0x80;
    return n;
}

static const char *utf8_skip(const char *s, int k)
{
    while (*s && k-- > 0)
        do s++; while ((*s & 0xC0) == 0x80);
    return s;
}

/* EP147 drops what does not fit at the right end of a line, which is where a
 * right-to-left line starts. For such a line, drop from the left end instead, behind
 * "..." when EP147 asked for an ellipsis, so the start of the name stays visible. */
static void cut_left(const void *font, struct juce_string_holder *h, float max_width, bool ellipsis)
{
    static __thread float xs[HEB_BIDI_MAX + 1];
    static __thread struct juce_string_holder dots;
    int n = measure(font, h, xs, HEB_BIDI_MAX + 1);
    if (n <= 0 || n != utf8_count(h->text) || xs[n] <= max_width + 1.0f) return;

    float dots_width = 0.0f;
    if (ellipsis) {
        float dx[4];
        strcpy(dots.text, "...");
        if (measure(font, &dots, dx, 4) == 3) dots_width = dx[3];
    }
    int k = 0;
    while (k < n && xs[n] - xs[k] + dots_width > max_width) k++;
    const char *rest = utf8_skip(h->text, k);
    while (*rest == ' ') rest++;

    size_t len = strlen(rest), pre = dots_width > 0.0f ? 3 : 0;
    if (pre + len > LINE_MAX_BYTES) return;
    memmove(h->text + pre, rest, len + 1);
    memcpy(h->text, "...", pre);
}

static void hook_add_line(void *arrangement, const void *font, char *const *text,
                          float x, float y, float max_width, bool ellipsis)
{
    const char *s = text ? *text : NULL;
    if (enabled && s && *s && heb_bidi_has_rtl(s)) {
        static __thread struct juce_string_holder holder;
        if (heb_bidi_visual(s, holder.text, sizeof holder.text) >= 0) {
            float limit = max_width;
            if (line_limit >= 0.0f && line_limit < limit) limit = line_limit;
            if (limit < 1.0e9f && heb_bidi_base_rtl(s))
                cut_left(font, &holder, limit, ellipsis || line_ellipsis);
            holder_set(&holder);
            char *visual = holder.text;
            orig_add_line(arrangement, font, &visual, x, y, max_width, ellipsis);
            return;
        }
    }
    orig_add_line(arrangement, font, text, x, y, max_width, ellipsis);
}

static uint32_t text_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

/* A selected browse row too narrow for its text scrolls it as a ticker: each frame
 * draws the line twice, at x = -offset and again one line width plus a gap to the
 * right, while the row's clip stays put. The copy left of the clip marks a ticker; the
 * next draw of the same line further right is its second copy. */
static __thread struct { uint32_t hash; float x, w; bool valid; } ticker;

static void hook_draw_text(void *graphics, char *const *text, const float *area,
                           const int *justification, bool ellipsis)
{
    const char *s = text ? *text : NULL;
    float saved = line_limit;
    float mirrored[4];
    if (enabled && s && *s && heb_bidi_base_rtl(s)) {
        void *context = *(void **)graphics;
        clip_bounds_fn clip_bounds = (*(clip_bounds_fn **)context)[CTX_SLOT_GET_CLIP_BOUNDS];
        struct rect_i clip = clip_bounds(context);
        bool copy = false;
        if ((float)clip.w < area[2]) {
            uint32_t hash = text_hash(s);
            if (area[0] < (float)clip.x - 0.5f) {
                ticker.hash = hash;
                ticker.x = area[0];
                ticker.w = area[2];
                ticker.valid = true;
                copy = true;
            } else if (ticker.valid && ticker.hash == hash && ticker.w == area[2] &&
                       area[0] > ticker.x) {
                ticker.valid = false;
                copy = true;
            }
        }
        if (copy) {
            /* Mirror both copies about the clip, so the ticker starts at the right end,
             * where a right-to-left line starts, and moves right, the second copy
             * following in from the left. */
            memcpy(mirrored, area, sizeof mirrored);
            mirrored[0] = 2.0f * (float)clip.x + (float)clip.w - area[0] - area[2];
            area = mirrored;
        } else {
            /* At rest: keep what fits from the right end, where the line starts. */
            float visible = (float)(clip.x + clip.w) - area[0];
            if (visible < area[2]) line_limit = visible > 0.0f ? visible : 0.0f;
        }
    }
    orig_draw_text(graphics, text, area, justification, ellipsis);
    line_limit = saved;
}

static void hook_add_fitted(void *arrangement, const void *font, char *const *text,
                            float x, float y, float width, float height,
                            const int *justification, int max_lines, float min_scale)
{
    const char *s = text ? *text : NULL;
    float saved_limit = line_limit;
    bool saved_ellipsis = line_ellipsis;
    /* One line: cut it to the width from the left behind "...", so it already fits and
     * fitLineIntoSpace has nothing to cut. Wrapped text is left to EP147. */
    if (enabled && s && *s && max_lines <= 1 && heb_bidi_base_rtl(s)) {
        line_limit = width;
        line_ellipsis = true;
    }
    orig_add_fitted(arrangement, font, text, x, y, width, height, justification, max_lines,
                    min_scale);
    line_limit = saved_limit;
    line_ellipsis = saved_ellipsis;
}

enum { ATOM_NEUTRAL, ATOM_LTR, ATOM_RTL };

static int atom_class(const char *s)
{
    if (heb_bidi_has_rtl(s)) return ATOM_RTL;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') || c >= 0xC0)
            return ATOM_LTR;
    }
    return ATOM_NEUTRAL;
}

/* The x a right-to-left line puts this atom at, or `x` when the line is not one the
 * search field draws plainly: one section, one line, only its own atoms. Words keep their
 * own letters' order (hook_add_line handles that); here the words themselves are ordered
 * the bidi way, with runs of left-to-right words (Latin, numbers) kept in their order. */
static float editor_atom_x(const unsigned char *it, float x)
{
    const unsigned char *atom = *(const unsigned char *const *)(it + IT_ATOM);
    const unsigned char *section = *(const unsigned char *const *)(it + IT_SECTION);
    const unsigned char *sections = *(const unsigned char *const *)(it + IT_SECTIONS);
    if (!atom || !section || !sections || *(const int *)(sections + 0x10) != 1 ||
        **(const unsigned char *const *const *)sections != section)
        return x;
    const unsigned char *atoms = *(const unsigned char *const *)(section + SECTION_ATOMS);
    int n = *(const int *)(section + SECTION_NUM_ATOMS);
    if (!atoms || n < 1 || n > EDITOR_MAX_ATOMS || atom < atoms ||
        atom >= atoms + (size_t)n * ATOM_SIZE || (size_t)(atom - atoms) % ATOM_SIZE)
        return x;
    int k = (int)((size_t)(atom - atoms) / ATOM_SIZE);

    unsigned char level[EDITOR_MAX_ATOMS];
    int cls[EDITOR_MAX_ATOMS], order[EDITOR_MAX_ATOMS];
    float width[EDITOR_MAX_ATOMS], total = 0.0f;
    int first_strong = ATOM_NEUTRAL;
    for (int i = 0; i < n; i++) {
        const char *t = *(const char *const *)(atoms + (size_t)i * ATOM_SIZE);
        if (!t || *t == '\n' || *t == '\r') return x;
        cls[i] = atom_class(t);
        if (first_strong == ATOM_NEUTRAL) first_strong = cls[i];
        width[i] = *(const float *)(atoms + (size_t)i * ATOM_SIZE + ATOM_WIDTH);
        total += width[i];
    }
    if (first_strong != ATOM_RTL || total > *(const float *)(it + IT_WRAP_WIDTH) + 0.5f)
        return x;

    /* Levels: right-to-left words 1, left-to-right words 2, a neutral atom (a space, a dash)
     * 2 only between two left-to-right words. */
    for (int i = 0; i < n; i++) {
        if (cls[i] != ATOM_NEUTRAL) { level[i] = cls[i] == ATOM_LTR ? 2 : 1; continue; }
        int before = ATOM_RTL, after = ATOM_RTL;
        for (int j = i - 1; j >= 0; j--) if (cls[j] != ATOM_NEUTRAL) { before = cls[j]; break; }
        for (int j = i + 1; j < n; j++) if (cls[j] != ATOM_NEUTRAL) { after = cls[j]; break; }
        level[i] = before == ATOM_LTR && after == ATOM_LTR ? 2 : 1;
    }
    /* Rule L2: reverse each run at level 2 or more, then the whole line. */
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n;) {
        if (level[order[i]] < 2) { i++; continue; }
        int j = i;
        while (j < n && level[order[j]] >= 2) j++;
        for (int a = i, b = j - 1; a < b; a++, b--) { int t = order[a]; order[a] = order[b]; order[b] = t; }
        i = j;
    }
    for (int a = 0, b = n - 1; a < b; a++, b--) { int t = order[a]; order[a] = order[b]; order[b] = t; }

    float start = *(const float *)(it + IT_ATOM_X);
    for (int i = 0; i < k; i++) start -= width[i];
    float vx = start;
    for (int v = 0; v < n && order[v] != k; v++) vx += width[order[v]];
    return vx;
}

static void hook_add_line_of_text(void *arrangement, const void *font, char *const *text,
                                  float x, float y)
{
    if (enabled && (uintptr_t)__builtin_return_address(0) == DRAW_CONTENT_RETURN) {
        /* our own frame record holds the caller's x29: drawContent's frame */
        const unsigned char *frame = *(const unsigned char *const *)__builtin_frame_address(0);
        /* The text this call draws is the String drawContent made in its own frame, and the
         * Iterator there says the atom starts at x: only then is the frame read as such. */
        if (frame && (const unsigned char *)text == frame + DRAW_CONTENT_TEXT &&
            *(const float *)(frame + DRAW_CONTENT_ITERATOR + IT_ATOM_X) == x)
            x = editor_atom_x(frame + DRAW_CONTENT_ITERATOR, x);
    }
    orig_add_line_of_text(arrangement, font, text, x, y);
}

/* Hebrew letters the deck's font lacks come from the built-in font (glyphs.c): they are
 * added to the typeface just before it looks them up, so it never falls back to a box. */
static float hook_string_width(void *typeface, char *const *text)
{
    if (glyphs_on && text) heb_glyphs_ensure(typeface, *text);
    return orig_string_width(typeface, text);
}

static void hook_typeface_positions(void *typeface, char *const *text, void *glyphs,
                                    void *x_offsets)
{
    if (glyphs_on && text) heb_glyphs_ensure(typeface, *text);
    orig_typeface_positions(typeface, text, glyphs, x_offsets);
}

/* The Israeli keyboard layout (SI-1452) on the deck's a..z keys. q and w, which type only
 * punctuation there, give the two final letters the deck has no key for (; and . on a
 * computer): q -> final pe, w -> final tsadi. */
static const char *const HEBREW_KEYS[26] = {
    "ש", "נ", "ב", "ג", "ק", "כ", "ע", "י", "ן", "ח", "ל", "ך", "צ",   /* a..m */
    "מ", "ם", "פ", "ף", "ר", "ד", "א", "ו", "ה", "ץ", "ס", "ט", "ז",   /* n..z */
};

/* One fixed string per letter: sendCharacter keeps a reference to the String it is given
 * (a juce::String copy shares the text) for a callback that may run later, so a buffer
 * reused for the next key could change a letter not yet typed. Filled once in init. */
static struct {
    int ref_count;
    size_t allocated;
    char text[8];
} hebrew_key_text[26];
_Static_assert(offsetof(__typeof__(hebrew_key_text[0]), text) == 16, "JUCE 5 StringHolder layout");

/* In Hebrew mode a letter key types the Hebrew letter in its place. */
static void hook_send_character(void *sender, char *const *text, int arg)
{
    const char *s = text ? *text : NULL;
    if (__atomic_load_n(&kbd_hebrew, __ATOMIC_RELAXED) && s && s[0] && !s[1]) {
        char c = s[0];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') {
            char *hebrew = hebrew_key_text[c - 'a'].text;
            orig_send_character(sender, &hebrew, arg);
            return;
        }
    }
    orig_send_character(sender, text, arg);
}

/* The language keys. The deck's letter keys show images of their Latin letters, made when
 * the keyboard is built, so they cannot be relabelled. Instead:
 *   The globe: one key of our own in the gap between 123 and z, in both languages.
 *   Hebrew:    two surfaces over the letter rows (q..p and a..l; z..m), with the 26 keys in
 *              the deck's places and sizes showing their Hebrew letters (kbd_icons.h). They
 *              only paint: with no on_input the core makes them input-transparent, so a tap
 *              reaches the deck's own key underneath, which types on EP147's message thread
 *              and hook_send_character turns the letter Hebrew there. Typing from the core's
 *              UI thread instead would edit the search field while EP147 paints it.
 * All show only while the search keyboard does. Geometry: the deck's keys, in panel pixels. */
#define GLOBE_KEY_X 165
#define GLOBE_KEY_Y 707
#define KEY_W 90
#define KEY_H 74
#define KEY_PITCH 100
#define ROW1_X 145                  /* q..p */
#define ROW2_X 195                  /* a..l */
#define ROW3_X 295                  /* z..m */
#define ROW1_Y 538
#define ROW2_Y 622
#define ROW3_Y 706
#define HEB_TOP_X 140               /* the surface over q..p and a..l, margins included */
#define HEB_TOP_Y 533
#define HEB_TOP_W 1000
#define HEB_TOP_H 168
#define HEB_ROW3_X 290              /* the one over z..m, clear of the globe */
#define HEB_ROW3_Y 701
#define HEB_ROW3_W 700
#define HEB_ROW3_H 84
/* Colors. The deck draws its keyboard in neutral grays and the Themes mod maps them (Day,
 * AZ-MODS, Midnight, ...); the core's fill_rect passes our colors through the same map, so
 * giving it the deck's own stock grays makes the Hebrew keys match the deck in every theme.
 * Stock grays found by inverting the AZ-MODS theme's ramp on sampled pixels. Images are not
 * themed, so the letters are tinted with the theme's text color instead (tint_keys). */
#define STOCK_KBD_BG      0x222222u
#define STOCK_CHECK_DARK  0x222222u   /* key faces: a 4 px checkerboard of two grays */
#define STOCK_CHECK_LIGHT 0x313131u
#define STOCK_KEY_MARGIN  0x121212u   /* the dark 5 px margin around each enabled key */
#define STOCK_KEY_DOWN    0x4a4a4au
#define STOCK_KEY_OFF     0x202020u   /* a disabled key (no source): flat */
#define KEY_MARGIN        5
#define DIM_PERCENT       29          /* a disabled key's letter: 29 % text over background */
/* juce::Component::isEnabled() const: false while the deck has disabled the keyboard. */
#define ADDR_COMPONENT_IS_ENABLED 0x25ab8c0u
typedef bool (*component_is_enabled_fn)(const void *component);
static const component_is_enabled_fn component_is_enabled =
    (component_is_enabled_fn)ADDR_COMPONENT_IS_ENABLED;

static int kbd_enabled = 1;         /* the deck's keyboard is enabled; UI thread only */
/* The letters and globe faces, tinted for the current theme and state (NULL: untinted). */
static unsigned char *tinted_letters, *tinted_globe_en, *tinted_globe_he;
static uint32_t tint_rgb = 0xffffffffu;

/* White-on-transparent premultiplied RGBA recolored to rgb (alpha kept). */
static void tint(unsigned char *dst, const unsigned char *src, size_t bytes, uint32_t rgb)
{
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    for (size_t i = 0; i < bytes; i += 4) {
        unsigned a = src[i + 3];
        dst[i] = (unsigned char)(r * a / 255);
        dst[i + 1] = (unsigned char)(g * a / 255);
        dst[i + 2] = (unsigned char)(b * a / 255);
        dst[i + 3] = (unsigned char)a;
    }
}

/* Called from tick: re-tint when the theme or the enabled state changes the ink.
 * Returns nonzero when it did. */
static int tint_keys(void)
{
    uint32_t text = C->color(AZ_COL_TEXT) & 0xffffffu, bg = C->color(AZ_COL_BG) & 0xffffffu;
    uint32_t ink = text;
    if (!kbd_enabled) {
        ink = 0;
        for (int sh = 0; sh <= 16; sh += 8) {
            unsigned t = (text >> sh) & 255, k = (bg >> sh) & 255;
            ink |= (uint32_t)((t * DIM_PERCENT + k * (100 - DIM_PERCENT)) / 100) << sh;
        }
    }
    if (ink == tint_rgb || !tinted_letters || !tinted_globe_en || !tinted_globe_he) return 0;
    tint(tinted_letters, kbd_letters, sizeof kbd_letters, ink);
    tint(tinted_globe_en, kbd_face_en, sizeof kbd_face_en, ink);
    tint(tinted_globe_he, kbd_face_he, sizeof kbd_face_he, ink);
    tint_rgb = ink;
    return 1;
}

/* A key: margin, then the checkerboard face (or the flat pressed / disabled face). */
static void paint_face(az_draw *d, int x, int y, int w, int h, int down)
{
    if (!kbd_enabled) {
        C->fill_rect(d, x, y, w, h, STOCK_KEY_OFF);
        return;
    }
    C->fill_rect(d, x - KEY_MARGIN, y - KEY_MARGIN, w + 2 * KEY_MARGIN, h + 2 * KEY_MARGIN,
                 STOCK_KEY_MARGIN);
    if (down) {
        C->fill_rect(d, x, y, w, h, STOCK_KEY_DOWN);
        return;
    }
    C->fill_rect(d, x, y, w, h, STOCK_CHECK_DARK);
    /* the deck's phase: a light square starts 2 px before the key's top left */
    for (int cy = -2, row = 0; cy < h; cy += 4, row++)
        for (int cx = -2 + 4 * (row & 1), col = 0; cx < w; cx += 8, col++) {
            int x0 = cx < 0 ? 0 : cx, y0 = cy < 0 ? 0 : cy;
            int x1 = cx + 4 > w ? w : cx + 4, y1 = cy + 4 > h ? h : cy + 4;
            if (x1 > x0 && y1 > y0) C->fill_rect(d, x + x0, y + y0, x1 - x0, y1 - y0, STOCK_CHECK_LIGHT);
        }
}

static const char ROW1[] = "qwertyuiop", ROW2[] = "asdfghjkl", ROW3[] = "zxcvbnm";
static az_surface *heb_top, *heb_row3;
static int heb_shown;
static int key_down = -1;           /* the key under the finger: 'a'..'z', 'G' = globe */
/* The first contact, as the touch filter saw it (bus thread): 0 when up, else
 * 1 << 31 | y << 16 | x. It only watches; every touch goes on to EP147. */
static unsigned touch_seen;

static void toggle_language(void)
{
    int he = !__atomic_load_n(&kbd_hebrew, __ATOMIC_RELAXED);
    __atomic_store_n(&kbd_hebrew, he, __ATOMIC_RELAXED);
    C->cfg_set(MOD_ID, "kbd_hebrew", he);
}

static void paint_globe(az_draw *d, int x, int y, int down)
{
    int he = __atomic_load_n(&kbd_hebrew, __ATOMIC_RELAXED);
    const unsigned char *label = he ? (tinted_globe_he ? tinted_globe_he : kbd_face_he)
                                    : (tinted_globe_en ? tinted_globe_en : kbd_face_en);
    paint_face(d, x, y, KBD_KEY_W, KBD_KEY_H, down);
    C->draw_image(d, x, y, label, KBD_KEY_W, KBD_KEY_H);
}

static void globe_paint(az_draw *d, az_rect area, void *user)
{
    (void)user;
    paint_globe(d, area.x, area.y, key_down == 'G');
}

static int globe_input(const az_event *ev, void *user)
{
    (void)user;
    if (!kbd_enabled) return 1;     /* like the deck's keys while there is no source */
    if (ev->kind == AZ_EV_DOWN) key_down = 'G';
    if (ev->kind == AZ_EV_UP) key_down = -1;
    if (ev->kind == AZ_EV_TAP) toggle_language();
    if (ev->kind == AZ_EV_DOWN || ev->kind == AZ_EV_UP || ev->kind == AZ_EV_TAP)
        C->surface_redraw(globe_key);
    return ev->kind == AZ_EV_TAP || ev->kind == AZ_EV_DOWN || ev->kind == AZ_EV_UP ||
           ev->kind == AZ_EV_LONG;
}

/* The letter key ('a'..'z') at a panel position, 'G' for the globe, or -1. */
static int key_at(int px, int py)
{
    static const struct { const char *keys; int x, y; } rows[3] = {
        { ROW1, ROW1_X, ROW1_Y }, { ROW2, ROW2_X, ROW2_Y }, { ROW3, ROW3_X, ROW3_Y },
    };
    if (px >= GLOBE_KEY_X && px < GLOBE_KEY_X + KBD_KEY_W && py >= GLOBE_KEY_Y &&
        py < GLOBE_KEY_Y + KBD_KEY_H)
        return 'G';
    for (int r = 0; r < 3; r++) {
        if (py < rows[r].y || py >= rows[r].y + KEY_H || px < rows[r].x) continue;
        int i = (px - rows[r].x) / KEY_PITCH;
        if ((px - rows[r].x) % KEY_PITCH < KEY_W && i < (int)strlen(rows[r].keys))
            return rows[r].keys[i];
    }
    return -1;
}

static void paint_row(az_draw *d, const char *keys, int x, int y)
{
    for (int i = 0; keys[i]; i++, x += KEY_PITCH) {
        size_t at = (size_t)(keys[i] - 'a') * KBD_LETTER_W * KBD_LETTER_H * 4;
        const unsigned char *letter = (tinted_letters ? tinted_letters : kbd_letters) + at;
        paint_face(d, x, y, KEY_W, KEY_H, key_down == keys[i]);
        C->draw_image(d, x + (KEY_W - KBD_LETTER_W) / 2, y + (KEY_H - KBD_LETTER_H) / 2,
                      letter, KBD_LETTER_W, KBD_LETTER_H);
    }
}

static void heb_top_paint(az_draw *d, az_rect area, void *user)
{
    (void)user;
    C->fill_rect(d, area.x, area.y, area.w, area.h, STOCK_KBD_BG);
    paint_row(d, ROW1, ROW1_X, ROW1_Y);
    paint_row(d, ROW2, ROW2_X, ROW2_Y);
}

static void heb_row3_paint(az_draw *d, az_rect area, void *user)
{
    (void)user;
    C->fill_rect(d, area.x, area.y, area.w, area.h, STOCK_KBD_BG);
    paint_row(d, ROW3, ROW3_X, ROW3_Y);
}

static int watch_touch(const az_touch *t, void *user)
{
    (void)user;
    unsigned v = 0;
    if (t->down && t->x >= 0 && t->x < 0x8000 && t->y >= 0 && t->y < 0x8000)
        v = 1u << 31 | (unsigned)t->y << 16 | (unsigned)t->x;
    __atomic_store_n(&touch_seen, v, __ATOMIC_RELAXED);
    return AZ_TOUCH_PASS;
}

/* The globe key shows and hides with the search keyboard: the controller is remembered
 * from its constructor until its destructor. */
static void hook_kbd_ctor(void *controller, void *arg)
{
    orig_kbd_ctor(controller, arg);
    __atomic_store_n(&kbd_controller, controller, __ATOMIC_RELEASE);
}

static void forget_controller(void *controller)
{
    void *expected = controller;
    __atomic_compare_exchange_n(&kbd_controller, &expected, NULL, false, __ATOMIC_ACQ_REL,
                                __ATOMIC_RELAXED);
}

static void hook_kbd_dtor(void *controller)
{
    forget_controller(controller);
    orig_kbd_dtor(controller);
}

static void hook_kbd_dtor_del(void *controller)
{
    forget_controller(controller);
    orig_kbd_dtor_del(controller);
}

/* The deck shows its lower-case letter layout: the first lower-case key, "q", and each
 * parent up to the keyboard widget visible. Anything unexpected reads as "not showing", so
 * the Hebrew keys stay out of the deck's way. */
static int letter_layout_showing(const char *widget)
{
    const char *base = widget + KBD_WIDGET_LOWER_KEYS;
    const char *const *keys = *(const char *const *const *)base;
    int n = *(const int *)(base + 0x10);
    if (!keys || n <= 0 || n > 64 || !keys[0]) return 0;
    const char *name = *(const char *const *)(keys[0] + COMPONENT_NAME);
    if (!name || name[0] != 'q' || name[1]) return 0;
    const char *c = keys[0];
    for (int depth = 0; c && c != widget; depth++) {
        if (depth > 8 || !(((const unsigned char *)c)[COMPONENT_FLAGS] & COMPONENT_VISIBLE_BIT))
            return 0;
        c = *(const char *const *)(c + COMPONENT_PARENT);
    }
    return c == widget;
}

static int search_keyboard_visible(void)
{
    const char *ctl = __atomic_load_n(&kbd_controller, __ATOMIC_ACQUIRE);
    if (!ctl) return 0;
    const unsigned char *widget = *(const unsigned char *const *)(ctl + KBD_CONTROLLER_WIDGET);
    return widget && (widget[COMPONENT_FLAGS] & COMPONENT_VISIBLE_BIT);
}

static void redraw_letters(void)
{
    C->surface_redraw(heb_top);
    C->surface_redraw(heb_row3);
}

/* The pressed look for the Hebrew key under the finger. The deck draws its own pressed key
 * under our surface, out of sight, so it is shown from what the touch filter saw. */
static void follow_touch(void)
{
    unsigned t = __atomic_load_n(&touch_seen, __ATOMIC_RELAXED);
    int k = t && kbd_enabled ? key_at((int)(t & 0xffff), (int)((t >> 16) & 0x7fff)) : -1;
    if (k == 'G') return;                   /* the globe key follows its own input */
    if (k < 'a' || k > 'z') k = -1;
    if (k != key_down && key_down != 'G') {
        key_down = k;
        redraw_letters();
    }
}

/* surface_show belongs on the core's UI thread, so the language keys follow the keyboard
 * here: the globe in both languages, the Hebrew letters in Hebrew. */
static void tick(void)
{
    int shown = kbd_hooked && search_keyboard_visible();
    int he = __atomic_load_n(&kbd_hebrew, __ATOMIC_RELAXED);
    if (shown) {
        const char *ctl = __atomic_load_n(&kbd_controller, __ATOMIC_ACQUIRE);
        const void *widget = ctl ? *(const void *const *)(ctl + KBD_CONTROLLER_WIDGET) : NULL;
        int en = widget ? component_is_enabled(widget) : 1;
        int changed = en != kbd_enabled;
        kbd_enabled = en;
        if (tint_keys()) changed = 1;           /* a new theme or state: new ink */
        if (changed) {
            key_down = -1;
            if (globe_key) C->surface_redraw(globe_key);
            if (heb_top) redraw_letters();
        }
    }
    if (shown) {
        const char *ctl = __atomic_load_n(&kbd_controller, __ATOMIC_ACQUIRE);
        const char *widget = ctl ? *(const char *const *)(ctl + KBD_CONTROLLER_WIDGET) : NULL;
        shown = widget && letter_layout_showing(widget);   /* not in the 123 layout */
    }
    int want_globe = shown, want_heb = shown && he && heb_top;
    if (globe_key && want_globe != globe_shown) {
        C->surface_show(globe_key, want_globe);
        globe_shown = want_globe;
    }
    if (heb_top && want_heb != heb_shown) {
        C->surface_show(heb_top, want_heb);
        C->surface_show(heb_row3, want_heb);
        heb_shown = want_heb;
        if (key_down != 'G') key_down = -1;
    }
    if (heb_shown) follow_touch();
}

static const az_surface_v1 GLOBE_KEY = {
    .size = sizeof(az_surface_v1),
    .layer = AZ_LAYER_OVER_EP147,
    .views = AZ_VIEW_BIT(AZ_VIEW_SEARCH),
    .on_paint = globe_paint,
    .on_input = globe_input,
};

/* No on_input: input-transparent, so the deck's keys underneath get the taps. */
static const az_surface_v1 HEB_TOP = {
    .size = sizeof(az_surface_v1),
    .layer = AZ_LAYER_OVER_EP147,
    .views = AZ_VIEW_BIT(AZ_VIEW_SEARCH),
    .on_paint = heb_top_paint,
};

static const az_surface_v1 HEB_ROW3 = {
    .size = sizeof(az_surface_v1),
    .layer = AZ_LAYER_OVER_EP147,
    .views = AZ_VIEW_BIT(AZ_VIEW_SEARCH),
    .on_paint = heb_row3_paint,
};

static int init(const az_core_v1 *core)
{
    C = core;
    enabled = C->cfg_get(MOD_ID, "enabled", 1);
    glyphs_on = C->cfg_get(MOD_ID, "glyphs", 1) && heb_glyphs_init() == 0;
    kbd_hebrew = C->cfg_get(MOD_ID, "kbd_hebrew", 0) != 0;
    for (int i = 0; i < 26; i++) {
        hebrew_key_text[i].ref_count = 0x3fffffff;          /* never freed, as holder_set */
        hebrew_key_text[i].allocated = sizeof hebrew_key_text[i].text;
        strcpy(hebrew_key_text[i].text, HEBREW_KEYS[i]);
    }
    tinted_letters = malloc(sizeof kbd_letters);
    tinted_globe_en = malloc(sizeof kbd_face_en);
    tinted_globe_he = malloc(sizeof kbd_face_he);
    tint_keys();
    /* The globe key, before the hooks: a surface can fail, a claimed hook stays. */
    if (AZ_HAS(C, surface_create) && (globe_key = C->surface_create(MOD_ID, &GLOBE_KEY))) {
        C->surface_place(globe_key, (az_rect){ GLOBE_KEY_X, GLOBE_KEY_Y, KBD_KEY_W, KBD_KEY_H });
        heb_top = C->surface_create(MOD_ID, &HEB_TOP);
        heb_row3 = heb_top ? C->surface_create(MOD_ID, &HEB_ROW3) : NULL;
        if (heb_row3) {
            C->surface_place(heb_top, (az_rect){ HEB_TOP_X, HEB_TOP_Y, HEB_TOP_W, HEB_TOP_H });
            C->surface_place(heb_row3, (az_rect){ HEB_ROW3_X, HEB_ROW3_Y, HEB_ROW3_W, HEB_ROW3_H });
        } else {
            heb_top = NULL;                 /* both or neither; a created one stays hidden */
        }
        /* Only watches (the pressed look); without it the Hebrew keys just don't light up. */
        if (heb_top && AZ_HAS(C, touch_filter)) C->touch_filter(MOD_ID, watch_touch, NULL);
    }
    /* Last, after everything that can fail: a claimed hook stays live until restart. */
    if (C->claim_hook(MOD_ID, ADDR_ADD_CURTAILED_LINE, (void *)hook_add_line,
                      (void **)&orig_add_line) != 0)
        return 1;
    /* Without this one, right-to-left lines still read right; long ones lose their start. */
    if (C->claim_hook(MOD_ID, ADDR_GRAPHICS_DRAW_TEXT, (void *)hook_draw_text,
                      (void **)&orig_draw_text) != 0)
        C->log(MOD_ID, "drawText hook refused: long right-to-left lines cut on the right");
    if (C->claim_hook(MOD_ID, ADDR_ADD_FITTED_TEXT, (void *)hook_add_fitted,
                      (void **)&orig_add_fitted) != 0)
        C->log(MOD_ID, "addFittedText hook refused: fitted right-to-left lines cut on the right");
    if (C->claim_hook(MOD_ID, ADDR_ADD_LINE_OF_TEXT, (void *)hook_add_line_of_text,
                      (void **)&orig_add_line_of_text) != 0)
        C->log(MOD_ID, "addLineOfText hook refused: Hebrew words in the search field read left to right");
    if (glyphs_on &&
        (C->claim_hook(MOD_ID, ADDR_TYPEFACE_STRING_WIDTH, (void *)hook_string_width,
                       (void **)&orig_string_width) != 0 ||
         C->claim_hook(MOD_ID, ADDR_TYPEFACE_GLYPH_POSITIONS, (void *)hook_typeface_positions,
                       (void **)&orig_typeface_positions) != 0)) {
        C->log(MOD_ID, "typeface hooks refused: Hebrew letters show as boxes");
        glyphs_on = 0;
    }
    if (globe_key) {
        /* The controller hooks first: without them the key would float over the results. */
        kbd_hooked =
            C->claim_hook(MOD_ID, ADDR_SEARCH_KBD_DTOR, (void *)hook_kbd_dtor,
                          (void **)&orig_kbd_dtor) == 0 &&
            C->claim_hook(MOD_ID, ADDR_SEARCH_KBD_DTOR_DEL, (void *)hook_kbd_dtor_del,
                          (void **)&orig_kbd_dtor_del) == 0 &&
            C->claim_hook(MOD_ID, ADDR_SEARCH_KBD_CTOR, (void *)hook_kbd_ctor,
                          (void **)&orig_kbd_ctor) == 0 &&
            C->claim_hook(MOD_ID, ADDR_KBD_SEND_CHARACTER, (void *)hook_send_character,
                          (void **)&orig_send_character) == 0;
        if (!kbd_hooked) {
            __atomic_store_n(&kbd_hebrew, 0, __ATOMIC_RELAXED);
            C->log(MOD_ID, "keyboard hooks refused: the search keyboard stays English");
        }
    }
    C->log(MOD_ID, "loaded on %s, enabled=%d, glyphs=%d, keyboard=%s", C->fw_tag ? C->fw_tag() : "?",
           enabled, glyphs_on, !kbd_hooked ? "off" : kbd_hebrew ? "hebrew" : "english");
    return 0;
}

static const char *const FW_TAGS[] = { "ep147-9e9454e357389ece", NULL };

static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = tick,
    .on_frame = NULL,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
