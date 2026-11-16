// spawn's pool (src/spawn.js), with workers that answer as the test says:
// started as jobs need them, up to the pool's size; a job queued until
// one is free; a cancelled job dropped before it starts, its answer
// dropped after; a worker whose app doesn't start failing its jobs; the
// next module (asuka dev's) loaded before the next job; a worker that
// broke on a job replaced.
import { test } from "node:test";
import assert from "node:assert/strict";
import { Pool } from "../src/spawn.js";
import * as fs from "../src/fs.js";

const enc = new TextEncoder();
const dec = new TextDecoder();
const tick = () => new Promise((r) => setTimeout(r, 2));

// fakeWorker answers init with ready (or fail(init)'s error) and each job
// once the test calls finish(id, answer).
function workers({ fail } = {}) {
  const made = [];
  const make = () => {
    const w = {
      inits: [], jobs: [], onmessage: null, terminated: false,
      postMessage(m) {
        if (m.init) {
          w.inits.push(m.init);
          const error = fail?.(m.init);
          setTimeout(() => w.onmessage({ data: error ? { ready: false, error } : { ready: true } }), 0);
        }
        if (m.job) w.jobs.push(m.job);
      },
      terminate() { w.terminated = true; },
      finish(id, text, extra = {}) { w.onmessage({ data: { done: { id, status: 0, bytes: enc.encode(text).buffer, files: null, ...extra } } }); },
    };
    made.push(w);
    return w;
  };
  return { made, make };
}

function pool(opts = {}) {
  const ws = workers(opts);
  const done = [];
  const logs = [];
  const p = new Pool({ wasm: "core", module: opts.module ?? null, appDir: "/data/x", size: opts.size ?? 2, done: (id, status, b) => done.push([id, status, dec.decode(b)]), log: (l) => logs.push(l), makeWorker: ws.make });
  return { p, ws, done, logs };
}

test("workers start as jobs need them, up to the size, and take jobs as they're free", async () => {
  fs.writeFile("/data/x/kept", enc.encode("k"));
  const { p, ws, done } = pool({ size: 2 });
  assert.equal(ws.made.length, 0, "none before a job");
  p.run(1, new Uint8Array([1]));
  p.run(2, new Uint8Array([2]));
  p.run(3, new Uint8Array([3]));
  assert.equal(ws.made.length, 1, "one starting at a time");
  assert.deepEqual(ws.made[0].inits[0].appDir, "/data/x");
  await tick();
  assert.equal(ws.made.length, 2);
  await tick();
  const [a, b] = ws.made;
  assert.deepEqual([a.jobs.map((j) => j.id), b.jobs.map((j) => j.id)], [[1], [2]]);
  assert.ok(a.inits[0].files.some(([p]) => p === "/data/x/kept"), "a new worker's copy of the files comes as it starts");
  a.finish(1, "one");
  assert.deepEqual(a.jobs.map((j) => j.id), [1, 3], "the free worker takes the next");
  b.finish(2, "two");
  a.finish(3, "three");
  assert.deepEqual(done, [[1, 0, "one"], [2, 0, "two"], [3, 0, "three"]]);
  assert.equal(ws.made.length, 2);
});

test("a cancelled job never starts, or its answer goes nowhere", async () => {
  const { p, ws, done } = pool({ size: 1 });
  p.run(1, new Uint8Array([1]));
  p.run(2, new Uint8Array([2]));
  await tick();
  p.cancel(2); // queued
  p.cancel(1); // running
  ws.made[0].finish(1, "late");
  assert.deepEqual(done, []);
  assert.deepEqual(ws.made[0].jobs.map((j) => j.id), [1]);
  p.run(3, new Uint8Array([3]));
  ws.made[0].finish(3, "three");
  assert.deepEqual(done, [[3, 0, "three"]]);
});

test("a worker whose app doesn't start fails its jobs, saying why", async () => {
  const { p, ws, done, logs } = pool({ fail: () => "asuka: init: boom" });
  p.run(1, new Uint8Array([1]));
  p.run(2, new Uint8Array([2]));
  await tick();
  assert.equal(ws.made[0].terminated, true);
  assert.deepEqual(done.map(([id, st, m]) => [id, st, m]), [
    [1, 2, "spawn: the app didn't start in a worker: asuka: init: boom"],
    [2, 2, "spawn: the app didn't start in a worker: asuka: init: boom"],
  ]);
  assert.equal(logs.length, 1);
});

test("asuka dev's next module loads in each worker before its next job", async () => {
  const one = new Uint8Array([1]), two = new Uint8Array([2]);
  const { p, ws, done } = pool({ size: 1, module: one });
  p.run(1, new Uint8Array([1]));
  await tick();
  ws.made[0].finish(1, "old");
  p.reload(two);
  p.run(2, new Uint8Array([2]));
  await tick();
  const w = ws.made[0];
  assert.deepEqual(w.inits.map((i) => i.module), [one, two]);
  assert.deepEqual(w.jobs.map((j) => j.id), [1, 2]);
  w.finish(2, "new");
  assert.deepEqual(done.map(([, , m]) => m), ["old", "new"]);
});

test("a worker that broke on its job goes, and the next job has another", async () => {
  const { p, ws, done } = pool({ size: 1 });
  p.run(1, new Uint8Array([1]));
  await tick();
  ws.made[0].finish(1, "the work failed: unreachable", { status: 2, dead: true });
  assert.equal(ws.made[0].terminated, true);
  p.run(2, new Uint8Array([2]));
  await tick();
  assert.equal(ws.made.length, 2);
  ws.made[1].finish(2, "ok");
  assert.deepEqual(done, [[1, 2, "the work failed: unreachable"], [2, 0, "ok"]]);
});
