// Typed native modules on the web (src/native.js): the pantry package's
// generated JavaScript (internal/nativegen/testdata/pantry/web) and its
// implementation, called as the core calls them, in core/src/native's
// wire format.
import test from "node:test";
import assert from "node:assert/strict";
import * as native from "../src/native.js";

globalThis.asuka = { native };
await import("../../../internal/nativegen/testdata/pantry/web/pantry.asuka.js");

const id = "@asuka/pantry";
const { NativeWriter, NativeReader } = native;

// The core's side: its answers, and its callbacks.
function coreSide(callbacks = {}) {
  const done = new Map();
  return {
    done: (call, b) => done.set(call, b),
    invoke: (cb, b) => callbacks[cb]?.(new NativeReader(b)) ?? null,
    answered: done,
  };
}

function ok(b) {
  const r = new NativeReader(b);
  assert.equal(r.uint8(), 0, `status of ${[...b]}`);
  return r;
}

function make(back) {
  return Number(ok(native.makeObject(id, 0, back)).uint64());
}

test("calls with every kind of value, and errors", () => {
  const back = coreSide();
  const h = make(back);
  assert.equal(ok(native.call(h, 1, new Uint8Array(0), back)).string(), "home");

  const r = ok(native.call(h, 2, new NativeWriter().string("x").bytes(), back));
  assert.equal(r.len(), 1);
  assert.equal(r.string(), "x");
  assert.equal(r.float64(), 1.5);
  assert.equal(r.int64(), 1n); // Level.high
  assert.equal(r.len(), 2);
  assert.deepEqual([r.string(), r.string()], ["a", "b"]);
  assert.equal(r.len(), 1);
  assert.deepEqual([r.string(), r.string()], ["k", "v"]);
  assert.equal(r.present(), true); // next
  assert.equal(r.string(), "inner");
  assert.equal(r.float64(), 0);
  assert.equal(r.int64(), 0n);
  assert.equal(r.len(), 0);
  assert.equal(r.len(), 0);
  assert.equal(r.present(), false);
  assert.deepEqual([...r.data()], []);
  assert.equal(r.duration(), 0);
  assert.equal(r.int64(), 0n);
  assert.equal(r.bool(), false);
  assert.deepEqual([...r.data()], [1, 2, 3]);
  assert.equal(r.duration(), 2500);
  assert.equal(r.int64(), -3n);
  assert.equal(r.bool(), true);

  // A declared error: its index.
  assert.deepEqual([...native.call(h, 3, itemBytes(""), back)], [1, 0, 0, 0, 0]);
  assert.deepEqual([...native.call(h, 3, itemBytes("y"), back)], [0]);

  // An error it doesn't declare: its name and message.
  const none = new NativeReader(native.call(h, 6, new NativeWriter().string("none").bytes(), back));
  assert.equal(none.uint8(), 2);
  assert.equal(none.string(), "RangeError");
  assert.equal(none.int64(), 0n);
  assert.equal(none.string(), "no shelf");

  // An object, its method, its declared error; released, it's gone.
  const shelf = Number(ok(native.call(h, 6, new NativeWriter().string("main").bytes(), back)).uint64());
  assert.deepEqual([...native.call(shelf, 1, new NativeWriter().int(3).int(1).bytes(), back)], [0]);
  assert.deepEqual([...native.call(shelf, 1, new NativeWriter().int(11).int(1).bytes(), back)], [1, 1, 0, 0, 0]);
  native.release(shelf);
  assert.equal(new NativeReader(native.call(shelf, 1, new NativeWriter().int(1).int(0).bytes(), back)).uint8(), 2);

  const counts = ok(native.call(h, 8, new Uint8Array(0), back));
  assert.equal(counts.len(), 2);
  assert.deepEqual([counts.string(), counts.int64(), counts.string(), counts.int64()], ["a", 1n, "b", -2n]);
});

test("a question callback answers on the call's thread", () => {
  let asked;
  const back = coreSide({ 7: (r) => { asked = r.string(); return new NativeWriter().bool(true).bytes(); } });
  const h = make(back);
  assert.equal(ok(native.call(h, 5, new NativeWriter().uint64(7).bytes(), back)).bool(), true);
  assert.equal(asked, "ok?");
});

test("an async call answers, its progress notified, and cancels", async () => {
  const progress = [];
  const back = coreSide({ 3: (r) => { progress.push(r.float64()); return null; } });
  const h = make(back);
  native.start(h, 4, new NativeWriter().string("soup").uint64(3).bytes(), 1, back);
  await new Promise((r) => setTimeout(r, 10));
  const r = ok(back.answered.get(1));
  assert.equal(r.string(), "soup done");
  assert.deepEqual(progress, [0.5, 1]);

  native.start(h, 4, new NativeWriter().string("slow").uint64(0).bytes(), 2, back);
  native.cancel(2);
  await new Promise((r) => setTimeout(r, 10));
  assert.deepEqual([...back.answered.get(2)], [4]);
});

// itemBytes is an Item named name, the rest zero.
function itemBytes(name) {
  return new NativeWriter().string(name).float64(0).int(0).len(0).len(0).present(false).data(new Uint8Array(0))
    .duration(0).int(0).bool(false).bytes();
}
