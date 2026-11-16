// The C core as WebAssembly (asuka.wasm: the core, its VM, and an app
// compiled in or loaded as bytecode), driven by the web host (host.js):
// batches and measurements through a shared buffer, events and the
// lists' calls into the core's C ABI (asuka.h). The C library's system
// calls are WASI's, answered by wasi.js (the app's files: fs.js); spawn
// runs on the page's workers (spawn.js), each with the same core.
//
//	const core = await startCore(host, { wasm: "asuka.wasm", module: "app.akb", dev: true, appDir: "/data/app" });

import { wasi } from "./wasi.js";
import { Pool } from "./spawn.js";

/**
 * startCore loads the core into the page and connects it to host. Options: wasm (the
 * core's URL), module (bytecode to run: asuka dev's app), dev (asuka dev's page: the module
 * loads as its reloads do, and core.reload loads the next one keeping the app's state and
 * views), appDir (the app's data directory, asuka/fs's appDir), workers (false: spawn runs
 * in place, on the page's thread), sqlite (a function resolving to the page's SQLite
 * client, sqlite.js's), makeWorker (makes a worker of a URL: Node's tests'),
 * log (where the core's output goes).
 */
export async function startCore(host, options = {}) {
  let instance, pool = null;
  // asuka/sqlite: the page's SQLite worker (sqlite.js), when the app has the plugin's library.
  let sqlite = null;
  if (options.sqlite) {
    try {
      sqlite = await options.sqlite();
      sqlite.root(options.appDir);
    } catch (err) {
      console.error("asuka/sqlite: SQLite didn't load:", err);
    }
  }
  const memory = () => instance.exports.memory;
  const timers = new Map();
  const log = options.log || ((fd, line) => (fd === 2 ? console.error : console.log)(line));
  const imports = {
    asuka: {
      apply: () => host.apply(),
      measure: () => host.measure(),
      post: () => host.post(),
      timer_set: (id, ms) => {
        timers.set(id, setTimeout(() => {
          timers.delete(id);
          host.call(() => { e.asuka_timer_fire(id); e.asuka_run_posted(); });
        }, ms));
      },
      timer_clear: (id) => { clearTimeout(timers.get(id)); timers.delete(id); },
      // The zone's offset from UTC at a time (Unix seconds), in seconds east:
      // dates, and asuka/time's local zone.
      zone_offset: (s) => -new Date(s * 1000).getTimezoneOffset() * 60,
      // spawn's jobs (core/src/abi/web.c): its bytes copied out, for a worker.
      spawn: (id, p, n) => pool.run(id, new Uint8Array(memory().buffer, p, n).slice()),
      spawn_cancel: (id) => pool?.cancel(id),
      // asuka/sqlite's calls (core/src/sqlite/sqlite.c): to the page's
      // SQLite worker, answered into the core once it isn't running.
      sqlite_available: () => (sqlite ? 1 : 0),
      sqlite_send: (id, op, conn, p, n) => {
        const input = new Uint8Array(memory().buffer, p, n).slice();
        sqlite.send(id, op, conn, input, (answer) => host.call(() => withBytes(answer, (q, k) => e.asuka_sqlite_done(id, q, k))));
      },
    },
    wasi_snapshot_preview1: wasi(memory, log),
  };
  // Compiled once: the page's instance and each worker's.
  const url = options.wasm || "asuka.wasm";
  const wasmModule = await WebAssembly.compileStreaming(fetch(url)).catch(async () => {
    // A server that doesn't send application/wasm.
    return WebAssembly.compile(await (await fetch(url)).arrayBuffer());
  });
  instance = await WebAssembly.instantiate(wasmModule, imports);
  const e = instance.exports;
  e._initialize?.();

  // Bytes into the core's memory for a call, freed after it.
  const withBytes = (bytes, f) => {
    const p = e.asuka_alloc(bytes.length);
    new Uint8Array(memory().buffer, p, bytes.length).set(bytes);
    try { return f(p, bytes.length); } finally { e.asuka_free(p); }
  };
  // asuka dev's loads: the state cells kept (0 for the first) and the microseconds taken.
  const devLoad = (bytes) => {
    const micros = e.asuka_alloc(4);
    try {
      const kept = withBytes(bytes, (p, n) => e.asuka_dev_reload(p, n, micros, 0));
      if (kept < 0) throw new Error("asuka: the app's module didn't load (see the console)");
      return { kept, micros: new DataView(memory().buffer).getUint32(micros, true) };
    } finally {
      e.asuka_free(micros);
    }
  };
  const core = {
    /** reload loads asuka dev's next module, keeping the app's state and views: { kept, micros }. */
    reload: (bytes) => new Promise((ok, fail) => host.call(() => {
      try {
        ok(devLoad(bytes));
        pool?.reload(bytes);
      } catch (err) { fail(err); }
    })),
    event: (bytes) => withBytes(bytes, (p, n) => e.asuka_event(p, n)),
    listCount: (id) => Number(e.asuka_list_count(id)),
    listBind: (id, index, cell) => e.asuka_list_bind(id, BigInt(index), cell),
    listUnbind: (id, cell) => e.asuka_list_unbind(id, cell),
    runPosted: () => e.asuka_run_posted(),
    nativeDone: (call, bytes) => withBytes(bytes, (p, n) => e.asuka_native_done?.(BigInt(call), p, n)),
    nativeInvoke: () => null,
    saveState: () => null,
  };
  host.attach(memory());
  host.connect(core, e.asuka_io());
  host.inGo = 1;
  if (options.appDir) withBytes(new TextEncoder().encode(options.appDir + "\0"), (p) => e.asuka_set_app_dir(p));
  e.asuka_web_start();
  let bytes = null;
  if (options.module) {
    // asuka dev: the app's bytecode.
    const res = await fetch(options.module);
    if (!res.ok) throw new Error("asuka: the app's module: " + (await res.text()));
    bytes = new Uint8Array(await res.arrayBuffer());
  }
  if (options.workers !== false && (options.makeWorker || typeof Worker !== "undefined")) {
    pool = new Pool({
      wasm: wasmModule,
      module: bytes,
      appDir: options.appDir,
      sqlite: !!sqlite,
      log: (line) => log(2, line),
      ...(options.makeWorker ? { makeWorker: options.makeWorker } : {}),
      // A job's answer: into the core, once it isn't running.
      done: (id, status, answer) => host.call(() => withBytes(answer, (p, n) => e.asuka_spawn_done(id, status, p, n))),
    });
    e.asuka_web_workers(1);
  }
  if (bytes) {
    if (options.dev) {
      core.loaded = devLoad(bytes);
    } else if (withBytes(bytes, (p, n) => e.asuka_load(p, n, 0)) !== 0) {
      throw new Error("asuka: the app's module didn't load (see the console)");
    }
  } else if (e.asuka_run(0) !== 0) {
    // The app compiled into asuka.wasm.
    throw new Error("asuka: the app didn't start (see the console)");
  }
  host.started();
  return core;
}
