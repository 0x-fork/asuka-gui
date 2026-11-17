// std/websocket's web client (std/websocket/web/websocket.js): the
// WebSocket Node has as a browser does, against internal/testserver's
// echo: messages both ways, the protocol it chose, its closes and ours,
// and a connection that fails. node --test platform/web.
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import net from "node:net";
import * as native from "../src/native.js";
import { startServer } from "../testing/testserver.js";

globalThis.asuka = { native };
const { FrameKind } = await import("../../../std/websocket/web/websocket.asuka.js");
const { makeClient } = await import("../../../std/websocket/web/websocket.js");

let server, wsURL;
before(async () => {
  server = await startServer();
  wsURL = server.url.replace(/^http/, "ws") + "/ws";
});
after(() => server.stop());

const none = () => new AbortController().signal;
const connect = (protocols = [], url = wsURL, timeout = 5000, signal = none()) =>
  makeClient().connect(signal, url, { "X-Token": "ignored" }, protocols, timeout);

test("echo: text and binary, in order; the protocol the server chose", async () => {
  const s = await connect(["chat", "other"]);
  assert.equal(s.protocol(), "chat");
  await s.sendText(none(), "hello");
  await s.sendBinary(none(), new Uint8Array([1, 2, 3]));
  await s.sendText(none(), "héllo ✓");
  // No header of the app's from a browser: the echo comes first.
  assert.deepEqual(await s.receive(none()), { kind: FrameKind.text, text: "hello", data: new Uint8Array(0), code: 0 });
  const b = await s.receive(none());
  assert.equal(b.kind, FrameKind.binary);
  assert.deepEqual([...b.data], [1, 2, 3]);
  assert.equal((await s.receive(none())).text, "héllo ✓");
  s.close();
  const c = await s.receive(none());
  assert.equal(c.kind, FrameKind.closed);
});

test("no protocol asked for: none", async () => {
  const s = await connect();
  assert.equal(s.protocol(), "");
  s.close();
});

test("the server closes: its code and reason, then always that", async () => {
  const s = await connect();
  await s.sendText(none(), "close please");
  const f = await s.receive(none());
  assert.deepEqual(f, { kind: FrameKind.closed, text: "bye", data: new Uint8Array(0), code: 4000 });
  assert.deepEqual(await s.receive(none()), f);
  await assert.rejects(s.sendText(none(), "late"), /closed/);

  const n = await connect();
  await n.sendText(none(), "close normally");
  const g = await n.receive(none());
  assert.equal(g.kind, FrameKind.closed);
  assert.equal(g.code, 1000);
});

test("shutdown: the client's close, answered with its code", async () => {
  const s = await connect();
  const waiting = s.receive(none());
  s.shutdown(4001, "done");
  assert.deepEqual(await waiting, { kind: FrameKind.closed, text: "done", data: new Uint8Array(0), code: 4001 });

  // A code a browser refuses from a page closes normally.
  const t = await connect();
  t.shutdown(1001, "going");
  const f = await t.receive(none());
  assert.equal(f.kind, FrameKind.closed);
  assert.equal(f.code, 1000);
});

test("receive: cancelled, then the next message still comes", async () => {
  const s = await connect();
  const ctl = new AbortController();
  const p = s.receive(ctl.signal);
  ctl.abort();
  await assert.rejects(p, { name: "AbortError" });
  await s.sendText(none(), "after");
  assert.equal((await s.receive(none())).text, "after");
  s.close();
});

test("connect: refused, timed out, cancelled", async () => {
  // A port nothing listens on.
  const free = await new Promise((resolve) => {
    const l = net.createServer().listen(0, "127.0.0.1", () => {
      const { port } = l.address();
      l.close(() => resolve(port));
    });
  });
  await assert.rejects(connect([], `ws://127.0.0.1:${free}/ws`), /can't connect/);
  // Not a WebSocket: the server answers 404.
  await assert.rejects(connect([], server.url.replace(/^http/, "ws") + "/status/404"), /can't connect/);

  // A server that takes the connection and never answers.
  const held = [];
  const silent = net.createServer((c) => held.push(c));
  await new Promise((resolve) => silent.listen(0, "127.0.0.1", resolve));
  const url = `ws://127.0.0.1:${silent.address().port}/`;
  await assert.rejects(connect([], url, 100), { name: "TimeoutError" });
  const ctl = new AbortController();
  const p = connect([], url, 0, ctl.signal);
  setTimeout(() => ctl.abort(), 50);
  await assert.rejects(p, { name: "AbortError" });
  for (const c of held) c.destroy();
  silent.close();
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

test("on the wire: connect's Socket and its frames, as the core reads them", async () => {
  const c = Number(ok(native.makeObject("std/websocket", 0, {})).uint64());
  const req = new native.NativeWriter().string(wsURL).len(0).len(1).string("chat").duration(5000).bytes();
  const s = Number(ok(await startCall(c, 1, req)).uint64());
  assert.equal(ok(native.call(s, 1, new Uint8Array(0), {})).string(), "chat");
  ok(await startCall(s, 3, new native.NativeWriter().string("close please").bytes()));
  const f = ok(await startCall(s, 2));
  assert.equal(f.int(), FrameKind.closed);
  assert.equal(f.string(), "bye");
  assert.equal(f.data().length, 0);
  assert.equal(f.int(), 4000);
  native.release(s);
  native.release(c);
  // A Socket is only ever connect's.
  assert.equal(new native.NativeReader(native.makeObject("std/websocket", 1, {})).uint8(), 3);
});
