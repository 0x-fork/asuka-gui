// markdown_windows_test.c: the Windows view (markdown_windows.c, a real
// rich edit) given a reply streamed in frame by frame: each frame writes
// over only the control's text from where the first block that changed
// starts (each write the control is sent, caught as it comes), the text
// before stays as it was, a selection in it survives, and at the end the
// control shows what one given the whole reply at once shows, character
// by character. platform/windows' TestStdViews runs it on Windows.
#include "markdown_windows.c"

#include "markdown_stream_test.h"

#include <wchar.h>

// What the view wrote to the control: the selection each write replaced.
static struct {
	int writes;
	CHARRANGE at;
} seen;

static LRESULT CALLBACK spy(HWND hwnd, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
	if (m == EM_SETTEXTEX || m == EM_REPLACESEL) {
		seen.writes++;
		DefSubclassProc(hwnd, EM_EXGETSEL, 0, (LPARAM)&seen.at);
		if (m == EM_SETTEXTEX && !(((const SETTEXTEX *)w)->flags & ST_SELECTION)) seen.at = (CHARRANGE){0, -1};  // all of it
	}
	return DefSubclassProc(hwnd, m, w, l);
}

// text is the control's text (UTF-16, a paragraph's end a CR), of malloc's.
static wchar_t *text(HWND ctl, int32_t *len) {
	GETTEXTLENGTHEX gl = {GTL_NUMCHARS | GTL_PRECISE, 1200};
	int32_t n = (int32_t)SendMessageW(ctl, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
	wchar_t *s = calloc((size_t)n + 1, sizeof *s);
	GETTEXTEX gt = {(DWORD)((n + 1) * sizeof *s), GT_DEFAULT, 1200, NULL, NULL};
	SendMessageW(ctl, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s);
	*len = (int32_t)wcslen(s);
	return s;
}

static CHARFORMAT2W format_at(HWND ctl, int32_t i) {
	SendMessageW(ctl, EM_SETSEL, i, i + 1);
	CHARFORMAT2W f = {.cbSize = sizeof f};
	SendMessageW(ctl, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&f);
	return f;
}

static int32_t total(const document *v) {
	int32_t n = 0;
	for (size_t i = 0; i < v->n; i++) n += v->blocks[i].len;
	return n;
}

int main(void) {
	HWND top = CreateWindowExW(0, L"STATIC", L"markdown", WS_OVERLAPPEDWINDOW, 0, 0, 600, 800, NULL, NULL, GetModuleHandleW(NULL), NULL);
	md_stream s = stream_load();
	CHECK(s.n > 20, "%zu frames", s.n);
	markdown_Document *d = markdown_make_Document((markdown_Document_events){.parent = top});
	CHECK(d != NULL, "no view");
	document *v = (document *)d;
	SetWindowPos(v->hwnd, NULL, 0, 0, 500, 700, SWP_NOZORDER);
	SetWindowSubclass(v->ctl, spy, 7, 0);
	markdown_list_Paragraph none = {0};
	int32_t replaced = 0, shown = 0, prev_len = 0;
	wchar_t *prev_text = calloc(1, sizeof *prev_text);
	bool selected = false;
	for (size_t f = 0; f < s.n; f++) {
		const markdown_list_Paragraph *prev = f > 0 ? &s.frames[f - 1] : &none;
		size_t first = first_change(prev, &s.frames[f]);
		int32_t from = 0;
		for (size_t i = 0; i < first && i < v->n; i++) from += v->blocks[i].len;
		seen.writes = 0;
		d->vt->set_paragraphs(d, s.frames[f]);
		int32_t len;
		wchar_t *now = text(v->ctl, &len);
		CHECK(v->n == s.frames[f].len, "frame %zu: %zu blocks for %zu paragraphs", f, v->n, s.frames[f].len);
		CHECK(len == total(v), "frame %zu: the control has %d characters, its blocks %d", f, len, total(v));
		// One write at most (none when what changed shows the same: a link's
		// address), from the first change's start or after it, to the
		// control's end (its last paragraph's mark, which no write removes,
		// is one past its text); the text before it as it was.
		CHECK(seen.writes <= 1, "frame %zu: %d writes", f, seen.writes);
		if (first == prev->len && first == s.frames[f].len) CHECK(seen.writes == 0, "frame %zu: nothing changed, %d writes", f, seen.writes);
		if (seen.writes == 1) {
			int32_t at = seen.at.cpMin;
			CHECK(at >= from && (seen.at.cpMax == -1 ? at == 0 : seen.at.cpMax == prev_len + 1),
			      "frame %zu: wrote over [%ld, %ld), want from %d (paragraph %zu's start) or after to %d", f, seen.at.cpMin, seen.at.cpMax,
			      from, first, prev_len + 1);
			CHECK(at <= len && wmemcmp(prev_text, now, (size_t)at) == 0, "frame %zu: the text before the change changed", f);
			replaced += len - at;
		}
		// Every block's links are its paragraph's.
		for (size_t i = 0; i < v->n && i < s.frames[f].len; i++) {
			const markdown_Paragraph *p = &s.frames[f].data[i];
			size_t k = 0;
			for (size_t j = 0; j < p->runs.len; j++) {
				if (!*p->runs.data[j].link) continue;
				CHECK(k < v->blocks[i].nlinks && strcmp(v->blocks[i].links[k].url, p->runs.data[j].link) == 0, "frame %zu: block %zu's link %zu: %s",
				      f, i, k, k < v->blocks[i].nlinks ? v->blocks[i].links[k].url : "(none)");
				k++;
			}
			CHECK(k == v->blocks[i].nlinks, "frame %zu: block %zu has %zu links, its paragraph %zu", f, i, v->blocks[i].nlinks, k);
		}
		shown += len;
		free(prev_text);
		prev_text = now;
		prev_len = len;
		// Once the heading is whole and the next block has begun, part of it
		// is selected, as a reader would while the rest streams in.
		if (!selected && v->n >= 2) {
			selected = true;
			SendMessageW(v->ctl, EM_SETSEL, 2, 6);
		}
	}
	CHARRANGE sel;
	SendMessageW(v->ctl, EM_EXGETSEL, 0, (LPARAM)&sel);
	CHECK(selected && sel.cpMin == 2 && sel.cpMax == 6, "the selection in the heading: [%ld, %ld)", sel.cpMin, sel.cpMax);
	CHECK(replaced * 4 < shown, "replaced %d characters of the %d shown frame by frame", replaced, shown);

	// At the end, what a view given it all at once shows.
	markdown_Document *whole = markdown_make_Document((markdown_Document_events){.parent = top});
	document *w = (document *)whole;
	SetWindowPos(w->hwnd, NULL, 0, 0, 500, 700, SWP_NOZORDER);
	whole->vt->set_paragraphs(whole, s.frames[s.n - 1]);
	int32_t na, nb;
	wchar_t *a = text(v->ctl, &na), *b = text(w->ctl, &nb);
	CHECK(na == nb && wmemcmp(a, b, (size_t)na) == 0, "the text streamed (%d characters) isn't the whole's (%d)", na, nb);
	CHECK(wcsstr(a, L"Moving the cache") == a, "the text: %ls", a);
	int differ = 0;
	for (int32_t i = 0; i < na && i < nb; i++) {
		CHARFORMAT2W x = format_at(v->ctl, i), y = format_at(w->ctl, i);
		if (x.dwEffects != y.dwEffects || x.yHeight != y.yHeight || x.crTextColor != y.crTextColor || x.crBackColor != y.crBackColor ||
		    wcscmp(x.szFaceName, y.szFaceName) != 0) {
			if (differ++ < 5) CHECK(false, "character %d (%lc): its format isn't the whole's", i, a[i]);
		}
	}
	CHECK(differ == 0, "%d characters formatted unlike the whole's", differ);
	float mw, mh;
	d->vt->measure(d, 400, NAN, &mw, &mh);
	CHECK(mw == 400 && mh > 200, "measured %gx%g", mw, mh);
	printf("markdown (windows): %zu frames, %d characters replaced of %d shown\n", s.n, replaced, shown);
	free(a);
	free(b);
	free(prev_text);
	DestroyWindow(top);
	stream_free(&s);
	return failed ? 1 : 0;
}
