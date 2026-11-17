// The core's WASI calls over the app's files (src/wasi.js, src/fs.js),
// made as wasi-libc makes them, on a memory of the test's own; and the
// copies spawn's workers keep (changesFor, take, replica, since).
import { test } from "node:test";
import assert from "node:assert/strict";
import { wasi } from "../src/wasi.js";
import * as fs from "../src/fs.js";

const enc = new TextEncoder();
const dec = new TextDecoder();
const O_CREAT = 1, O_DIRECTORY = 2, O_EXCL = 4, O_TRUNC = 8;
const READ = 1n << 1n, WRITE = 1n << 6n;
const ENOENT = 44, EEXIST = 20, ENOTEMPTY = 55, EISDIR = 31;

function setup() {
  const memory = new WebAssembly.Memory({ initial: 2 });
  const logged = [];
  const w = wasi(() => memory, (fd, line) => logged.push([fd, line]));
  const u8 = () => new Uint8Array(memory.buffer);
  const dv = () => new DataView(memory.buffer);
  let top = 1024;
  const alloc = (n) => { const p = top; top += (n + 7) & ~7; return p; };
  const str = (s) => { const b = enc.encode(s); const p = alloc(b.length); u8().set(b, p); return [p, b.length]; };
  const iov = (bytes) => {
    const p = alloc(bytes.length), v = alloc(8);
    u8().set(bytes, p);
    dv().setUint32(v, p, true);
    dv().setUint32(v + 4, bytes.length, true);
    return v;
  };
  const open = (path, oflags, rights = READ | WRITE, fdflags = 0) => {
    const out = alloc(4);
    const [p, n] = str(path.replace(/^\//, ""));
    const r = w.path_open(3, 1, p, n, oflags, rights, rights, fdflags, out);
    return r === 0 ? dv().getUint32(out, true) : -r;
  };
  const write = (fd, text) => {
    const n = alloc(4);
    assert.equal(w.fd_write(fd, iov(enc.encode(text)), 1, n), 0);
    return dv().getUint32(n, true);
  };
  const read = (fd, cap = 100) => {
    const buf = new Uint8Array(cap);
    const v = iov(buf), n = alloc(4);
    assert.equal(w.fd_read(fd, v, 1, n), 0);
    const p = dv().getUint32(v, true);
    return dec.decode(u8().slice(p, p + dv().getUint32(n, true)));
  };
  const at = (dirfn, path, ...rest) => { const [p, n] = str(path.replace(/^\//, "")); return w[dirfn](3, p, n, ...rest); };
  return { w, memory, logged, alloc, str, open, write, read, at, dv, u8 };
}

test("files open, write, read, seek and stat as libc's calls make them", () => {
  const s = setup();
  fs.makeDir("/data/w1");
  const fd = s.open("/data/w1/a.txt", O_CREAT | O_TRUNC);
  assert.ok(fd > 3);
  assert.equal(s.write(fd, "hello"), 5);
  assert.equal(s.write(fd, " world"), 6);
  const pos = s.alloc(8);
  assert.equal(s.w.fd_seek(fd, 0n, 0, pos), 0);
  assert.equal(s.read(fd, 5), "hello");
  assert.equal(s.read(fd), " world");
  assert.equal(s.w.fd_close(fd), 0);
  assert.equal(dec.decode(fs.readFile("/data/w1/a.txt")), "hello world");
  // stat: a regular file of 11 bytes.
  const st = s.alloc(64);
  const [p, n] = s.str("data/w1/a.txt");
  assert.equal(s.w.path_filestat_get(3, 1, p, n, st), 0);
  assert.equal(s.dv().getUint8(st + 16), 4);
  assert.equal(s.dv().getBigUint64(st + 32, true), 11n);
  // Appending.
  const fa = s.open("/data/w1/a.txt", 0, WRITE, 1);
  s.write(fa, "!");
  s.w.fd_close(fa);
  assert.equal(dec.decode(fs.readFile("/data/w1/a.txt")), "hello world!");
  // What isn't there, and creating what is (exclusively).
  assert.equal(s.open("/data/w1/none", 0), -ENOENT);
  assert.equal(s.open("/data/w1/a.txt", O_CREAT | O_EXCL), -EEXIST);
  assert.equal(s.open("/data/w1", O_DIRECTORY, WRITE), -EISDIR);
  // Standard output and error go to the log, a line at a time.
  s.write(1, "one\ntw");
  s.write(1, "o\n");
  s.write(2, "oops\n");
  assert.deepEqual(s.logged, [[1, "one"], [1, "two"], [2, "oops"]]);
});

test("directories: made, listed, renamed and removed", () => {
  const s = setup();
  assert.equal(s.at("path_create_directory", "/data/w2"), 0);
  assert.equal(s.at("path_create_directory", "/data/w2"), EEXIST);
  assert.equal(s.at("path_create_directory", "/data/w2/sub"), 0);
  fs.writeFile("/data/w2/f", enc.encode("x"));
  const dir = s.open("/data/w2", O_DIRECTORY, READ);
  const buf = s.alloc(256), used = s.alloc(4);
  assert.equal(s.w.fd_readdir(dir, buf, 256, 0n, used), 0);
  const names = [];
  const b = s.u8(), d = s.dv();
  for (let at = buf, end = buf + d.getUint32(used, true); at < end;) {
    const len = d.getUint32(at + 16, true);
    names.push([dec.decode(b.slice(at + 24, at + 24 + len)), d.getUint8(at + 20)]);
    at += 24 + len;
  }
  assert.deepEqual(names, [[".", 3], ["..", 3], ["f", 4], ["sub", 3]]);
  // A small buffer gets what fits; the next call starts at the cookie.
  assert.equal(s.w.fd_readdir(dir, buf, 30, 2n, used), 0);
  assert.equal(d.getUint32(used, true), 30);
  assert.equal(s.at("path_remove_directory", "/data/w2"), ENOTEMPTY);
  const [fp, fn] = s.str("data/w2/sub"), [tp, tn] = s.str("data/w2/moved");
  assert.equal(s.w.path_rename(3, fp, fn, 3, tp, tn), 0);
  assert.deepEqual(fs.fsys.names("/data/w2"), ["f", "moved"]);
  assert.equal(s.at("path_unlink_file", "/data/w2/f"), 0);
  assert.equal(s.at("path_unlink_file", "/data/w2/f"), ENOENT);
  assert.equal(s.at("path_remove_directory", "/data/w2/moved"), 0);
  assert.deepEqual(fs.fsys.names("/data/w2"), []);
});

test("a worker's copy gets what changed since its last job, and its job's changes come back", () => {
  const w = { id: 77, synced: 0 };
  fs.writeFile("/data/w3/a", enc.encode("a"));
  const all = fs.changesFor(w);
  assert.ok(all.some(([p, e]) => p === "/data/w3/a" && dec.decode(e.data) === "a"), "a new copy has every file");
  assert.equal(fs.changesFor(w), null, "nothing since");
  fs.writeFile("/data/w3/b", enc.encode("b"));
  fs.removeFile("/data/w3/a");
  assert.deepEqual(fs.changesFor(w).map(([p, e]) => [p, e && dec.decode(e.data)]), [["/data/w3/b", "b"], ["/data/w3/a", null]]);
  // The worker's changes: kept and journaled as its, so not sent back to it.
  fs.take(77, [["/data/w3/c/d", { dir: false, mode: 0o100644, mtime: 1, size: 1, data: enc.encode("d") }], ["/data/w3/b", null]]);
  assert.equal(dec.decode(fs.readFile("/data/w3/c/d")), "d");
  assert.equal(fs.readFile("/data/w3/b"), null);
  assert.equal(fs.changesFor(w), null);
  const other = { id: 78, synced: 0 };
  const got = new Map(fs.changesFor(other));
  assert.ok(got.get("/data/w3/c")?.dir, "the directory it made");
  assert.equal(dec.decode(got.get("/data/w3/c/d").data), "d");
  // since: what changed after a mark (a worker's own job).
  const m = fs.mark();
  fs.fsys.write("/data/w3/c/d", fs.fsys.entry("/data/w3/c/d"), enc.encode("D"), 0);
  assert.deepEqual(fs.since(m).map(([p, e]) => [p, dec.decode(e.data)]), [["/data/w3/c/d", "D"]]);
});
