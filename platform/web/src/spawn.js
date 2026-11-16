// spawn on the web: the page's Web Workers, a few, started as spawn first
// needs them. Each runs the same core (asuka.wasm's compiled module,
// posted to it) with the same app (asuka dev's bytecode, or the app
// compiled in), whose init it runs without a UI (spawn-worker.js); then
// the jobs the page's core hands out (core/src/abi/web.c: a closure's
// function by its index, its captures encoded), one at a time, each
// answered with its result or error. The page's core settles the spawn's
// future with the answer (asuka_spawn_done).
//
// The app's files go with the jobs: a worker has a copy of the page's
// file system (fs.js), sent what changed since its last job with the
// next one, and its job's changes come back with its answer.
//
//	const pool = new Pool({ wasm, module: bytes, appDir, done: (id, status, bytes) => … });
//	pool.run(id, job);

import * as fs from "./fs.js";

const FAILED = 2; // web.c's ANSWER_FAILED: a message, not a result

let lastWorker = 0;

/** size is how many workers a page may have: the cores the browser says it has, but the page's own, up to 4. */
export const poolSize = () => Math.max(1, Math.min(4, (globalThis.navigator?.hardwareConcurrency || 2) - 1));

export class Pool {
  /**
   * wasm is the core (a WebAssembly.Module), module the app's bytecode
   * (null: compiled in), appDir its data directory, sqlite whether the
   * page has SQLite (a worker's init opens the app's databases as the
   * page's does, which goes nowhere there), crash() where the page's
   * crash record is and which runs' pages are open (crash.js's), done(id, status,
   * bytes) what gets each job's answer. makeWorker(url) makes a worker
   * (Node's tests give their own).
   */
  constructor({ wasm, module = null, appDir = "", sqlite = false, crash = null, size = poolSize(), done, log = (line) => console.error(line), makeWorker = (url) => new Worker(url, { type: "module" }) }) {
    this.wasm = wasm;
    this.module = module;
    this.appDir = appDir;
    this.sqlite = sqlite;
    this.crash = crash;
    this.size = size;
    this.done = done;
    this.log = log;
    this.makeWorker = makeWorker;
    this.queue = []; // { id, bytes, module }
    this.workers = []; // { id, w, module, ready, job, synced }
    this.cancelled = new Set();
  }

  /** run queues a job (its bytes, the page's copy) for the next free worker. */
  run(id, bytes) {
    this.queue.push({ id, bytes, module: this.module });
    this.dispatch();
  }

  /** cancel drops a job that hasn't started; one that has runs to its end, and its answer goes nowhere. */
  cancel(id) {
    const i = this.queue.findIndex((j) => j.id === id);
    if (i >= 0) this.queue.splice(i, 1);
    else if (this.workers.some((w) => w.job?.id === id)) this.cancelled.add(id);
  }

  /** reload makes the jobs from now on the next module's (asuka dev's): each worker loads it before its next job. */
  reload(module) { this.module = module; }

  /** close ends the workers. */
  close() {
    for (const w of this.workers) w.w.terminate();
    this.workers = [];
    this.queue = [];
  }

  dispatch() {
    while (this.queue.length) {
      const job = this.queue[0];
      // A worker free with the job's module, else one free to load it.
      let w = this.workers.find((w) => w.ready && !w.job && w.module === job.module) || this.workers.find((w) => w.ready && !w.job);
      if (!w) {
        if (this.workers.length < this.size && !this.workers.some((w) => !w.ready)) this.start();
        return;
      }
      this.queue.shift();
      if (w.module !== job.module) {
        this.init(w, job.module);
        this.queue.unshift(job);
        continue;
      }
      w.job = job;
      const files = fs.changesFor(w);
      w.w.postMessage({ job: { id: job.id, bytes: job.bytes, files, crash: this.crash?.() } }, [job.bytes.buffer]);
    }
  }

  start() {
    const w = { id: ++lastWorker, w: null, module: undefined, ready: false, job: null, synced: 0 };
    try {
      w.w = this.makeWorker(new URL("./spawn-worker.js", import.meta.url));
    } catch (e) {
      this.broken(`spawn's worker didn't start: ${e?.message ?? e}`);
      return;
    }
    w.w.onmessage = (e) => this.message(w, e.data);
    w.w.onerror = (e) => {
      e?.preventDefault?.();
      this.lost(w, `spawn's worker failed: ${e?.message ?? e}`);
    };
    this.workers.push(w);
    this.init(w, this.queue[0]?.module ?? this.module, fs.changesFor(w));
  }

  init(w, module, files = null) {
    w.ready = false;
    w.module = module;
    w.w.postMessage({ init: { wasm: this.wasm, module, appDir: this.appDir, sqlite: this.sqlite, crash: this.crash?.(), files } });
  }

  message(w, m) {
    if (m.log) {
      this.log(m.log);
      return;
    }
    if (m.ready !== undefined) {
      if (!m.ready) {
        // The app's init failed there: its jobs fail with why, and the
        // worker goes (the next job starts another).
        this.lost(w, `spawn: the app didn't start in a worker: ${m.error}`, w.module);
        return;
      }
      w.ready = true;
      this.dispatch();
      return;
    }
    if (m.done) {
      const { id, status, bytes, files, dead } = m.done;
      w.job = null;
      fs.take(w.id, files);
      if (dead) this.retire(w);
      if (this.cancelled.delete(id)) {
        this.dispatch();
        return;
      }
      this.done(id, status, new Uint8Array(bytes));
      this.dispatch();
    }
  }

  // lost: worker w can't go on (why). Its job fails, and those queued for
  // module (when the module is what failed).
  lost(w, why, module) {
    this.log(why);
    const job = w.job;
    this.retire(w);
    if (job && !this.cancelled.delete(job.id)) this.fail(job.id, why);
    if (module !== undefined) {
      const failing = this.queue.filter((j) => j.module === module);
      this.queue = this.queue.filter((j) => j.module !== module);
      for (const j of failing) this.fail(j.id, why);
    }
    this.dispatch();
  }

  retire(w) {
    w.w.terminate();
    this.workers = this.workers.filter((x) => x !== w);
  }

  // broken: no worker can start: what's queued fails.
  broken(why) {
    this.log(why);
    const q = this.queue;
    this.queue = [];
    for (const j of q) this.fail(j.id, why);
  }

  fail(id, why) { this.done(id, FAILED, new TextEncoder().encode(why)); }
}
