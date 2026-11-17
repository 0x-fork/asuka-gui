// Workers' Web Workers (src/workers.js), with Web Workers that answer as
// the test says: one per worker, started with its init and then the job
// that makes its state; its calls sent one at a time, in order; stopped,
// it ends once its queue ran, or, cancelled, its queued calls answer
// CANCELLED and it ends once the running one answered; one whose app
// doesn't start fails its calls.
import { test } from "node:test";
import assert from "node:assert/strict";
import { Workers } from "../src/workers.js";

const enc = new TextEncoder();
const dec = new TextDecoder();
const tick = () => new Promise((r) => setTimeout(r, 2));
const CANCELLED = 3;

function fakes({ fail } = {}) {
  const made = [];
  const make = () => {
    const w = {
      messages: [], jobs: [], onmessage: null, terminated: false,
      postMessage(m) {
        w.messages.push(Object.keys(m)[0]);
        if (m.init) {
          const error = fail?.(m.init);
          setTimeout(() => w.onmessage({ data: error ? { ready: false, error } : { ready: true } }), 0);
        }
        if (m.job) w.jobs.push(m.job.id);
      },
      terminate() { w.terminated = true; },
      finish(id, text) { w.onmessage({ data: { done: { id, status: 0, bytes: enc.encode(text).buffer, files: null } } }); },
    };
    made.push(w);
    return w;
  };
  return { made, make };
}

function workers(opts = {}) {
  const f = fakes(opts);
  const done = [];
  const logs = [];
  const ws = new Workers({ wasm: "core", appDir: "/data/x", done: (id, status, b) => done.push([id, status, dec.decode(b)]), log: (l) => logs.push(l), makeWorker: f.make });
  return { ws, f, done, logs };
}

test("a worker is a Web Worker of its own, its init then its state's job, its calls one at a time", async () => {
  const { ws, f, done } = workers();
  ws.start(1, new Uint8Array([1]));
  ws.start(2, new Uint8Array([2]));
  assert.equal(f.made.length, 2, "one each");
  const [a, b] = f.made;
  assert.deepEqual(a.messages, ["init", "start"]);
  ws.call(1, 10, new Uint8Array([10]));
  ws.call(1, 11, new Uint8Array([11]));
  ws.call(2, 20, new Uint8Array([20]));
  assert.deepEqual(a.jobs, [], "none before it's ready");
  await tick();
  assert.deepEqual([a.jobs, b.jobs], [[10], [20]], "the first call of each");
  a.finish(10, "ten");
  assert.deepEqual(a.jobs, [10, 11], "the next once it's answered");
  a.finish(11, "eleven");
  b.finish(20, "twenty");
  assert.deepEqual(done, [[10, 0, "ten"], [11, 0, "eleven"], [20, 0, "twenty"]]);
});

test("stopped, it runs what's queued, then ends", async () => {
  const { ws, f, done } = workers();
  ws.start(1, new Uint8Array([1]));
  ws.call(1, 10, new Uint8Array([10]));
  ws.call(1, 11, new Uint8Array([11]));
  ws.stop(1, false);
  await tick();
  const [w] = f.made;
  w.finish(10, "a");
  assert.equal(w.terminated, false);
  w.finish(11, "b");
  assert.equal(w.terminated, true);
  assert.deepEqual(done.map((d) => d[0]), [10, 11]);
  ws.call(1, 12, new Uint8Array([12]));
  assert.deepEqual(done[2], [12, CANCELLED, ""], "a call after it went");
});

test("cancelled, its queued calls answer CANCELLED, and it ends once the running one answered", async () => {
  const { ws, f, done } = workers();
  ws.start(1, new Uint8Array([1]));
  for (const id of [10, 11, 12]) ws.call(1, id, new Uint8Array([id]));
  await tick();
  const [w] = f.made;
  assert.deepEqual(w.jobs, [10]);
  ws.stop(1, true);
  assert.deepEqual(done, [[11, CANCELLED, ""], [12, CANCELLED, ""]]);
  assert.equal(w.terminated, false, "the running call finishes");
  w.finish(10, "ran");
  assert.deepEqual(done[2], [10, 0, "ran"]);
  assert.equal(w.terminated, true);
  assert.deepEqual(w.jobs, [10]);
});

test("a stop that waits for its queue, cancelled, stops at once", async () => {
  const { ws, f, done } = workers();
  ws.start(1, new Uint8Array([1]));
  for (const id of [10, 11]) ws.call(1, id, new Uint8Array([id]));
  ws.stop(1, false);
  ws.stop(1, true);
  assert.deepEqual(done, [[10, CANCELLED, ""], [11, CANCELLED, ""]]);
  assert.equal(f.made[0].terminated, true);
});

test("a worker whose app doesn't start fails its calls", async () => {
  const { ws, done, logs } = workers({ fail: () => "no module" });
  ws.start(1, new Uint8Array([1]));
  ws.call(1, 10, new Uint8Array([10]));
  await tick();
  ws.call(1, 11, new Uint8Array([11]));
  assert.deepEqual(done.map((d) => d[0]), [10, 11]);
  assert.match(done[0][2], /no module/);
  assert.equal(done[0][1], 2);
  assert.ok(logs.length > 0);
});
