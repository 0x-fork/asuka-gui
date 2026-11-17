// The page's SQLite worker: SQLite's WebAssembly module (sqlite-engine.js)
// and its files (sqlite-vfs.js), which keep an app's databases in the
// origin private file system (else IndexedDB: sqlite-store.js). The page
// (sqlite.js) sends it the core's calls (core/src/sqlite/sqlite.c's on
// the web), each a whole statement, which it runs one after another and
// answers as the core's thread does on the other platforms: a query's
// rows gathered over its chunks into one answer, with the tables it read
// and wrote.
//
// Messages: { init: { wasm } } loads the module (its URL); { roots }
// adds data directories, whose databases are kept; { call: { id, op,
// conn, input, migrate } } is a call, answered { id, answer } unless its
// id is 0 (one the page doesn't wait for); { cancel: id } drops a call
// that hasn't run (it's answered "cancelled"). { moved: path } tells the
// page a database an earlier version kept in its files is here now.
//
// conn is the page's number for a connection: the page doesn't wait for
// OPEN. A connection that didn't open fails every call with why; a call
// the page didn't wait for that failed fails the connection's next call
// with its error (that call doesn't run), and is logged.
//
// A database another tab holds answers SQLITE_BUSY at once (SQLite can't
// sleep in a worker that must answer the lock's messages): a statement
// outside a transaction runs again, waiting a little longer each time,
// until it gets the database or its busy timeout passes.

import { Engine, Reader, Writer } from "./sqlite-engine.js";
import { VFS } from "./sqlite-vfs.js";
import { IDBStore, OPFSStore, openIDB, opfsRoot } from "./sqlite-store.js";

// The core's ops (sqlite.c's WEB_*), and asq's.
const OPEN = 1, CLOSE = 2, EXEC = 3, QUERY = 4, MIGRATE = 5;
const ASQ_NEXT = 5, ASQ_READS = 1;
const BUSY = 5, CANTOPEN = 14;
const CHUNK = 1 << 20;

const roots = new Set();
const kept = (path) => {
  for (const r of roots) if (path.startsWith(r + "/")) return true;
  return false;
};

let vfs = null, engine = null, ready = null;
const conns = new Map(); // the page's conn -> { handle, busy, error, deferred }
const cancelled = new Set();
let queue = Promise.resolve();
let say = (m, transfer) => postMessage(m, transfer);

/** init loads the module, with the best store the browser has. */
async function init({ wasm, module }) {
  const res = module ?? fetch(wasm, { credentials: "same-origin" });
  let store = null;
  const root = await opfsRoot();
  if (root) store = (path) => new OPFSStore(async () => root, path);
  else if (globalThis.indexedDB) {
    const idb = openIDB();
    idb.catch(() => {});
    store = (path) => new IDBStore(idb, path);
  } else {
    console.warn("std/sqlite: no origin private file system or IndexedDB here: databases last only as long as the page");
  }
  vfs = new VFS({ kept, store });
  engine = await Engine.load(await res, vfs);
}

const okAnswer = (payload) => {
  const b = new Uint8Array(1 + payload.length);
  b[0] = 1;
  b.set(payload, 1);
  return b;
};
const errAnswer = (message) => new Writer().u8(0).string(message).bytes();

// An answer of asq's, read: ok, its body, its tables; or its error.
function parse(a) {
  const r = new Reader(a);
  const ok = r.u8() === 1;
  const off = r.u32();
  if (!ok) {
    const code = r.u32();
    const autocommit = r.u8() === 1;
    const message = r.string();
    return { ok, code, autocommit, message: `sqlite: ${message} (${code})` };
  }
  return { ok, body: a.subarray(5, off), tables: a.subarray(off) };
}

// sets reads a set of names (u32 n, strings) from r: its bytes.
function set(r) {
  const at = r.p;
  const n = r.u32();
  for (let i = 0; i < n; i++) r.string();
  return r.b.subarray(at, r.p);
}

function call(op, handle, arg, input) {
  try {
    return engine.call(op, handle, arg, input, CHUNK);
  } finally {
    vfs.afterCall();
  }
}

// exec runs an EXEC: its tables written (u32 n, strings), or its error.
function exec(c, input) {
  const a = parse(call(EXEC, c.handle, 0, input));
  if (!a.ok) return a;
  return { ok: true, payload: set(new Reader(a.tables)).slice() };
}

