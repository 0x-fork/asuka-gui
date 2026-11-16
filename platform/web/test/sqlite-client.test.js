// The page's side of SQLite (src/sqlite.js), as core.js calls it for
// the core (core/src/sqlite/sqlite.c on the web), with a worker that
// answers what the test says; and the worker's side (src/sqlite-worker.js)
// with SQLite's module: calls the core doesn't wait for run before the
// ones after them, their errors fail the next call, a query's rows come
// as one answer, migrate's steps, a cancelled call.
import { test } from "node:test";
import assert from "node:assert/strict";
import { Client } from "../src/sqlite.js";
import { handle } from "../src/sqlite-worker.js";
import { Reader, Writer } from "../src/sqlite-engine.js";
import { loadWasm } from "../testing/asqlite.js";
import * as fs from "../src/fs.js";

const OPEN = 1, CLOSE = 2, EXEC = 3, QUERY = 4, MIGRATE = 5, CANCEL = 8;
const tick = () => new Promise((r) => setTimeout(r, 5));

// fakeWorker answers each call that wants one with reply(call), a moment later.
function fakeWorker(reply) {
  const w = {
    sent: [],
    postMessage(m) {
      w.sent.push(m);
      if (m.call?.id) setTimeout(() => w.onmessage({ data: { id: m.call.id, answer: reply(m.call).buffer } }), 0);
    },
  };
  return w;
}

test("calls go to the worker in order; answers come back to theirs; a cancel goes at once", async () => {
  let worker;
  const c = new Client("http://x/lib/asqlite.wasm", () => (worker = fakeWorker((call) => new Uint8Array([1, call.id]))));
  c.root("/data/app");
  assert.equal(worker.sent[0].init.wasm, "http://x/lib/asqlite.wasm");
  assert.deepEqual(worker.sent[1], { roots: ["/data/app"] });
  const got = [];
  c.send(0, OPEN, 1, new Writer().u32(5000).string(":memory:").bytes(), null);
  c.send(7, EXEC, 1, new Uint8Array([9]), (b) => got.push([...b]));
  c.send(0, CANCEL, 0, new Writer().i64(7).bytes(), null);
  assert.deepEqual(worker.sent.slice(2).map((m) => m.call ? [m.call.id, m.call.op] : m), [[0, OPEN], [7, EXEC], { cancel: 7 }]);
  await tick();
  assert.deepEqual(got, [[1, 7]]);
});

test("a database the page's files kept goes to the worker, and leaves them once it's moved", async () => {
  fs.writeFile("/data/app2/old.db", new Uint8Array([1, 2, 3]));
  fs.writeFile("/data/app2/old.db-journal", new Uint8Array([4]));
  let worker;
  const c = new Client("http://x/lib/asqlite.wasm", () => (worker = fakeWorker(() => new Uint8Array([1]))));
  c.root("/data/app2");
  c.send(0, OPEN, 1, new Writer().u32(0).string("/data/app2/old.db").bytes(), null);
  const m = worker.sent.at(-1).call;
  assert.deepEqual([...m.migrate.db], [1, 2, 3]);
  assert.deepEqual([...m.migrate.journal], [4]);
  assert.ok(fs.readFile("/data/app2/old.db"), "until it's moved");
  worker.onmessage({ data: { moved: "/data/app2/old.db", took: true } });
  assert.equal(fs.readFile("/data/app2/old.db"), null);
  assert.equal(fs.readFile("/data/app2/old.db-journal"), null);
});

test("a worker that won't start answers each call with an error", async () => {
  const c = new Client("http://x/lib/asqlite.wasm", () => { throw new Error("no workers here"); });
  let got = null;
  c.send(3, EXEC, 1, new Uint8Array(0), (b) => { got = b; });
  await tick();
  const r = new Reader(got);
  assert.equal(r.u8(), 0);
  assert.match(r.string(), /SQLite's worker didn't start: no workers here/);
});

// --- The worker, with SQLite's module ---

