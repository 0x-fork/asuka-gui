// The web host's batch decoder (platform/web/src/ops.js), timed under
// node on batches like those the core sends: a thousand rows made (each
// a row, two texts and a button: created, inserted, their props and
// frames set), and the same rows' labels changed.
//
//   node bench/ui/web_decode.mjs
import { decode } from "../../platform/web/src/ops.js";

const enc = new TextEncoder();

function batch(rows, makeRows) {
  const parts = [];
  let n = 0;
  const u8 = (v) => parts.push(Uint8Array.of(v));
  const u16 = (v) => { const b = new Uint8Array(2); new DataView(b.buffer).setUint16(0, v, true); parts.push(b); };
  const u32 = (v) => { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, v, true); parts.push(b); };
  const f32 = (v) => { const b = new Uint8Array(4); new DataView(b.buffer).setFloat32(0, v, true); parts.push(b); };
  const str = (s) => { const b = enc.encode(s); u32(b.length); parts.push(b); };
  const op = (code, id) => { n++; u8(code); u32(id); };
  const words = ["pretty", "large", "big", "small", "red", "yellow", "table", "chair", "house", "pizza"];
  let id = 100;
  for (let i = 0; i < rows; i++) {
    const row = id++, num = id++, label = id++, button = id++;
    const text = `${words[i % 10]} ${words[(i * 7) % 10]} ${words[(i * 3) % 10]}`;
    if (makeRows) {
      op(1, row); u16(1);
      op(1, num); u16(2);
      op(5, num); u16(1); u8(4); str(String(i + 1));
      op(1, label); u16(2);
      op(5, label); u16(1); u8(4); str(text);
      op(1, button); u16(3);
      op(5, button); u16(1); u8(4); str("×");
      op(7, button); u16(1);
      op(5, row); u16(20); u8(6); f32(4); f32(8); f32(4); f32(8);
      for (const c of [num, label, button]) { op(3, row); u32(c); u32(0); }
      op(3, 1); u32(row); u32(i);
      op(6, row); f32(0); f32(i * 28); f32(390); f32(28);
      op(6, num); f32(8); f32(4); f32(60); f32(20);
      op(6, label); f32(76); f32(4); f32(250); f32(20);
      op(6, button); f32(334); f32(0); f32(48); f32(28);
    } else {
      op(5, label); u16(1); u8(4); str(text + " !!!");
    }
  }
  const head = new Uint8Array(4);
  new DataView(head.buffer).setUint32(0, n, true);
  const len = parts.reduce((a, p) => a + p.length, 4);
  const out = new Uint8Array(len);
  out.set(head, 0);
  let o = 4;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return { bytes: out, ops: n };
}

let sink = 0;
const nop = {
  create(id, k) { sink += id ^ k; }, destroy(id) { sink += id; }, insert(p, c, i) { sink += p ^ c ^ i; }, remove(p, c) { sink += p ^ c; },
  setProp(id, p, t, v) { sink += typeof v === "string" ? v.length : 1; }, setFrame(id, x, y, w, h) { sink += x + y + w + h; },
  listen(id, e) { sink += e; }, unlisten(id, e) { sink += e; }, command(id, c, a) { sink += a.length; }, setRoot(id) { sink += id; },
};

function time(name, { bytes, ops }) {
  for (let i = 0; i < 200; i++) decode(bytes, nop);  // warm up
  const runs = [];
  for (let r = 0; r < 15; r++) {
    const k = 200, t0 = process.hrtime.bigint();
    for (let i = 0; i < k; i++) decode(bytes, nop);
    runs.push(Number(process.hrtime.bigint() - t0) / k);
  }
  runs.sort((a, b) => a - b);
  const med = runs[runs.length >> 1];
  console.log(`${name}: ${ops} ops, ${(bytes.length / 1024).toFixed(1)} KB: ${(med / 1e3).toFixed(1)} µs a batch, ${(med / ops).toFixed(1)} ns an op (fastest ${(runs[0] / ops).toFixed(1)})`);
}

time("1k rows made", batch(1000, true));
time("1k labels changed", batch(1000, false));
if (sink === 42) console.log(sink);
