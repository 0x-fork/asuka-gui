// Crash reports (asuka/crash) in the browser: the page's side of the
// core's record of the run (core/src/std/crash.c), which the core keeps
// in the app's files (fs.js, kept in IndexedDB) as it does on a phone.
//
//   - The record begins as the core starts (asuka_crash_start), with the
//     browser and its system as the device.
//   - Which runs live. The app's files are the origin's, shared by its
//     pages, so a page marks its run alive in localStorage, every page of
//     the origin seeing it, while it's open: the core leaves the runs of
//     the others alone (its crash_alive import asks here; spawn's workers
//     are told the marks with each job).
//   - The core's own fatal error: its panic writes the run's .crash file
//     (in memory, as the page's files are) and traps; the page keeps the
//     file at once and ends its mark, and the next launch reports it. A
//     trap without a panic (the core ran out of memory, a stack overflow)
//     is reported as a fatal exception.
//   - Uncaught JavaScript errors (the host's, or the app's web/*.js): an
//     exception report each, not fatal (the page goes on), written to the
//     inbox as the mobile hosts write theirs, and the core told
//     (asuka_crash_notify), so crash.reports() has it at once. At most
//     maxErrors a run, each message and stack once.

import * as fs from "./fs.js";

const ALIVE = "asuka-run:";
const aliveKey = (path) => ALIVE + path;
const heartbeat = 30_000; // how often a live page marks its run
const stale = 10 * 60_000; // a mark older than this is a page that's gone (a background tab's timers run once a minute at least)
const maxErrors = 20;
const enc = new TextEncoder();

function storage() {
  try {
    return globalThis.localStorage || null;
  } catch {
    return null; // blocked
  }
}

/** Crash keeps the page's side of the run's record: host is the web host (its core, once started). */
export class Crash {
  constructor(host) {
    this.host = host;
    this.dir = ""; // the record's directory, once the core began it
    this.path = ""; // the run's .crash file
    this.run = "";
    this.notify = null; // tells the core a report came
    this.ended = false;
    this.timer = 0;
    this.errors = 0;
    this.seen = new Set();
    this.onError = (e) => {
      if (e.error !== undefined && e.error !== null) this.exception(e.error);
    };
    this.onRejection = (e) => this.exception(e.reason);
    this.onHide = () => this.mark(false);
    this.onShow = () => this.mark(true);
    globalThis.addEventListener?.("error", this.onError);
    globalThis.addEventListener?.("unhandledrejection", this.onRejection);
    globalThis.addEventListener?.("pagehide", this.onHide);
    globalThis.addEventListener?.("pageshow", this.onShow);
  }

  /**
   * start begins the core's record (e: its exports; withBytes and text
   * are core.js's): before the core starts.
   */
  start(e, withBytes, text) {
    const info = JSON.stringify(this.info());
    const p = withBytes(enc.encode(info + "\0"), (q) => e.asuka_crash_start(q));
    this.dir = p ? text(p) : "";
    if (p) e.asuka_free(p);
    if (!this.dir) return;
    const [dir, run] = text(e.asuka_crash_where()).split("\n");
    this.run = run;
    this.path = `${dir}/runs/${run}.crash`;
    this.notify = () => e.asuka_crash_notify();
    this.mark(true);
  }

  /** where is the record's directory and run (asuka_crash_where's), for spawn's workers; "" if none. */
  where() { return this.dir ? `${this.dir}\n${this.run}` : ""; }

  // --- The run's mark ---

  /** alive reports whether a page has the run of path marked alive. */
  alive(path) {
    if (path === this.path && !this.ended) return true;
    const v = Number(storage()?.getItem(aliveKey(path)));
    return v > 0 && Date.now() - v < stale;
  }

  /** aliveRuns are the runs' paths marked alive now (for spawn's workers). */
  aliveRuns() {
    const st = storage(), out = [];
    if (!st) return this.path && !this.ended ? [this.path] : out;
    for (let i = 0; i < st.length; i++) {
      const k = st.key(i);
      if (k?.startsWith(ALIVE) && this.alive(k.slice(ALIVE.length))) out.push(k.slice(ALIVE.length));
    }
    return out;
  }

  mark(on) {
    if (!this.path) return;
    if (on && this.ended) return;
    clearInterval(this.timer);
    this.timer = 0;
    const st = storage();
    try {
      if (on && !this.ended) {
        st?.setItem(aliveKey(this.path), String(Date.now()));
        this.timer = setInterval(() => {
          try { st?.setItem(aliveKey(this.path), String(Date.now())); } catch {}
        }, heartbeat);
        this.timer.unref?.(); // (Node's tests: it doesn't keep the process)
      } else {
        st?.removeItem(aliveKey(this.path));
      }
    } catch {}
  }

  // --- Errors ---

