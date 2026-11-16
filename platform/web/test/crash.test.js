// platform/crash in the browser (crash.js): the page's side of the core's
// record: begun with the browser as the device, the run marked alive,
// uncaught errors as exception records in the inbox (the core told), the
// core's trap ending the run; and what the page says of the browser.
import { test } from "node:test";
import assert from "node:assert/strict";
import * as fs from "../src/fs.js";
import { Crash, frames, systemOf, browserOf } from "../src/crash.js";

const items = new Map();
Object.defineProperty(globalThis, "localStorage", {
  configurable: true,
  value: {
    getItem: (k) => items.get(k) ?? null, setItem: (k, v) => items.set(k, String(v)), removeItem: (k) => items.delete(k),
    key: (i) => [...items.keys()][i] ?? null, get length() { return items.size; },
  },
});
const enc = new TextEncoder();
const dec = new TextDecoder();

// fakeCore is the core's exports as Crash uses them, its strings in a memory of its own.
function fakeCore(dir, run) {
  const mem = new Uint8Array(4096);
  let top = 16;
  const str = (s) => { const b = enc.encode(s + "\0"); const p = top; mem.set(b, p); top += b.length; return p; };
  const core = {
    started: null, notified: 0, freed: [],
    asuka_crash_start: (p) => { core.started = text(p); return dir ? str(dir) : 0; },
    asuka_crash_where: () => str(`${dir}\n${run}`),
    asuka_crash_notify: () => core.notified++,
    asuka_free: (p) => core.freed.push(p),
  };
  const withBytes = (b, f) => { const p = top; mem.set(b, p); top += b.length; return f(p, b.length); };
  const text = (p) => dec.decode(mem.subarray(p, mem.indexOf(0, p)));
  return { core, withBytes, text };
}

const inbox = (dir) => (fs.fsys.entry(fs.clean(dir + "/inbox")) ? fs.fsys.names(fs.clean(dir + "/inbox")) : []).map((n) => [n, JSON.parse(dec.decode(fs.readFile(`${dir}/inbox/${n}`)))]);

test("the record begins with the browser as the device, and the run is marked alive while the page is", () => {
  const dir = "/data/crash1/crashes";
  const { core, withBytes, text } = fakeCore(dir, "100-7");
  const c = new Crash({ afterCore: (f) => f() });
  c.start(core, withBytes, text);
  assert.equal(JSON.parse(core.started).system, "Web");
  assert.equal(core.freed.length, 1, "the directory's string freed");
  assert.equal(c.where(), `${dir}\n100-7`);
  assert.ok(c.alive(`${dir}/runs/100-7.crash`));
  assert.ok(c.aliveRuns().includes(`${dir}/runs/100-7.crash`));
  assert.equal(c.alive(`${dir}/runs/1-1.crash`), false, "another page's, not marked");
  items.set(`asuka-run:${dir}/runs/1-1.crash`, String(Date.now()));
  assert.ok(c.alive(`${dir}/runs/1-1.crash`), "another open page's");
  items.set(`asuka-run:${dir}/runs/1-1.crash`, String(Date.now() - 11 * 60_000));
  assert.equal(c.alive(`${dir}/runs/1-1.crash`), false, "a mark gone stale");
  c.onHide();
  assert.equal(items.has(`asuka-run:${dir}/runs/100-7.crash`), false);
  c.onShow();
  assert.ok(items.has(`asuka-run:${dir}/runs/100-7.crash`));
});

test("an app that doesn't read its reports keeps no record", () => {
  const { core, withBytes, text } = fakeCore("", "");
  const c = new Crash({ afterCore: (f) => f() });
  c.start(core, withBytes, text);
  assert.equal(c.where(), "");
  c.exception(new Error("x"));
  assert.equal(core.notified, 0);
});

