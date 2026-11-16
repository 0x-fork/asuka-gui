// asuka/markdown on the web: the paragraphs the core read (md4c) as the
// page's own elements (headings, paragraphs, code blocks, rules, tables'
// rows), styled after the platform's text: one selection, links that open
// in a new tab. Text is only ever text (textContent), never HTML.

const css = `
.asuka-markdown { display: block; color: var(--asuka-label); line-height: 1.45; overflow-wrap: anywhere; user-select: text; -webkit-user-select: text; }
.asuka-markdown > * { margin: 0 0 0.75em; }
.asuka-markdown > .tight { margin-bottom: 0.25em; }
.asuka-markdown > :last-child { margin-bottom: 0; }
.asuka-markdown h1, .asuka-markdown h2, .asuka-markdown h3, .asuka-markdown h4, .asuka-markdown h5, .asuka-markdown h6 { line-height: 1.25; font-weight: 650; }
.asuka-markdown h1 { font-size: 1.6em; } .asuka-markdown h2 { font-size: 1.35em; } .asuka-markdown h3 { font-size: 1.15em; }
.asuka-markdown h4, .asuka-markdown h5, .asuka-markdown h6 { font-size: 1em; }
.asuka-markdown .marker { display: inline-block; width: 1.5em; margin-inline-start: -1.5em; color: var(--asuka-secondary-label); }
.asuka-markdown .quoted { color: var(--asuka-secondary-label); }
.asuka-markdown a { color: var(--asuka-accent); }
.asuka-markdown code { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 0.9em; background: var(--asuka-fill); border-radius: 4px; padding: 0.1em 0.3em; }
.asuka-markdown pre { background: var(--asuka-secondary-background); border-radius: 8px; padding: 10px 12px; overflow-x: auto; white-space: pre; }
.asuka-markdown pre code { background: none; padding: 0; font-size: 0.875em; }
.asuka-markdown hr { border: none; border-top: 1px solid var(--asuka-separator); margin: 1em 0; }
.asuka-markdown .row { display: grid; grid-auto-flow: column; grid-auto-columns: minmax(0, 1fr); gap: 1em; border-bottom: 1px solid var(--asuka-separator); padding: 2px 0; }
`;

let styled = false;

/** run is a run's text in its style: a link, struck, emphasized, strong, code, innermost last. */
function run(r) {
  let node = document.createTextNode(r.text);
  const wrap = (tag) => {
    const e = document.createElement(tag);
    e.appendChild(node);
    node = e;
  };
  if (r.code) wrap("code");
  if (r.bold) wrap("strong");
  if (r.italic) wrap("em");
  if (r.struck) wrap("s");
  if (r.quoted) {
    wrap("span");
    node.className = "quoted";
  }
  if (r.link) {
    wrap("a");
    node.href = r.link;
    node.target = "_blank";
    node.rel = "noopener noreferrer";
  }
  return node;
}

/** block is a paragraph's element. */
export function block(p) {
  const heading = Number(p.heading);
  if (p.rule) return document.createElement("hr");
  let el, into;
  if (p.code) {
    el = document.createElement("pre");
    into = el.appendChild(document.createElement("code"));
  } else if (heading > 0) {
    el = into = document.createElement(`h${Math.min(heading, 6)}`);
  } else if (p.row) {
    el = into = document.createElement("div");
    el.className = "row";
  } else {
    el = into = document.createElement("p");
  }
  if (p.tight) el.classList.add("tight");
  const indent = Number(p.indent);
  if (indent > 0) el.style.paddingInlineStart = `${indent * 1.5}em`;
  if (p.marker) {
    const m = document.createElement("span");
    m.className = "marker";
    m.textContent = p.marker;
    into.appendChild(m);
  }
  if (p.row) {
    // Cells: the runs between tab runs.
    let cell = document.createElement("div");
    for (const r of p.runs) {
      if (r.text === "\t") {
        into.appendChild(cell);
        cell = document.createElement("div");
      } else {
        cell.appendChild(run(r));
      }
    }
    into.appendChild(cell);
  } else {
    for (const r of p.runs) into.appendChild(run(r));
  }
  return el;
}

export function makeDocument(events) {
  if (!styled) {
    styled = true;
    const s = document.createElement("style");
    s.textContent = css;
    document.head.appendChild(s);
  }
  const el = document.createElement("div");
  el.className = "asuka-markdown";
  return {
    el,
    setParagraphs(v) {
      el.replaceChildren(...v.map(block));
    },
    measure(c) {
      // As wide as it's given, as tall as its text at that width.
      const w = c && Number.isFinite(c.width) ? c.width : c && Number.isFinite(c.availW) ? c.availW : el.scrollWidth;
      const prev = el.style.width;
      el.style.width = w + "px";
      const h = el.scrollHeight;
      el.style.width = prev;
      return [w, h];
    },
  };
}
