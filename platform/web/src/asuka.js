// Asuka's web loader: it starts an app (the C core as WebAssembly, the
// app compiled into it or loaded as bytecode) with the web host on a page.
//
//	import { start } from "./asuka/asuka.js";
//	start({ core: "asuka.wasm", container: document.body });
//
// Options: container (the element the app fills; the page's body by
// default, the app then filling the page), core (the URL of the C core,
// asuka.wasm, with the app compiled in) and module (an app's bytecode to
// load into it: asuka dev's, which with dev loads each save's in place,
// keeping the app's state), assets (where its assets are served:
// "assets"), name (the app's: its stored data is kept under it), fonts
// (the app's font files, assets/fonts), libs (the URLs of the app's
// plugins' files, by their names: an app that imports asuka/sqlite has
// asqlite.wasm, and SQLite's worker starts with it), serviceWorker (the
// URL of the app's service worker, sw.js with the app's settings, which
// the page registers: its notifications and pushes), vapidPublicKey (the
// app's VAPID key, for asuka/push), icon (a notification's icon),
// mapTiles (the URL of maps.View's tiles, with {z}, {x} and {y}:
// OpenStreetMap's by default), workers (false: spawn runs on the page's
// thread, not its workers), dev (asuka dev's: the page reloads with each
// build, keeping the app's state, and the app's logs go to the
// terminal).

import { Host } from "./host.js";
import * as fs from "./fs.js";
import { natives as builtinNatives } from "./modules/hardware.js";
import * as worker from "./worker.js";
import { notifyFor } from "./modules/system-notify.js";
import { startCore } from "./core.js";
import { Crash } from "./crash.js";
// What packages' generated JavaScript (asuka native) builds on: globalThis.asuka.native.
export * as native from "./native.js";

let hosts = 0;
const natives = new Map();
const modules = new Map();

/** registerNative gives the page a native view kind (ui.Native): factory(ctx) returns { el, set(name, value), measure?(c), destroy?() }; ctx.emit(name, values) sends its events. */
export function registerNative(kind, factory) { natives.set(kind, factory); }

/** registerModule gives the page a native module (ui.Call): methods by name, each taking the call's bytes and returning (or resolving to) the result's. */
export function registerModule(name, methods) { modules.set(name, methods); }

export async function start(options = {}) {
  const fullPage = !options.container;
  const container = options.container || document.body.appendChild(document.createElement("div"));
  if (fullPage) container.classList.add("asuka-page");
  const name = options.name || "app";
  const dev = options.dev ? devClient(options.dev) : null;
  // The app's service worker, listened to before the app starts: a tap
  // on a notification may be what opened the page.
  worker.register(options.serviceWorker);
  // SQLite's worker starts beside the app (asuka/sdk/sqlite), loading its module meanwhile.
  const sqliteWasm = options.libs?.["asqlite.wasm"];
  const sqlite = sqliteWasm ? import("./sqlite.js").then((m) => m.load(new URL(sqliteWasm, document.baseURI))) : null;
  sqlite?.catch(() => {});
  const data = "/data/" + name.replace(/[^\w.-]/g, "_");
  await fs.mount({ name, dir: data, output: dev?.log });
  const host = new Host(container, { ...options, fullPage, name });
  // The host's own native views (camera.View), then the app's, which may replace them.
  for (const [k, f] of Object.entries(builtinNatives)) host.natives.set(k, f);
  for (const [k, f] of natives) host.natives.set(k, f);
  for (const [k, m] of modules) host.modules.registered.set(k, m);
  worker.attach(host);
  // Files an installed app is opened with (asuka.json's fileTypes).
  worker.openLaunchFiles(host, fs.saveUpload);
  const hostName = "asukaHost" + (hosts++ || "");
  globalThis[hostName] = host;
  let core;
  try {
    core = await startCore(host, {
      wasm: options.core || "asuka.wasm",
      module: options.module,
      dev: !!dev,
      log: dev?.log,
      appDir: data,
      workers: options.workers,
      sqlite: sqlite ? () => sqlite : null,
      crash: (host.crash = new Crash(host)),
    });
  } catch (e) {
    showError(container, "The app couldn't load: " + e.message);
    throw e;
  }
  if (dev) dev.attach(host, core);
  try { notifyFor(host); } catch (e) { console.warn("asuka/notify:", e); }
  return host;
}

function showError(container, msg) {
  const d = document.createElement("pre");
  d.className = "asuka-error";
  d.textContent = msg;
  container.appendChild(d);
}

/** devClient is asuka dev's side of the page: logs to the terminal, reloads with each build. */
function devClient(url) {
  const base = typeof url === "string" ? url : "/__asuka/";
  let pending = "";
  let timer = 0;
  let errorText = "";
  const send = () => {
    timer = 0;
    const body = pending;
    pending = "";
    if (body) fetch(base + "log", { method: "POST", body, keepalive: true }).catch(() => {});
  };
  const log = (fd, text) => {
    if (fd === 2) errorText += text;
    pending += (fd === 2 ? "\x02" : "\x01") + text;
    if (!timer) timer = setTimeout(send, 30);
  };
  addEventListener("error", (e) => log(2, `page: ${e.message} (${e.filename}:${e.lineno})\n${e.error?.stack || ""}\n`));
  addEventListener("unhandledrejection", (e) => log(2, `page: ${e.reason?.stack || e.reason}\n`));
  let host = null, core = null;
  const report = (r, first) => fetch(base + "reloaded", { method: "POST", body: JSON.stringify({ First: first, Kept: r.kept, Micros: r.micros }) }).catch(() => {});
  const events = new EventSource(base + "events");
  // A new build of the app's module: loaded in place, its state and views kept.
  let loading = Promise.resolve();
  events.addEventListener("module", (e) => {
    if (!core?.reload) return;
    loading = loading.then(async () => {
      try {
        const res = await fetch(base + "app.akb?" + e.data);
        if (!res.ok) throw new Error(await res.text());
        report(await core.reload(new Uint8Array(await res.arrayBuffer())), false);
      } catch (err) {
        log(2, `page: the new module didn't load: ${err?.message || err}\n`);
      }
      send();
    });
  });
  events.addEventListener("reload", () => {
    host?.saveState();
    send();
    fs.flush().then(() => location.reload());
  });
  events.addEventListener("failed", (e) => {
    let d = document.querySelector(".asuka-build-error");
    if (!d) {
      d = document.createElement("pre");
      d.className = "asuka-error asuka-build-error";
      document.body.appendChild(d);
    }
    d.textContent = e.data;
  });
  events.addEventListener("fixed", () => document.querySelector(".asuka-build-error")?.remove());
  // GUI tests' commands (e2e/), as the mobile hosts' debug builds take them.
  events.addEventListener("automation", async (e) => {
    const { id, commands } = JSON.parse(e.data);
    let out = "", error = "";
    try {
      const { run } = await import("./automation.js");
      out = await run(host, commands);
    } catch (err) {
      error = String(err?.message || err);
    }
    fetch(base + "automation/done", { method: "POST", body: JSON.stringify({ id, out, error }) }).catch(() => {});
  });
  return {
    log,
    attach(h, c) {
      host = h;
      core = c || null;
      if (core?.loaded) report(core.loaded, true);
    },
    lastError: () => errorText.trim().split("\n").slice(-30).join("\n"),
  };
}
