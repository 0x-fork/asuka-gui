// std/http's web client (std/http/web/http.js): a download read as it
// comes, its progress reported, written into the app's files; its
// timeout the longest wait for more (TimeoutError past it, not a cancel):
// node --test platform/web.
import { test } from "node:test";
import assert from "node:assert/strict";
import http from "node:http";
import { makeTransport } from "../../../std/http/web/http.js";

function serve(handler) {
  return new Promise((resolve) => {
    const s = http.createServer(handler);
    s.listen(0, "127.0.0.1", () => resolve(s));
  });
}

test("download: progress as it comes, the file written", async () => {
  const written = new Map();
  globalThis.asuka = { native: { fs: { writeFile: (p, b) => written.set(p, b) } } };
  const s = await serve(async (req, res) => {
    res.writeHead(200, { "Content-Length": String(4 * 65536), "X-Asuka": "yes" });
    for (let i = 0; i < 4; i++) {
      res.write(Buffer.alloc(65536, 97));
      await new Promise((r) => setTimeout(r, 150));
    }
    res.end();
  });
  const url = `http://127.0.0.1:${s.address().port}/big`;
  const reports = [];
  const r = await makeTransport().download(new AbortController().signal, "GET", url, {}, new Uint8Array(0), 5000, "/data/big.bin", (got, total) => reports.push([got, total]));
  s.close();
  assert.equal(r.status, 200);
  assert.equal(r.headers["x-asuka"], "yes");
  assert.equal(r.body.length, 0);
  assert.equal(written.get("/data/big.bin").length, 4 * 65536);
  assert.ok(reports.length >= 2, `reports: ${JSON.stringify(reports)}`);
  assert.deepEqual(reports.at(-1), [4 * 65536, 4 * 65536]);
  assert.ok(reports.slice(0, -1).every(([got, total]) => got <= 4 * 65536 && total === 4 * 65536));
});

test("download: cancelled, and a server that stalls past the timeout", async () => {
  globalThis.asuka = { native: { fs: { writeFile: () => assert.fail("nothing to write") } } };
  const s = await serve((req, res) => {
    res.writeHead(200, { "Content-Length": "10" });
    res.write("abc"); // then nothing
  });
  const url = `http://127.0.0.1:${s.address().port}/stall`;
  await assert.rejects(makeTransport().download(new AbortController().signal, "GET", url, {}, new Uint8Array(0), 200, "/x", () => {}), { name: "TimeoutError" });
  const ctl = new AbortController();
  const p = makeTransport().download(ctl.signal, "GET", url, {}, new Uint8Array(0), 0, "/x", () => {});
  setTimeout(() => ctl.abort(), 100);
  await assert.rejects(p, { name: "AbortError" });
  s.closeAllConnections();
  s.close();
});
