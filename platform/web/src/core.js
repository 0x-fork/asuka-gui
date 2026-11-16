// The C core as WebAssembly (asuka.wasm: the core, its VM, and an app
// compiled in or loaded as bytecode), driven by the web host (host.js)
// as the Go web host was: batches and measurements through a shared
// buffer, events and the lists' calls into the core's C ABI (asuka.h).
//
//	const core = await startCore(host, { wasm: "asuka.wasm", module: "app.akb", dev: true });

/**
 * wasi is the little of WASI the core's C library calls: the clock, randomness, writing to
 * the console. The page has no directory for the core (asuka/fs's calls fail, as they would
 * with nothing preopened), so the file system's calls answer EBADF.
 */
function wasi(memory, log) {
  const mem = () => new DataView(memory().buffer);
  const text = new TextDecoder();
  const lines = { 1: "", 2: "" };
  return {
    fd_write(fd, iovs, n, written) {
      const m = mem();
      let total = 0;
      for (let i = 0; i < n; i++) {
        const p = m.getUint32(iovs + i * 8, true), len = m.getUint32(iovs + i * 8 + 4, true);
        lines[fd] = (lines[fd] || "") + text.decode(new Uint8Array(memory().buffer, p, len));
        total += len;
      }
      let nl;
      while ((nl = lines[fd].indexOf("\n")) >= 0) {
        log(fd, lines[fd].slice(0, nl));
        lines[fd] = lines[fd].slice(nl + 1);
      }
      m.setUint32(written, total, true);
      return 0;
    },
    fd_close() { return 0; },
    fd_seek() { return 70; }, // ESPIPE
    fd_fdstat_get(fd, out) {
      const m = mem();
      m.setUint8(out, fd <= 2 ? 2 : 0);
      return 0;
    },
    fd_prestat_get() { return 8; }, // EBADF: no preopened directories
    fd_prestat_dir_name() { return 8; },
    fd_read() { return 8; },
    fd_readdir() { return 8; },
    fd_filestat_get() { return 8; },
    path_open() { return 8; },
    path_filestat_get() { return 8; },
    path_create_directory() { return 8; },
    path_remove_directory() { return 8; },
    path_rename() { return 8; },
    path_unlink_file() { return 8; },
    environ_sizes_get(count, size) { const m = mem(); m.setUint32(count, 0, true); m.setUint32(size, 0, true); return 0; },
    environ_get() { return 0; },
    args_sizes_get(count, size) { const m = mem(); m.setUint32(count, 0, true); m.setUint32(size, 0, true); return 0; },
    args_get() { return 0; },
    clock_time_get(id, precision, out) {
      const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
      mem().setBigUint64(out, ns, true);
      return 0;
    },
    random_get(p, n) { crypto.getRandomValues(new Uint8Array(memory().buffer, p, n)); return 0; },
    proc_exit(code) { throw new Error("asuka: the core exited (" + code + ")"); },
    sched_yield() { return 0; },
    poll_oneoff() { return 52; }, // ENOSYS
  };
}

/**
 * startCore loads the core into the page and connects it to host. Options: wasm (the
 * core's URL), module (bytecode to run: asuka dev's app), dev (asuka dev's page: the module
 * loads as its reloads do, and core.reload loads the next one keeping the app's state and
 * views), log (where the core's output goes).
 */
export async function startCore(host, options = {}) {
  let instance;
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
      // The zone's offset from UTC at a time (Unix seconds), in seconds east: asuka/time's
      // local zone.
      tz_offset: (unix) => -new Date(unix * 1000).getTimezoneOffset() * 60,
    },
    wasi_snapshot_preview1: wasi(memory, log),
  };
  const url = options.wasm || "asuka.wasm";
  const r = await WebAssembly.instantiateStreaming(fetch(url), imports).catch(async () => {
    const bytes = await (await fetch(url)).arrayBuffer();
    return WebAssembly.instantiate(bytes, imports);
  });
  instance = r.instance;
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
      try { ok(devLoad(bytes)); } catch (err) { fail(err); }
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
  e.asuka_web_start();
  if (options.module) {
    // asuka dev: the app's bytecode.
    const res = await fetch(options.module);
    if (!res.ok) throw new Error("asuka: the app's module: " + (await res.text()));
    const bytes = new Uint8Array(await res.arrayBuffer());
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
