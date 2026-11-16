// The web host's system modules (src/modules/system*.js): their
// arguments and results as the core and the packages over asuka/host
// encode and decode them (ui.ShareFiles, compose, notify), and the MIME
// types of files.js.
import { test } from "node:test";
import assert from "node:assert/strict";
import { decodeShare, smsURL } from "../src/modules/system.js";
import { WaitQueue } from "../src/modules/system-notify.js";
import { typeOf } from "../src/files.js";

const enc = new TextEncoder();

// Go's little-endian u32 and u32-length strings.
function u32(n) { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, n, true); return b; }
function str(s) { const b = enc.encode(s); return concat(u32(b.length), b); }
function concat(...bs) {
  const out = new Uint8Array(bs.reduce((n, b) => n + b.length, 0));
  let p = 0;
  for (const b of bs) { out.set(b, p); p += b.length; }
  return out;
}

test("share.files' arguments (ui.ShareFiles)", () => {
  const b = concat(u32(42), str("application/pdf"), str("com.adobe.pdf"), str("Invoice"), u32(2), str("/data/app/a.pdf"), str("/tmp/b.pdf"));
  assert.deepEqual(decodeShare(b), { view: 42, mime: "application/pdf", uti: "com.adobe.pdf", title: "Invoice", paths: ["/data/app/a.pdf", "/tmp/b.pdf"] });
});

test("compose.message's sms: link", () => {
  assert.equal(smsURL(["+15551234", "+15555678"], "Hi there", false), "sms:+15551234,+15555678?body=Hi%20there");
  assert.equal(smsURL(["+15551234"], "Hi", true), "sms:+15551234&body=Hi");
  assert.equal(smsURL([], "", false), "sms:");
});

test("MIME types from names", () => {
  assert.equal(typeOf("/tmp/shared/1/Photo.JPG"), "image/jpeg");
  assert.equal(typeOf("https://example.com/a.pdf?x=1"), "application/pdf");
  assert.equal(typeOf("README"), "");
});

test("notify's wait queue: kept until asked, stopped with an error", async () => {
  const q = new WaitQueue();
  q.deliver("first");
  assert.equal(await q.next(), "first");
  const p = q.next();
  q.deliver("second");
  assert.equal(await p, "second");
  const w = q.next();
  q.stop();
  await assert.rejects(w, /stopped/);
});
