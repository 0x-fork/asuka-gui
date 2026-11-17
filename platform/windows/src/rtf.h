// rtf.h: the Rich Text Format a rich edit control (RICHEDIT50W) takes and
// gives: styled runs of text, with bold, italic, underline,
// strikethrough, a monospaced font, colors and HYPERLINK fields (the
// control's friendly links). It's a Rich TextField's way in and out of
// the control (richfield.c). Plain C, without Windows: its test
// (test/rtf_test.c) runs on every system.
#ifndef ASUKA_WIN_RTF_H
#define ASUKA_WIN_RTF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint8_t r, g, b;
} rtf_color;

// A run of styled text, UTF-8; a "\n" in it ends a paragraph.
typedef struct {
	char *text;
	size_t len;
	bool bold, italic, underline, strike, mono;
	bool has_color, has_bg;  // else the document's color, no background
	rtf_color color, bg;
	char *link;  // NULL: none
	float size;  // points (0: the document's)
} rtf_run;

typedef struct {
	rtf_run *v;
	uint32_t n, cap;
} rtf_runs;

// The document's defaults: its font, its monospaced font (UTF-8), its size
// in points and its color.
typedef struct {
	const char *face, *mono_face;
	float size;
	rtf_color color;
} rtf_doc;

typedef struct {
	char *p;
	size_t n, cap;
} rtf_buf;

// rtf_write writes runs as RTF, in ASCII (\u escapes for the rest), NUL
// terminated (out->n doesn't count it). Free out->p.
void rtf_write(const rtf_run *runs, uint32_t n, const rtf_doc *d, rtf_buf *out);

// rtf_parse reads RTF into runs, a run per stretch of one style;
// paragraphs end in "\n" (but the last: a document's final \par is its
// end). Text in the document's color has no color of its own. Free them
// with rtf_runs_free.
void rtf_parse(const char *data, size_t len, const rtf_doc *d, rtf_runs *out);

// rtf_trim cuts runs to n UTF-16 units of text, dropping the empty ones
// at the end.
void rtf_trim(rtf_runs *r, size_t n);

// rtf_utf16_len is how many UTF-16 units UTF-8 text is.
size_t rtf_utf16_len(const char *s, size_t len);

void rtf_runs_free(rtf_runs *r);

#endif