const stmt = (sql, flags = 0) => new Writer().u8(flags).string(sql).u32(0).bytes();

function worker() {
  const answers = new Map();
  const post = (m) => { if (m.id) answers.set(m.id, new Uint8Array(m.answer)); };
  let next = 100;
  const call = (op, conn, input, wait = true) => {
    const id = wait ? ++next : 0;
    handle({ call: { id, op, conn, input: input.slice().buffer } }, post);
    return id;
  };
  const answer = async (id) => {
    while (!answers.has(id)) await tick();
    const b = answers.get(id);
    const r = new Reader(b);
    return r.u8() === 1 ? { ok: true, r } : { ok: false, message: r.string() };
  };
  return { call, answer };
}

test("the worker: statements in order, rows in one answer, unseen errors on the next call, migrate, cancel", async () => {
  const module = await WebAssembly.compile(await loadWasm());
  handle({ init: { module } }, () => {});
  const w = worker();
  w.call(OPEN, 1, new Writer().u32(5000).string(":memory:").bytes(), false);
  w.call(MIGRATE, 1, new Writer().u32(2).string("create table t (id integer primary key, s text)").string("insert into t (s) values ('a'), ('b')").bytes(), false);
  const q = await w.answer(w.call(QUERY, 1, stmt("select id, s from t order by id", 1)));
  assert.ok(q.ok);
  const r = q.r;
  assert.equal(r.u32(), 2);
  assert.deepEqual([r.string(), r.string()], ["id", "s"]);
  assert.equal(r.u32(), 2, "two rows");
  assert.deepEqual([r.u8(), r.i64(), r.u8(), r.string()], [1, 1n, 3, "a"]);
  assert.deepEqual([r.u8(), r.i64(), r.u8(), r.string()], [1, 2n, 3, "b"]);
  assert.equal(r.u32(), 0, "wrote nothing");
  assert.equal(r.u32(), 1);
  assert.equal(r.string(), "t", "read t");
  const v = await w.answer(w.call(QUERY, 1, stmt("pragma user_version", 1)));
  v.r.u32(), v.r.string(), v.r.u32(), v.r.u8();
  assert.equal(v.r.i64(), 2n);

  // An exec answers the tables it wrote.
  const e = await w.answer(w.call(EXEC, 1, stmt("insert into t (s) values ('c')")));
  assert.deepEqual([e.r.u32(), e.r.string()], [1, "t"]);

  // A call not waited for that failed: the next call fails with it, then calls go on.
  w.call(EXEC, 1, stmt("insert into nowhere values (1)"), false);
  const failed = await w.answer(w.call(EXEC, 1, stmt("insert into t (s) values ('d')")));
  assert.deepEqual(failed, { ok: false, message: "sqlite: no such table: nowhere (1)" });
  const count = await w.answer(w.call(QUERY, 1, stmt("select count(*) from t", 1)));
  count.r.u32(), count.r.string(), count.r.u32(), count.r.u8();
  assert.equal(count.r.i64(), 3n, "the failed call's next didn't run");

  // A migration that failed fails the next call, saying which step.
  w.call(OPEN, 2, new Writer().u32(5000).string("").bytes(), false);
  w.call(MIGRATE, 2, new Writer().u32(1).string("this isn't sql").bytes(), false);
  const m = await w.answer(w.call(EXEC, 2, stmt("select 1")));
  assert.deepEqual(m, { ok: false, message: "sqlite: migration 1: sqlite: near \"this\": syntax error (1)" });

  // Cancelled before it runs.
  const id = w.call(QUERY, 1, stmt("select 1", 1));
  handle({ cancel: id });
  assert.deepEqual(await w.answer(id), { ok: false, message: "sqlite: cancelled" });
  w.call(CLOSE, 1, new Uint8Array(0), false);
  assert.deepEqual(await w.answer(w.call(EXEC, 1, stmt("select 1"))), { ok: false, message: "sqlite: the database is closed" });
});
