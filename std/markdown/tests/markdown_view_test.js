// markdown_view_test.js: the web view (web/markdown.js: the page's own
// elements) given a reply streamed in frame by frame (stream.json, beside
// this): each frame removes only the elements from the first paragraph
// that changed and adds theirs (a MutationObserver counts what it
// removes and adds, in one batch a frame), the elements before stay; a
// selection in the heading survives; and at the end the view is what one
// given the whole reply is. e2e's TestStdWebViews runs it in headless
// Chrome: it ends by writing "PASS ..." or "FAIL ..." into the page's
// #result.
import { makeDocument } from "../web/markdown.js";

const failures = [];
const check = (ok, what) => {
  if (!ok) failures.push(what);
};

// A paragraph as the generated glue gives it: every field, ints BigInts.
const paragraph = (p) => ({
  runs: (p.runs ?? []).map((r) => ({
    text: r.text ?? "", bold: !!r.bold, italic: !!r.italic, code: !!r.code, struck: !!r.struck, quoted: !!r.quoted, link: r.link ?? "",
  })),
  heading: BigInt(p.heading ?? 0), indent: BigInt(p.indent ?? 0), marker: p.marker ?? "",
  code: !!p.code, rule: !!p.rule, row: !!p.row, tight: !!p.tight,
});

const same = (a, b) => JSON.stringify(a, (_, x) => (typeof x === "bigint" ? Number(x) : x)) === JSON.stringify(b, (_, x) => (typeof x === "bigint" ? Number(x) : x));

async function run() {
  const { frames } = await (await fetch(new URL("stream.json", import.meta.url))).json();
  const stream = frames.map((f) => f.map(paragraph));
  check(stream.length > 20, `${stream.length} frames`);
  const v = makeDocument({});
  document.body.appendChild(v.el);
  const changes = [];
  const watch = new MutationObserver((records) => changes.push(...records.filter((r) => r.target === v.el)));
  watch.observe(v.el, { childList: true });
  let prev = [], replaced = 0, shown = 0, heading = null;
  for (const [f, paras] of stream.entries()) {
    let first = 0;
    while (first < prev.length && first < paras.length && same(prev[first], paras[first])) first++;
    const before = [...v.el.children];
    v.setParagraphs(paras);
    changes.push(...watch.takeRecords().filter((r) => r.target === v.el));
    const now = [...v.el.children];
    const removed = changes.reduce((n, r) => n + r.removedNodes.length, 0);
    const added = changes.reduce((n, r) => n + r.addedNodes.length, 0);
    changes.length = 0;
    check(now.length === paras.length, `frame ${f}: ${now.length} elements for ${paras.length} paragraphs`);
    check(removed === before.length - first && added === paras.length - first,
      `frame ${f}: removed ${removed} and added ${added} elements, want ${before.length - first} and ${paras.length - first}: from paragraph ${first}`);
    for (let i = 0; i < first; i++) check(now[i] === before[i], `frame ${f}: element ${i}, before the first change, was replaced`);
    replaced += added;
    shown += now.length;
    prev = paras;
    // Once the heading is whole and the next block has begun, part of it
    // is selected, as a reader would while the rest streams in.
    if (!heading && paras.length >= 2) {
      heading = now[0];
      const range = document.createRange();
      range.setStart(heading.firstChild, 2);
      range.setEnd(heading.firstChild, 6);
      getSelection().removeAllRanges();
      getSelection().addRange(range);
    }
  }
  watch.disconnect();
  const sel = getSelection();
  check(heading && v.el.children[0] === heading && sel.rangeCount === 1 && sel.toString() === heading.textContent.slice(2, 6),
    `the selection in the heading: "${sel.toString()}"`);
  check(replaced * 4 < shown, `replaced ${replaced} elements of the ${shown} shown frame by frame`);

  // At the end, what a view given it all at once is.
  const whole = makeDocument({});
  whole.setParagraphs(stream[stream.length - 1]);
  check(whole.el.innerHTML === v.el.innerHTML, `the view streamed isn't the whole's:\n${v.el.innerHTML}\n---\n${whole.el.innerHTML}`);
  check(v.el.textContent.startsWith("Moving the cache"), `the text: ${v.el.textContent}`);
  return `markdown (web): ${stream.length} frames, ${replaced} elements replaced of ${shown} shown`;
}

const out = document.getElementById("result") ?? document.body.appendChild(Object.assign(document.createElement("pre"), { id: "result" }));
run().then(
  (summary) => { out.textContent = failures.length ? `FAIL\n${failures.join("\n")}` : `PASS ${summary}`; },
  (e) => { out.textContent = `FAIL ${e?.stack ?? e}`; },
);
