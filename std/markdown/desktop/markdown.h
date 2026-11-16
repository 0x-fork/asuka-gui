// markdown.h: what markdown's Windows and Linux C share (markdown.c): the
// text read by md4c (third_party/md4c: CommonMark, with GitHub's tables,
// task lists, strikethrough and autolinks) into styled paragraphs, which
// each platform shows as its own styled text (a label's Pango markup, a
// rich edit's RTF).
#ifndef MARKDOWN_H
#define MARKDOWN_H

#include "markdown.asuka.h"

// md_run is styled text.
typedef struct {
	char *text;
	bool bold, italic, mono, strike;
	bool quote;  // secondary text
	char *link;  // where it goes (NULL: not a link, or one that doesn't open)
} md_run;

// md_para is a paragraph: its runs, how deep in lists and quotes it is,
// its list marker, and what kind of block it's of.
typedef struct {
	md_run *runs;
	size_t nruns, cap;
	int indent;
	char *marker;  // NULL: none
	int heading;   // 1 to 6
	bool code;     // a code block's lines (runs are its text, monospaced)
	bool rule;     // a thematic break
	bool cells;    // a table row: its cells separated by tabs
	bool tight;    // little space after it (a tight list's item)
} md_para;

typedef struct {
	md_para *paras;
	size_t n, cap;
} md_doc;

// markdown_read reads src into paragraphs, in order.
md_doc markdown_read(const char *src);
void markdown_doc_free(md_doc *d);

// markdown_safe_link reports whether a link opens: a web, mail or phone
// address.
bool markdown_safe_link(const char *dest);

#endif
