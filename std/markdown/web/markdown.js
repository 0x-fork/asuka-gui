// asuka/markdown on the web: marked (vendored, marked.js: CommonMark with
// GitHub's tables, task lists, strikethrough and autolinks) as HTML in
// the view, styled after the platform's text. Raw HTML in the text is
// escaped, so it shows as text, and links and images take only web,
// mail and relative addresses; links open in a new tab.
import { Marked } from "./marked.js";

const escape = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);

/** safe is href if it's a web, mail or relative address, else null. */
function safe(href, image) {
  if (href == null) return null;
  const h = String(href).trim();
  const scheme = /^([a-z][a-z0-9+.-]*):/i.exec(h);
  if (!scheme) return h;
  const s = scheme[1].toLowerCase();
  if (s === "http" || s === "https") return h;
  if (!image && (s === "mailto" || s === "tel")) return h;
  if (image && s === "data" && /^data:image\/(png|gif|jpe?g|webp|avif);/i.test(h)) return h;
  return null;
}

const md = new Marked({
  gfm: true,
  renderer: {
    html(html) { return escape(html); },
    link(href, title, text) {
      const h = safe(href, false);
      if (h == null) return text;
      const t = title ? ` title="${escape(title)}"` : "";
      return `<a href="${escape(h)}"${t} target="_blank" rel="noopener noreferrer">${text}</a>`;
    },
    image(href, title, text) {
      const h = safe(href, true);
      if (h == null) return escape(text);
      const t = title ? ` title="${escape(title)}"` : "";
      return `<img src="${escape(h)}" alt="${escape(text)}"${t} loading="lazy">`;
    },
  },
});

const css = `
.asuka-markdown { display: block; color: var(--asuka-label); line-height: 1.45; overflow-wrap: anywhere; user-select: text; -webkit-user-select: text; }
.asuka-markdown > :first-child { margin-top: 0; }
.asuka-markdown > :last-child { margin-bottom: 0; }
.asuka-markdown p, .asuka-markdown ul, .asuka-markdown ol, .asuka-markdown blockquote, .asuka-markdown pre, .asuka-markdown table { margin: 0 0 0.75em; }
.asuka-markdown h1, .asuka-markdown h2, .asuka-markdown h3, .asuka-markdown h4, .asuka-markdown h5, .asuka-markdown h6 { margin: 1em 0 0.5em; line-height: 1.25; font-weight: 650; }
.asuka-markdown h1 { font-size: 1.6em; } .asuka-markdown h2 { font-size: 1.35em; } .asuka-markdown h3 { font-size: 1.15em; }
.asuka-markdown h4, .asuka-markdown h5, .asuka-markdown h6 { font-size: 1em; }
.asuka-markdown ul, .asuka-markdown ol { padding-inline-start: 1.5em; }
.asuka-markdown li > p { margin: 0; }
.asuka-markdown li + li { margin-top: 0.25em; }
.asuka-markdown a { color: var(--asuka-accent); }
.asuka-markdown code { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 0.9em; background: var(--asuka-fill); border-radius: 4px; padding: 0.1em 0.3em; }
.asuka-markdown pre { background: var(--asuka-secondary-background); border-radius: 8px; padding: 10px 12px; overflow-x: auto; }
.asuka-markdown pre code { background: none; padding: 0; font-size: 0.875em; }
.asuka-markdown blockquote { padding-inline-start: 0.9em; border-inline-start: 3px solid var(--asuka-separator); color: var(--asuka-secondary-label); }
.asuka-markdown hr { border: none; border-top: 1px solid var(--asuka-separator); margin: 1em 0; }
.asuka-markdown table { border-collapse: collapse; display: block; overflow-x: auto; }
.asuka-markdown th, .asuka-markdown td { border: 1px solid var(--asuka-separator); padding: 4px 8px; text-align: start; }
.asuka-markdown img { max-width: 100%; }
.asuka-markdown input[type=checkbox] { margin-inline-end: 0.4em; }
`;

let styled = false;

export function makeDocument(events) {
  if (!styled) {
    styled = true;
    const s = document.createElement("style");
    s.textContent = css;
    document.head.appendChild(s);
  }
  const el = document.createElement("div");
  el.className = "asuka-markdown";
  let text = null;
  return {
    el,
    setText(v) {
      if (v === text) return;
      text = v;
      el.innerHTML = md.parse(v);
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
