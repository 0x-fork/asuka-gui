// asuka/print's web code (std/print/web/print.js) printing a WebView's
// page: its frame's when the page is the app's origin's, else the app's
// page with only the WebView on it; and a Ref crossing to a package's
// JavaScript as its element (src/native.js): node --test platform/web.
import { test } from "node:test";
import assert from "node:assert/strict";
import * as native from "../src/native.js";

globalThis.asuka = { native };
// The app's page: what printDialog listens on while a dialog is open.
globalThis.addEventListener = () => {};
globalThis.removeEventListener = () => {};
const { makePrinter } = await import("../../../std/print/web/print.js");
const { Outcome, PrintError } = await import("../../../std/print/web/print.asuka.js");

// A window whose print() opens and closes a dialog, noting what its
// document had in it then.
function fakeWindow(doc, seen) {
  const listeners = {};
  return {
    document: doc,
    addEventListener(k, f) { (listeners[k] ??= []).push(f); },
    removeEventListener() {},
    print() {
      seen.push({ title: doc.title, styles: doc.head.children.map((s) => s.textContent) });
      for (const k of ["beforeprint", "afterprint"]) for (const f of listeners[k] ?? []) f();
    },
  };
}

function fakeDocument(title) {
  const head = {
    children: [],
    appendChild(e) { this.children.push(e); e.remove = () => { this.children = this.children.filter((x) => x !== e); }; },
  };
  return { title, head, documentElement: head, createElement: () => ({ textContent: "" }), addEventListener() {}, removeEventListener() {} };
}

function fakeElement(frame) {
  const classes = new Set();
  return {
    tagName: "DIV",
    classList: { add: (c) => classes.add(c), remove: (c) => classes.delete(c) },
    classes,
    querySelector: (q) => (q === "iframe" ? frame : null),
  };
}

test("webView: a page of the app's origin prints from its frame", async () => {
  globalThis.document = fakeDocument("App");
  const seen = [];
  const doc = fakeDocument("Page");
  const frame = { tagName: "IFRAME", contentWindow: null };
  frame.contentWindow = fakeWindow(doc, seen);
  const el = fakeElement(frame);
  const out = await makePrinter().webView(new AbortController().signal, el, { name: "Receipt", landscape: true, letter: true });
  assert.equal(out, Outcome.unknown);
  assert.equal(seen.length, 1);
  assert.equal(seen[0].title, "Receipt");
  assert.match(seen[0].styles[0], /@page \{ size: letter landscape; \}/);
  assert.doesNotMatch(seen[0].styles[0], /asuka-printing/);
  assert.equal(doc.title, "Page", "its title back");
  assert.equal(doc.head.children.length, 0, "its style gone");
});

test("webView: another site's page prints as the app's page shows it", async () => {
  const seen = [];
  const appDoc = fakeDocument("App");
  globalThis.document = appDoc;
  const win = fakeWindow(appDoc, seen);
  globalThis.window = win;
  const frame = { tagName: "IFRAME", get contentWindow() { return { get document() { throw new Error("SecurityError"); } }; } };
  const el = fakeElement(frame);
  let during;
  const print = win.print;
  win.print = () => { during = [...el.classes]; print(); };
  const out = await makePrinter().webView(new AbortController().signal, el, { name: "", landscape: false, letter: false });
  assert.equal(out, Outcome.unknown);
  assert.deepEqual(during, ["asuka-printing"]);
  assert.match(seen[0].styles[0], /@page \{ size: A4 portrait; \}.*@media print \{ body \* \{ visibility: hidden/);
  assert.equal(seen[0].title, "App");
  assert.equal(el.classes.size, 0);
  assert.equal(appDoc.head.children.length, 0);
});

test("webView: no WebView in the Ref", async () => {
  const p = makePrinter();
  await assert.rejects(p.webView(new AbortController().signal, null, { name: "" }), (e) => e instanceof PrintError && e.message === "print: can't print this file");
  await assert.rejects(p.webView(new AbortController().signal, fakeElement(null), { name: "" }), PrintError);
});

test("a Ref crosses as the element the host shows by its id", () => {
  const el = { tagName: "DIV" };
  native.setViews((id) => (id === 7 ? el : null));
  const w = new native.NativeWriter().uint64(7n).uint64(9n).uint64(0n);
  const r = new native.NativeReader(w.bytes());
  assert.equal(r.viewRef(), el);
  assert.equal(r.viewRef(), null);
  assert.equal(r.viewRef(), null);
  native.setViews(() => null);
});