// query runs a QUERY and its NEXTs: one rows answer (sqlite.c's).
function query(c, input) {
  let a = parse(call(QUERY, c.handle, 0, input));
  if (!a.ok) return a;
  const body = new Reader(a.body);
  const cursor = body.u32();
  const ncols = body.u32();
  const w = new Writer().u32(ncols);
  for (let i = 0; i < ncols; i++) {
    w.string(body.string());
    body.string(); // the declared type
  }
  const rowsAt = w.length;
  w.u32(0);
  let rows = 0;
  const written = [], read = [];
  for (;;) {
    const done = body.u8();
    const n = body.u32();
    rows += n;
    w.raw(body.b.subarray(body.p));
    const t = new Reader(a.tables);
    for (const out of [written, read]) {
      const k = t.u32();
      for (let i = 0; i < k; i++) out.push(t.string());
    }
    if (done || !cursor) break;
    a = parse(call(ASQ_NEXT, c.handle, cursor, new Uint8Array([ASQ_READS])));
    if (!a.ok) return a;
    body.b = a.body;
    body.v = new DataView(a.body.buffer, a.body.byteOffset, a.body.byteLength);
    body.p = 0;
  }
  w.setU32(rowsAt, rows);
  w.u32(written.length);
  for (const s of written) w.string(s);
  w.u32(read.length);
  for (const s of read) w.string(s);
  return { ok: true, payload: w.bytes() };
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// retry runs f again while another tab holds the database, outside a
// transaction, until its busy timeout.
async function retry(c, id, f) {
  const start = Date.now();
  let wait = 1;
  for (;;) {
    const r = f();
    if (r.ok || (r.code & 0xff) !== BUSY || !r.autocommit || Date.now() - start >= c.busy || cancelled.has(id)) return r;
    await sleep(wait);
    wait = Math.min(2 * wait, 100);
  }
}

const sql = (s) => new Writer().u8(0).string(s).u32(0).bytes();

// migrate runs migrate's steps from PRAGMA user_version on (sqlite.c's
// migrateConn, as the other platforms run it).
async function migrate(c, input) {
  const r = new Reader(input);
  const steps = [];
  for (let n = r.u32(), i = 0; i < n; i++) steps.push(r.string());
  const v0 = await retry(c, 0, () => query(c, sql("pragma user_version")));
  if (!v0.ok) return v0;
  const rows = new Reader(v0.payload);
  for (let n = rows.u32(), i = 0; i < n; i++) rows.string();
  let v = 0;
  if (rows.u32() > 0 && rows.u8() === 1) v = Number(rows.i64());
  for (; v < steps.length; v++) {
    const fail = (e) => ({ ok: false, message: `sqlite: migration ${v + 1}: ${e.message}` });
    const begin = await retry(c, 0, () => exec(c, sql("begin immediate")));
    if (!begin.ok) return fail(begin);
    for (const s of [steps[v], `pragma user_version = ${v + 1}`, "commit"]) {
      const e = exec(c, sql(s));
      if (!e.ok) {
        exec(c, sql("rollback"));
        return fail(e);
      }
    }
  }
  return { ok: true, payload: new Uint8Array(4) };
}

async function open(conn, input, migrateFrom) {
  const r = new Reader(input);
  const busy = r.u32();
  const path = r.string();
  const c = { handle: 0, busy, error: null, deferred: null };
  conns.set(conn, c);
  try {
    if (kept(path) && vfs.store) {
      // Held as SQLite opens it (it reads the header then): another
      // call's end may have let it go to another tab meanwhile.
      let took;
      do took = await vfs.prepare(path, migrateFrom);
      while (!vfs.databases.get(path)?.held);
      if (migrateFrom) say({ moved: path, took: !!took });
    }
    const a = parse(call(OPEN, 0, 0, input));
    if (!a.ok) c.error = a.message;
    else c.handle = Number(new Reader(a.body).u64());
  } catch (e) {
    c.error = `sqlite: ${path}: ${e?.message ?? e} (${e?.code > 0 ? e.code : CANTOPEN})`;
  }
  if (c.error) console.error("std/sqlite:", c.error);
}

async function run({ id, op, conn, input, migrate: migrateFrom }) {
  let result;
  try {
    await ready;
    const b = new Uint8Array(input ?? new ArrayBuffer(0));
    if (op === OPEN) return void (await open(conn, b, migrateFrom));
    const c = conns.get(conn);
    if (op === CLOSE) {
      conns.delete(conn);
      if (c?.handle) call(CLOSE, c.handle, 0, new Uint8Array(0));
      return;
    }
    if (!c) result = { ok: false, message: "sqlite: the database is closed" };
    else if (c.error) result = { ok: false, message: c.error };
    else if (id && c.deferred) {
      result = { ok: false, message: c.deferred };
      c.deferred = null;
    } else if (cancelled.has(id)) result = { ok: false, message: "sqlite: cancelled" };
    else if (op === EXEC) result = await retry(c, id, () => exec(c, b));
    else if (op === QUERY) result = await retry(c, id, () => query(c, b));
    else if (op === MIGRATE) result = await migrate(c, b);
    else result = { ok: false, message: `sqlite: no op ${op}` };
    if (!id && !result.ok && c && !c.error) {
      c.deferred ??= result.message;
      console.error("std/sqlite:", result.message);
    }
  } catch (e) {
    result = { ok: false, message: `sqlite: ${e?.message ?? e}` };
  } finally {
    cancelled.delete(id);
  }
  if (!id) return;
  const answer = result.ok ? okAnswer(result.payload) : errAnswer(result.message);
  say({ id, answer: answer.buffer }, [answer.buffer]);
}

/** handle takes one of the page's messages (the worker's onmessage; the tests call it). */
export function handle(m, post) {
  if (post) say = post;
  if (m.init) {
    ready = init(m.init).catch((e) => {
      console.error("std/sqlite: SQLite didn't load:", e);
      throw new Error(`SQLite didn't load: ${e?.message ?? e}`);
    });
    ready.catch(() => {}); // each call says it
  }
  if (m.roots) for (const r of m.roots) roots.add(r);
  if (m.cancel) cancelled.add(m.cancel);
  if (m.call) {
    const c = m.call;
    queue = queue.then(() => run(c));
  }
}

if (typeof WorkerGlobalScope !== "undefined" && globalThis instanceof WorkerGlobalScope) {
  onmessage = (e) => handle(e.data);
}

