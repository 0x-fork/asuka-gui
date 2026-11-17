// Workers on the web (the language's worker: core/src/rt/worker.c): a
// Web Worker each, running the same core with the same app as spawn's
// (spawn-worker.js), its init run without a UI. The page's core starts
// one with the job that makes its state there (worker_start), then hands
// it calls (worker_call), jobs as spawn's whose function takes that
// state; they run one at a time, in the order they were made, each
// answered with its result or error (asuka_spawn_done, as spawn's). A
// worker is sent a call once the one before it was answered, so that one
// stopped with its calls cancelled (its owner went) answers those still
// here with CANCELLED; the one running finishes first.
//
// The app's files go with the calls, as with spawn's jobs (fs.js).
//
//	const workers = new Workers({ wasm, module: bytes, appDir, done: (job, status, bytes) => … });
//	workers.start(id, job); workers.call(id, job, bytes); workers.stop(id, cancel);

import * as fs from "./fs.js";
import { workerId } from "./spawn.js";

const FAILED = 2; // web.c's ANSWER_FAILED: a message, not a result
const CANCELLED = 3; // ANSWER_CANCELLED: a call its worker's owner went before it ran

export class Workers {
  /**
   * The options are spawn.js's Pool's: wasm (the core, a WebAssembly.Module),
   * module (the app's bytecode, null: compiled in), appDir, sqlite, crash,
   * done(job, status, bytes) (what gets each call's answer), log and makeWorker.
   */
  constructor({ wasm, module = null, appDir = "", sqlite = false, crash = null, done, log = (line) => console.error(line), makeWorker = (url) => new Worker(url, { type: "module" }) }) {
    this.wasm = wasm;
    this.module = module;
    this.appDir = appDir;
    this.sqlite = sqlite;
    this.crash = crash;
    this.done = done;
    this.log = log;
    this.makeWorker = makeWorker;
    this.workers = new Map(); // the core's id: { id, w, ready, broken, queue, job, stopping, synced }
  }

  /** start makes worker id, whose init's job (bytes) makes its state. */
  start(id, bytes) {
    const w = { id: workerId(), w: null, ready: false, broken: null, queue: [], job: null, stopping: false, synced: 0 };
    this.workers.set(id, w);
    try {
      w.w = this.makeWorker(new URL("./spawn-worker.js", import.meta.url));
    } catch (e) {
      this.broke(w, `a worker's Web Worker didn't start: ${e?.message ?? e}`);
      return;
    }
    w.w.onmessage = (e) => this.message(id, w, e.data);
    w.w.onerror = (e) => {
      e?.preventDefault?.();
      this.broke(w, `a worker's Web Worker failed: ${e?.message ?? e}`);
    };
    w.w.postMessage({ init: { wasm: this.wasm, module: this.module, appDir: this.appDir, sqlite: this.sqlite, crash: this.crash?.(), files: fs.changesFor(w) } });
    w.w.postMessage({ start: { bytes } }, [bytes.buffer]);
  }

  /** call queues a call (job, its bytes) for worker id. */
  call(id, job, bytes) {
    const w = this.workers.get(id);
    if (!w || w.stopping) {
      this.done(job, CANCELLED, new Uint8Array(0));
      return;
    }
    if (w.broken) {
      this.fail(job, w.broken);
      return;
    }
    w.queue.push({ id: job, bytes });
    this.dispatch(w);
  }

  /**
   * stop ends worker id once its calls queued ran, or, cancel, once the
   * one running did (those queued answered CANCELLED).
   */
  stop(id, cancel) {
    const w = this.workers.get(id);
    if (!w) return;
    w.stopping = true;
    if (cancel) {
      const q = w.queue;
      w.queue = [];
      for (const j of q) this.done(j.id, CANCELLED, new Uint8Array(0));
    }
    this.end(id, w);
  }

  /** reload makes the workers started from now on the next module's (asuka dev's): those running keep theirs. */
  reload(module) { this.module = module; }

  /** close ends every worker. */
  close() {
    for (const w of this.workers.values()) w.w?.terminate();
    this.workers.clear();
  }

  dispatch(w) {
    if (!w.ready || w.job || !w.queue.length) return;
    const job = (w.job = w.queue.shift());
    const files = fs.changesFor(w);
    w.w.postMessage({ job: { id: job.id, bytes: job.bytes, files, crash: this.crash?.() } }, [job.bytes.buffer]);
  }

  // end lets a stopping worker go once nothing is queued or running.
  end(id, w) {
    if (!w.stopping || w.job || w.queue.length) return;
    w.w?.terminate();
    this.workers.delete(id);
  }

  message(id, w, m) {
    if (m.log) {
      this.log(m.log);
      return;
    }
    if (m.ready !== undefined) {
      if (!m.ready) {
        this.broke(w, `a worker: the app didn't start in its Web Worker: ${m.error}`);
        return;
      }
      w.ready = true;
      this.dispatch(w);
      return;
    }
    if (m.done) {
      const { id: job, status, bytes, files, dead } = m.done;
      w.job = null;
      fs.take(w.id, files);
      this.done(job, status, new Uint8Array(bytes));
      if (dead) {
        this.broke(w, "a worker's Web Worker broke on a call");
        return;
      }
      this.dispatch(w);
      this.end(id, w);
    }
  }

  // broke: worker w can't go on (why): its calls, queued and to come, fail.
  broke(w, why) {
    this.log(why);
    w.broken = why;
    w.w?.terminate();
    const q = w.job ? [w.job, ...w.queue] : w.queue;
    w.job = null;
    w.queue = [];
    for (const j of q) this.fail(j.id, why);
  }

  fail(job, why) { this.done(job, FAILED, new TextEncoder().encode(why)); }
}
