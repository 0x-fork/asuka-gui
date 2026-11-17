// std/http's web client (std/http/web/http.js), a body read as it comes:
// Transport.open answers with the head, Body.read with each piece the
// network delivers; a timeout, a cancel and close stop it. Against
// internal/testserver: node --test platform/web.
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import * as native from "../src/native.js";
import { startServer } from "../testing/testserver.js";

globalThis.asuka = { native };
await import("../../../std/http/web/http.asuka.js");
const { makeTransport } = await import("../../../std/http/web/http.js");

let server;
before(async () => { server = await startServer(); });
after(() => server.stop());

const none = () => new AbortController().signal;
const open = (path, timeout = 5000, signal = none()) => makeTransport().open(signal, "GET", server.url + path, {}, new Uint8Array(0), timeout);
const text = (b) => new TextDecoder().decode(b);

test("open: the head as soon as it's in, the body's pieces as they come", async () => {
  const b = await open("/stream");
  const h = b.head();
  assert.equal(h.status, 200);
  assert.equal(h.url, server.url + "/stream");
  assert.equal(h.headers["content-type"], "text/plain; charset=utf-8");
  assert.equal(h.body.length, 0);
  // The first piece is in before the body ends (it takes 150 ms).
  const first = await b.read(none());
  assert.equal(text(first), "one\n");
  const pieces = [first];
  for (;;) {
    const p = await b.read(none());
    if (!p.length) break;
    pieces.push(p);
  }
  assert.ok(pieces.length >= 2, `pieces: ${pieces.map(text)}`);
  assert.equal(pieces.map(text).join(""), "one\ntwo\nthree\n");
  // At the end, always empty.
  assert.equal((await b.read(none())).length, 0);
  b.close();
});

test("open: a status that isn't a success opens too", async () => {
  const b = await open("/status/404");
  assert.equal(b.head().status, 404);
  let all = "";
  for (let p; (p = await b.read(none())).length;) all += text(p);
  assert.equal(all, "missing");
  b.close();
});

test("open: a POST's body and headers", async () => {
  const b = await makeTransport().open(none(), "POST", server.url + "/echo", { "X-Asuka": "yes" }, new TextEncoder().encode("hi"), 5000);
  let all = "";
  for (let p; (p = await b.read(none())).length;) all += text(p);
  const echo = JSON.parse(all);
  assert.equal(echo.method, "POST");
  assert.equal(echo.body, "hi");
  assert.equal(echo.headers["x-asuka"], "yes");
});

test("read: no piece within the timeout throws TimeoutError", async () => {
  const b = await open("/stall", 200);
  assert.equal(b.head().status, 200);
  await assert.rejects(b.read(none()), { name: "TimeoutError" });
  b.close();
});

test("cancelled: open's and read's signals abort the request; close stops it", async () => {
  const ctl = new AbortController();
  const b = await open("/stall", 0, ctl.signal);
  // open's signal is done with once it answered.
  ctl.abort();
  const read = new AbortController();
  const p = b.read(read.signal);
  setTimeout(() => read.abort(), 50);
  await assert.rejects(p, { name: "AbortError" });

  const c = await open("/stall", 0);
  const q = c.read(none());
  c.close();
  await assert.rejects(q, { name: "AbortError" });
  await assert.rejects(c.read(none()), /closed/);

  // A server that never answers: open itself cancelled.
  const ctl2 = new AbortController();
  ctl2.abort();
  await assert.rejects(open("/stream", 0, ctl2.signal), { name: "AbortError" });
});

// The core's side of a call: its answer.
function startCall(h, method, input = new Uint8Array(0)) {
  return new Promise((resolve) => {
    native.start(h, method, input, 1, { done: (_, b) => resolve(b), invoke: () => null });
  });
}

function ok(b) {
  const r = new native.NativeReader(b);
  assert.equal(r.uint8(), 0, `status of ${[...b]}`);
  return r;
}

test("on the wire: open's Body, its head and its pieces, as the core reads them", async () => {
  const t = Number(ok(native.makeObject("std/http", 0, {})).uint64());
  const req = new native.NativeWriter().string("GET").string(server.url + "/stream").len(0).data(new Uint8Array(0)).duration(5000).bytes();
  const body = Number(ok(await startCall(t, 3, req)).uint64());
  assert.ok(body > 0);
  const r = ok(native.call(body, 1, new Uint8Array(0), {}));
  assert.equal(r.int(), 200);
  assert.equal(r.string(), server.url + "/stream");
  const n = r.len();
  const headers = {};
  for (let i = 0; i < n; i++) headers[r.string()] = r.string();
  assert.equal(headers["content-type"], "text/plain; charset=utf-8");
  assert.equal(r.data().length, 0);
  assert.equal(text(ok(await startCall(body, 2)).data()), "one\n");
  ok(native.call(body, 3, new Uint8Array(0), {}));
  native.release(body);
  native.release(t);
  // A Body is only ever open's.
  assert.equal(new native.NativeReader(native.makeObject("std/http", 1, {})).uint8(), 3);
});
