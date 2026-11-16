// SQLite on the page, for asuka/sqlite: the page's side of its SQLite
// worker (sqlite-worker.js), which runs SQLite's WebAssembly module
// (the sqlite plugin's asqlite.wasm, which the CLI ships with the apps
// that use asuka/sqlite) and keeps databases in the origin private file
// system. The core's calls (core/src/sqlite/sqlite.c on the web) come
// through core.js: send(id, op, conn, input, answer) posts one to the
// worker, and its answer comes back to answer(bytes) (none for id 0: a
// call the core doesn't wait for).
//
// A database an earlier version kept in the page's files (fs.js, in
// IndexedDB) moves to the worker's store as it's first opened.

import * as fs from "./fs.js";
import { Reader, Writer } from "./sqlite-engine.js";

const OPEN = 1, CANCEL = 8;

let client = null;

/** load starts the page's SQLite worker once, with its module (asqlite.wasm) at the URL wasm. */
export function load(wasm) {
  client ??= new Client(wasm);
  return Promise.resolve(client);
}

const failed = (why) => new Writer().u8(0).string(why).bytes();

export class Client {
  constructor(wasm, makeWorker = (url) => new Worker(url, { type: "module" })) {
    this.waiting = new Map(); // call id -> answer handler
    this.broken = null; // why the worker doesn't answer
    this.dirs = new Set();
    try {
      this.worker = makeWorker(new URL("./sqlite-worker.js", import.meta.url));
      this.worker.onmessage = (e) => this.message(e.data);
      this.worker.onerror = (e) => this.fail(e.message || "the worker failed");
      this.worker.postMessage({ init: { wasm: new URL(wasm).href } });
    } catch (e) {
      this.fail(e.message);
    }
  }

  fail(why) {
    this.broken = `sqlite: SQLite's worker didn't start: ${why}`;
    console.error("asuka/sqlite:", this.broken);
    const waiting = [...this.waiting.values()];
    this.waiting.clear();
    for (const f of waiting) f(failed(this.broken));
  }

  message(m) {
    if (m.moved) {
      // Moved: no longer the page's file.
      if (m.took) {
        fs.removeFile(m.moved);
        fs.removeFile(m.moved + "-journal");
      }
      return;
    }
    const f = this.waiting.get(m.id);
    this.waiting.delete(m.id);
    f?.(new Uint8Array(m.answer));
  }

  /** roots says the app's data directory (dir): its databases are kept. */
  root(dir) {
    if (!dir || this.dirs.has(dir)) return;
    this.dirs.add(dir);
    this.worker?.postMessage({ roots: [dir] });
  }

  /** send posts a call of the core's (input: its bytes, the page's copy); answer(bytes) gets its answer, unless id is 0. */
  send(id, op, conn, input, answer) {
    if (op === CANCEL) {
      this.worker?.postMessage({ cancel: Number(new Reader(input).u64()) });
      return;
    }
    if (this.broken) {
      if (id) queueMicrotask(() => answer(failed(this.broken)));
      return;
    }
    if (id) this.waiting.set(id, answer);
    const transfer = [input.buffer];
    let migrate = null;
    if (op === OPEN) {
      const r = new Reader(input);
      r.u32();
      const path = r.string();
      for (const dir of this.dirs) {
        if (!path.startsWith(dir + "/")) continue;
        const db = fs.readFile(path);
        if (db?.length) {
          migrate = { db: db.slice(), journal: fs.readFile(path + "-journal")?.slice() ?? null };
          transfer.push(migrate.db.buffer);
          if (migrate.journal) transfer.push(migrate.journal.buffer);
        }
      }
    }
    this.worker.postMessage({ call: { id, op, conn, input: input.buffer, migrate } }, transfer);
  }
}