test("uncaught errors: an exception record each in the inbox, not fatal, each once, the core told", () => {
  const dir = "/data/crash2/crashes";
  const { core, withBytes, text } = fakeCore(dir, "200-9");
  const c = new Crash({ afterCore: (f) => f() });
  c.start(core, withBytes, text);
  const e = new TypeError("x is undefined");
  c.exception(e);
  c.exception(e);
  c.exception("a string");
  const recs = inbox(dir).map(([, r]) => r);
  assert.equal(recs.length, 2);
  const te = recs.find((r) => r.name === "TypeError");
  assert.equal(te.reason, "x is undefined");
  assert.equal(te.fatal, false);
  assert.equal(te.run, "200-9");
  assert.ok(te.frames.length > 0);
  assert.ok(recs.find((r) => r.name === "Uncaught" && r.reason === "a string"));
  assert.equal(core.notified, 2);
});

test("the core's trap ends the run: its panic's .crash kept; without one, a fatal exception", async () => {
  const dir = "/data/crash3/crashes";
  const { core, withBytes, text } = fakeCore(dir, "300-1");
  const c = new Crash({ afterCore: (f) => f() });
  c.start(core, withBytes, text);
  fs.writeFile(`${dir}/runs/300-1.crash`, enc.encode("panic: boom\n"));
  c.exception(new WebAssembly.RuntimeError("unreachable"));
  assert.equal(inbox(dir).length, 0, "the panic says it");
  assert.equal(c.panicMessage(), "boom", "what the page shows: the panic's message");
  await fs.flush();
  await new Promise((r) => setTimeout(r, 0));
  assert.equal(c.alive(`${dir}/runs/300-1.crash`), false);
  c.exception(new Error("after"));
  assert.equal(inbox(dir).length, 0, "nothing after the end");

  const dir2 = "/data/crash4/crashes";
  const k = fakeCore(dir2, "400-1");
  const c2 = new Crash({ afterCore: (f) => f() });
  c2.start(k.core, k.withBytes, k.text);
  c2.exception(new WebAssembly.RuntimeError("memory access out of bounds"));
  const [[, rec]] = inbox(dir2);
  assert.equal(rec.name, "RuntimeError");
  assert.equal(rec.fatal, true);
  assert.equal(c2.panicMessage(), "", "no panic: the page shows the trap's own");
});

test("a stack's frames, V8's and Firefox's and Safari's", () => {
  assert.deepEqual(frames("    at set (http://localhost/asuka/views.js:12:5)\n    at http://localhost/web/natives.js:3:9"), [
    { function: "set", file: "http://localhost/asuka/views.js", line: 12 },
    { file: "http://localhost/web/natives.js", line: 3 },
  ]);
  assert.deepEqual(frames("set@http://localhost/asuka/views.js:12:5\n@http://localhost/web/natives.js:3:9"), [
    { function: "set", file: "http://localhost/asuka/views.js", line: 12 },
    { file: "http://localhost/web/natives.js", line: 3 },
  ]);
});

test("the browser and its system", () => {
  const mac = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Safari/605.1.15";
  assert.equal(systemOf({ userAgent: mac }), "macOS");
  assert.equal(browserOf({ userAgent: mac }), "Safari 18.1");
  const iphone = "Mozilla/5.0 (iPhone; CPU iPhone OS 18_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Mobile/15E148 Safari/604.1";
  assert.equal(systemOf({ userAgent: iphone }), "iOS 18.1");
  const android = "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36";
  assert.equal(systemOf({ userAgent: android }), "Android 14");
  assert.equal(browserOf({ userAgent: android }), "Chrome 131");
  const firefox = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0";
  assert.equal(systemOf({ userAgent: firefox }), "Windows");
  assert.equal(browserOf({ userAgent: firefox }), "Firefox 133");
  const edge = { userAgent: "… Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0", userAgentData: { brands: [{ brand: "Not_A Brand", version: "24" }, { brand: "Chromium", version: "131" }, { brand: "Microsoft Edge", version: "131" }] } };
  assert.equal(browserOf(edge), "Microsoft Edge 131");
  const chrome = { userAgent: "", userAgentData: { brands: [{ brand: "Chromium", version: "131" }, { brand: "Google Chrome", version: "131" }, { brand: "Not_A Brand", version: "24" }] } };
  assert.equal(browserOf(chrome), "Chrome 131");
});
