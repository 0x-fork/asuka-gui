// markdown_test.c: the paragraphs markdown.c reads (platform/desktop's
// TestStdUnits builds it with the package's portable C and runs it).
#include "markdown.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
	md_doc d = markdown_read(
	    "# Plan\n\nThree **steps**, *in* `order`:\n\n1. Read [docs](https://example.com) and [no](javascript:x)\n2. Write\n   - [x] "
	    "nested\n\n> quoted\n\n---\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```go\nfmt.Println(1)\n```\n\nA&amp;B &#x263A; www.asuka.dev "
	    "~~gone~~<br>raw\n");
	asuka_writer w = {0};
	for (size_t i = 0; i < d.n; i++) {
		const md_para *p = &d.paras[i];
		char *head = asuka_sprintf("%d \"%s\" h%d code=%d rule=%d cells=%d tight=%d:", p->indent, p->marker ? p->marker : "", p->heading, p->code,
		                           p->rule, p->cells, p->tight);
		asuka_writer_put(&w, head, strlen(head));
		free(head);
		for (size_t j = 0; j < p->nruns; j++) {
			const md_run *r = &p->runs[j];
			char *run = asuka_sprintf(" [%s%s%s%s%s%s%s%s]", r->text, r->bold ? " b" : "", r->italic ? " i" : "", r->mono ? " m" : "",
			                          r->strike ? " s" : "", r->link ? " ->" : "", r->link ? r->link : "", r->quote ? " q" : "");
			asuka_writer_put(&w, run, strlen(run));
			free(run);
		}
		asuka_writer_put(&w, "\n", 1);
	}
	asuka_writer_u8(&w, 0);
	markdown_doc_free(&d);
	const char *want =
	    "0 \"\" h1 code=0 rule=0 cells=0 tight=0: [Plan]\n"
	    "0 \"\" h0 code=0 rule=0 cells=0 tight=0: [Three ] [steps b] [, ] [in i] [ ] [order m] [:]\n"
	    "1 \"1.\" h0 code=0 rule=0 cells=0 tight=1: [Read ] [docs ->https://example.com] [ and no]\n"
	    "1 \"2.\" h0 code=0 rule=0 cells=0 tight=1: [Write]\n"
	    "2 \"• ☑\" h0 code=0 rule=0 cells=0 tight=1: [nested]\n"
	    "1 \"\" h0 code=0 rule=0 cells=0 tight=0: [quoted q]\n"
	    "0 \"\" h0 code=0 rule=1 cells=0 tight=0:\n"
	    "0 \"\" h0 code=0 rule=0 cells=1 tight=1: [a b] [\t] [b b]\n"
	    "0 \"\" h0 code=0 rule=0 cells=1 tight=1: [1] [\t] [2]\n"
	    "0 \"\" h0 code=1 rule=0 cells=0 tight=0: [fmt.Println(1) m]\n"
	    "0 \"\" h0 code=0 rule=0 cells=0 tight=0: [A&B ☺ ] [www.asuka.dev ->http://www.asuka.dev] [ ] [gone s] [<br>raw]\n";
	if (strcmp((const char *)w.data, want) != 0) {
		printf("got\n%s\nwant\n%s", (const char *)w.data, want);
		return 1;
	}
	printf("markdown: ok\n");
	asuka_writer_free(&w);
	return 0;
}