  /** exception writes an uncaught error (a thrown value) to the inbox: a report; the core's trap ends the run. */
  exception(err) {
    if (!this.path || this.ended) return;
    if (err instanceof WebAssembly.RuntimeError) {
      // The core trapped: its panic wrote the .crash file, kept now; one
      // without (out of memory, its stack overflowed) is a fatal exception.
      this.ended = true;
      const crashed = fs.readFile(this.path)?.length > 0;
      if (!crashed) this.write(err, true);
      clearInterval(this.timer);
      this.timer = 0;
      fs.flush().then(() => this.mark(false));
      return;
    }
    if (this.errors >= maxErrors) return;
    if (this.write(err, false)) this.host?.afterCore?.(() => this.notify?.());
  }

  /** panicMessage is the core's panic, from the run's .crash file ("" if none). */
  panicMessage() {
    const b = this.path ? fs.readFile(this.path) : null;
    if (!b?.length) return "";
    return new TextDecoder().decode(b).trim().split("\n")[0].replace(/^panic: /, "");
  }

  write(err, fatal) {
    const isError = err instanceof Error || (err && typeof err === "object" && "message" in err);
    const name = isError ? String(err.name || "Error") : "Uncaught";
    const reason = isError ? String(err.message ?? "") : String(err);
    const stack = isError ? stackText(err) : "";
    const key = name + "\n" + reason + "\n" + stack;
    if (this.seen.has(key)) return false;
    this.seen.add(key);
    this.errors++;
    const rec = {
      time: Date.now(), pid: 0, thread: "main",
      name, reason, stack, frames: frames(stack), fatal,
      run: this.run, // this run's: its context
    };
    const id = Date.now().toString(36) + Math.random().toString(36).slice(2, 10);
    fs.writeFile(`${this.dir}/inbox/exception-web-${id}.json`, enc.encode(JSON.stringify(rec)));
    return true;
  }

  /** info is the browser and its system, for the run's context. */
  info() {
    return { model: systemOf(globalThis.navigator), system: "Web", systemVersion: browserOf(globalThis.navigator) };
  }
}

/** stackText is an error's stack without its first line where that repeats the message (V8's). */
function stackText(err) {
  const s = String(err.stack || "");
  const head = `${err.name}: ${err.message}`;
  return s.startsWith(head) ? s.slice(head.length).replace(/^\n/, "") : s;
}

/** frames reads a stack's frames: V8's "at f (url:1:2)", Firefox's and Safari's "f@url:1:2". */
export function frames(stack) {
  const out = [];
  for (let l of stack.split("\n")) {
    l = l.trim();
    let m = /^at (?:(.*?) \()?(.*?):(\d+):\d+\)?$/.exec(l);
    if (m) {
      out.push(frame(m[1] || "", m[2], m[3]));
      continue;
    }
    m = /^(.*?)@(.*?):(\d+):\d+$/.exec(l);
    if (m) out.push(frame(m[1], m[2], m[3]));
  }
  return out;
}

function frame(fn, file, line) {
  const f = {};
  if (fn) f.function = fn;
  if (file) f.file = file;
  const n = Number(line);
  if (n > 0) f.line = n;
  return f;
}

/** systemOf is the system the browser runs on, and its version where the browser says it: "macOS 15.1", "Android 14", "Windows". */
export function systemOf(nav) {
  const ua = String(nav?.userAgent || "");
  let m;
  if ((m = /\b(?:iPhone|iPad|iPod).*? OS (\d+(?:_\d+)*)/.exec(ua))) return "iOS " + m[1].replace(/_/g, ".");
  if ((m = /Android (\d+(?:\.\d+)*)/.exec(ua))) return "Android " + m[1];
  if (/Windows/.test(ua)) return "Windows";
  if (/CrOS/.test(ua)) return "ChromeOS";
  if ((m = /Mac OS X (\d+(?:[._]\d+)*)/.exec(ua))) {
    // Browsers freeze macOS's version at 10.15.7: only an older one is real.
    const v = m[1].replace(/_/g, ".");
    return v === "10.15.7" ? "macOS" : "macOS " + v;
  }
  if (/Linux/.test(ua)) return "Linux";
  return String(nav?.userAgentData?.platform || nav?.platform || "");
}

/** browserOf is the browser and its major version: "Chrome 131", "Safari 18.1", "Firefox 133". */
export function browserOf(nav) {
  const ua = String(nav?.userAgent || "");
  const brands = nav?.userAgentData?.brands;
  if (Array.isArray(brands)) {
    const b = brands.find((b) => !/Not.?A.?Brand|Chromium/i.test(b.brand)) || brands.find((b) => /Chromium/i.test(b.brand));
    if (b) return `${b.brand.replace(/^Google /, "")} ${b.version}`;
  }
  let m;
  if ((m = /Edg(?:e|A|iOS)?\/(\d+)/.exec(ua))) return "Edge " + m[1];
  if ((m = /Firefox\/(\d+)/.exec(ua)) || (m = /FxiOS\/(\d+)/.exec(ua))) return "Firefox " + m[1];
  if ((m = /(?:Chrome|CriOS)\/(\d+)/.exec(ua))) return "Chrome " + m[1];
  if ((m = /Version\/(\d+(?:\.\d+)?).*Safari/.exec(ua))) return "Safari " + m[1];
  return "";
}
