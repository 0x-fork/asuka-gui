// The WASI calls (preview 1) the core's C library makes, answered by the
// page (or a spawn worker, spawn-worker.js): the clock, randomness, the
// console, and files: the app's file system in memory (fs.js), "/"
// preopened as the core's one directory, so std/fs, the core's crash
// record and anything else that opens a path reach it.
//
//	const imports = { wasi_snapshot_preview1: wasi(() => instance.exports.memory, log) };

import { fsys, clean, fsError } from "./fs.js";

// WASI's errno values.
const E = {
  SUCCESS: 0, ACCES: 2, BADF: 8, BUSY: 10, EXIST: 20, INVAL: 28, IO: 29, ISDIR: 31, NOENT: 44,
  NOSYS: 52, NOTDIR: 54, NOTEMPTY: 55, NOTSUP: 58, PERM: 63, SPIPE: 70, XDEV: 75,
};
const errno = (e) => {
  const code = e?.code;
  if (code && code.startsWith("E") && E[code.slice(1)] !== undefined) return E[code.slice(1)];
  console.error("asuka: a file call failed:", e);
  return E.IO;
};

// Filetypes, open flags, fd flags, rights.
const DIRECTORY = 3, REGULAR = 4, CHAR = 2;
const O_CREAT = 1, O_DIRECTORY = 2, O_EXCL = 4, O_TRUNC = 8;
const FD_APPEND = 1;
const RIGHT_FD_READ = 1n << 1n, RIGHT_FD_WRITE = 1n << 6n;
const ALL_RIGHTS = (1n << 30n) - 1n;
const FST_MTIM = 1 << 2, FST_MTIM_NOW = 1 << 3;
const PREOPEN = 3; // "/"

/**
 * wasi makes the imports: memory() is the instance's memory (it may
 * grow: read through it each call), log(fd, line) gets what the core
 * writes to standard output (1) and error (2).
 */
export function wasi(memory, log) {
  const dv = () => new DataView(memory().buffer);
  const bytes = (p, n) => new Uint8Array(memory().buffer, p, n);
  const text = new TextDecoder();
  const enc = new TextEncoder();
  const lines = { 1: "", 2: "" };
  // Open files and directories: fd -> { path, e, pos, append, read, write, dir }
  const fds = new Map([[PREOPEN, { path: "/", dir: true, preopen: true }]]);
  let nextFD = PREOPEN + 1;

  const str = (p, n) => text.decode(bytes(p, n));
  const pathAt = (dirfd, p, n) => {
    const d = fds.get(dirfd);
    if (!d || !d.dir) return null;
    return clean(d.path + "/" + str(p, n));
  };
  const entryOf = (f) => (f.e && f.path ? f.e : fsys.entry(f.path));
  const iovecs = (iovs, n) => {
    const m = dv();
    const out = [];
    for (let i = 0; i < n; i++) out.push(bytes(m.getUint32(iovs + i * 8, true), m.getUint32(iovs + i * 8 + 4, true)));
    return out;
  };
  const filestat = (out, e) => {
    const m = dv();
    m.setBigUint64(out, 1n, true); // dev
    m.setBigUint64(out + 8, BigInt(e.ino ?? 0), true);
    m.setUint8(out + 16, e.dir ? DIRECTORY : REGULAR);
    m.setBigUint64(out + 24, 1n, true); // nlink
    m.setBigUint64(out + 32, BigInt(e.dir ? 0 : e.size), true);
    const t = BigInt(Math.round(e.mtime)) * 1000000n;
    m.setBigUint64(out + 40, t, true);
    m.setBigUint64(out + 48, t, true);
    m.setBigUint64(out + 56, t, true);
  };
  const times = (path, e, mtim, flags) => {
    if (flags & FST_MTIM_NOW) fsys.touch(path, e, Date.now());
    else if (flags & FST_MTIM) fsys.touch(path, e, Number(mtim / 1000000n));
  };
  // A call: errors thrown as fsErrors are their errno.
  const call = (f) => (...a) => {
    try {
      return f(...a) ?? E.SUCCESS;
    } catch (e) {
      return errno(e);
    }
  };

  const readInto = (f, iovs, n, at) => {
    const e = entryOf(f);
    if (!e) throw fsError("EBADF");
    if (e.dir) throw fsError("EISDIR");
    let total = 0;
    for (const b of iovecs(iovs, n)) {
      const got = fsys.read(e, b, at + total);
      total += got;
      if (got < b.length) break;
    }
    return total;
  };
  const writeFrom = (f, iovs, n, at) => {
    const e = entryOf(f);
    if (!e) throw fsError("EBADF");
    if (e.dir) throw fsError("EISDIR");
    let total = 0;
    for (const b of iovecs(iovs, n)) total += fsys.write(f.path, e, b.slice(), at + total);
    return total;
  };
  const console_ = (fd, iovs, n) => {
    let total = 0;
    for (const b of iovecs(iovs, n)) {
      lines[fd] += text.decode(b);
      total += b.length;
    }
    let nl;
    while ((nl = lines[fd].indexOf("\n")) >= 0) {
      log(fd, lines[fd].slice(0, nl));
      lines[fd] = lines[fd].slice(nl + 1);
    }
    return total;
  };

  return {
    fd_write: call((fd, iovs, n, written) => {
      let total;
      if (fd === 1 || fd === 2) total = console_(fd, iovs, n);
      else {
        const f = fds.get(fd);
        if (!f || f.dir) return E.BADF;
        if (!f.write) return E.BADF;
        const e = entryOf(f);
        const at = f.append ? e.size : f.pos;
        total = writeFrom(f, iovs, n, at);
        f.pos = at + total;
      }
      dv().setUint32(written, total, true);
    }),
    fd_pwrite: call((fd, iovs, n, offset, written) => {
      const f = fds.get(fd);
      if (!f || f.dir || !f.write) return E.BADF;
      dv().setUint32(written, writeFrom(f, iovs, n, Number(offset)), true);
    }),
    fd_read: call((fd, iovs, n, read) => {
      if (fd === 0) {
        dv().setUint32(read, 0, true);
        return;
      }
      const f = fds.get(fd);
      if (!f || !f.read) return E.BADF;
      const got = readInto(f, iovs, n, f.pos);
      f.pos += got;
      dv().setUint32(read, got, true);
    }),
    fd_pread: call((fd, iovs, n, offset, read) => {
      const f = fds.get(fd);
      if (!f || !f.read) return E.BADF;
      dv().setUint32(read, readInto(f, iovs, n, Number(offset)), true);
    }),
    fd_seek: call((fd, offset, whence, out) => {
      if (fd <= 2) return E.SPIPE;
      const f = fds.get(fd);
      if (!f || f.dir) return E.BADF;
      const base = whence === 0 ? 0 : whence === 1 ? f.pos : entryOf(f).size;
      const pos = base + Number(offset);
      if (pos < 0) return E.INVAL;
      f.pos = pos;
      dv().setBigUint64(out, BigInt(pos), true);
    }),
    fd_tell: call((fd, out) => {
      const f = fds.get(fd);
      if (!f || f.dir) return E.BADF;
      dv().setBigUint64(out, BigInt(f.pos), true);
    }),
    fd_close: call((fd) => {
      const f = fds.get(fd);
      if (fd <= 2) return;
      if (!f || f.preopen) return E.BADF;
      fds.delete(fd);
      if (f.write) fsys.closed(f.path);
    }),
    fd_sync: () => E.SUCCESS,
    fd_datasync: () => E.SUCCESS,
    fd_advise: () => E.SUCCESS,
    fd_allocate: call((fd, offset, len) => {
      const f = fds.get(fd);
      if (!f || !f.write) return E.BADF;
      const e = entryOf(f);
      const end = Number(offset) + Number(len);
      if (end > e.size) fsys.resize(f.path, e, end);
    }),
    fd_fdstat_get: call((fd, out) => {
      const m = dv();
      const f = fds.get(fd);
      if (fd > 2 && !f) return E.BADF;
      m.setUint8(out, fd <= 2 ? CHAR : f.dir ? DIRECTORY : REGULAR);
      m.setUint16(out + 2, f?.append ? FD_APPEND : 0, true);
      m.setBigUint64(out + 8, ALL_RIGHTS, true);
      m.setBigUint64(out + 16, ALL_RIGHTS, true);
    }),
    fd_fdstat_set_flags: call((fd, flags) => {
      const f = fds.get(fd);
      if (!f) return E.BADF;
      f.append = !!(flags & FD_APPEND);
    }),
    fd_fdstat_set_rights: () => E.SUCCESS,
    fd_filestat_get: call((fd, out) => {
      if (fd <= 2) {
        filestat(out, { dir: false, size: 0, mtime: 0, ino: fd });
        dv().setUint8(out + 16, CHAR);
        return;
      }
      const f = fds.get(fd);
      const e = f && entryOf(f);
      if (!e) return E.BADF;
      filestat(out, e);
    }),
    fd_filestat_set_size: call((fd, size) => {
      const f = fds.get(fd);
      if (!f || f.dir || !f.write) return E.BADF;
      fsys.resize(f.path, entryOf(f), Number(size));
    }),
    fd_filestat_set_times: call((fd, atim, mtim, flags) => {
      const f = fds.get(fd);
      const e = f && entryOf(f);
      if (!e) return E.BADF;
      times(f.path, e, mtim, flags);
    }),
    fd_prestat_get: call((fd, out) => {
      if (fd !== PREOPEN) return E.BADF;
      const m = dv();
      m.setUint8(out, 0); // a directory
      m.setUint32(out + 4, 1, true); // "/"
    }),
    fd_prestat_dir_name: call((fd, p, n) => {
      if (fd !== PREOPEN) return E.BADF;
      bytes(p, n).set(enc.encode("/").subarray(0, n));
    }),
    fd_readdir: call((fd, buf, len, cookie, used) => {
      const f = fds.get(fd);
      if (!f || !f.dir) return E.BADF;
      const names = [".", "..", ...fsys.names(f.path)];
      const out = new Uint8Array(len);
      let at = 0;
      for (let i = Number(cookie); i < names.length && at < len; i++) {
        const name = enc.encode(names[i]);
        const path = names[i] === "." ? f.path : names[i] === ".." ? clean(f.path + "/..") : clean(f.path + "/" + names[i]);
        const e = fsys.entry(path);
        const head = new Uint8Array(24);
        const hv = new DataView(head.buffer);
        hv.setBigUint64(0, BigInt(i + 1), true);
        hv.setBigUint64(8, BigInt(e?.ino ?? 0), true);
        hv.setUint32(16, name.length, true);
        hv.setUint8(20, e?.dir ? DIRECTORY : REGULAR);
        // As much as fits: libc reads again with a bigger buffer for a cut entry.
        const whole = new Uint8Array(24 + name.length);
        whole.set(head);
        whole.set(name, 24);
        const n = Math.min(whole.length, len - at);
        out.set(whole.subarray(0, n), at);
        at += n;
      }
      bytes(buf, at).set(out.subarray(0, at));
      dv().setUint32(used, at, true);
    }),
    fd_renumber: call((from, to) => {
      const f = fds.get(from);
      if (!f || f.preopen || !fds.has(to) || fds.get(to).preopen) return E.BADF;
      fds.set(to, f);
      fds.delete(from);
    }),
    path_open: call((dirfd, dirflags, p, n, oflags, rightsBase, rightsInheriting, fdflags, out) => {
      const path = pathAt(dirfd, p, n);
      if (!path) return E.BADF;
      let e = fsys.entry(path);
      if (oflags & O_CREAT) {
        if (e && oflags & O_EXCL) return E.EXIST;
        if (!e) e = fsys.create(path);
      }
      if (!e) return E.NOENT;
      if (oflags & O_DIRECTORY && !e.dir) return E.NOTDIR;
      const write = (rightsBase & RIGHT_FD_WRITE) !== 0n;
      const read = (rightsBase & RIGHT_FD_READ) !== 0n || e.dir;
      if (e.dir && (oflags & O_TRUNC || (rightsBase & RIGHT_FD_WRITE) !== 0n)) return E.ISDIR;
      if (oflags & O_TRUNC && !e.dir && e.size) fsys.resize(path, e, 0);
      const fd = nextFD++;
      fds.set(fd, { path, e, pos: 0, append: !!(fdflags & FD_APPEND), read, write, dir: !!e.dir });
      dv().setUint32(out, fd, true);
    }),
    path_filestat_get: call((dirfd, flags, p, n, out) => {
      const path = pathAt(dirfd, p, n);
      if (!path) return E.BADF;
      const e = fsys.entry(path);
      if (!e) return E.NOENT;
      filestat(out, e);
    }),
    path_filestat_set_times: call((dirfd, flags, p, n, atim, mtim, fst) => {
      const path = pathAt(dirfd, p, n);
      if (!path) return E.BADF;
      const e = fsys.entry(path);
      if (!e) return E.NOENT;
      times(path, e, mtim, fst);
    }),
    path_create_directory: call((dirfd, p, n) => {
      const path = pathAt(dirfd, p, n);
      if (!path) return E.BADF;
      fsys.mkdir(path);
    }),
    path_remove_directory: call((dirfd, p, n) => {
      const path = pathAt(dirfd, p, n);
      if (!path) return E.BADF;
      fsys.rmdir(path);
    }),
    path_unlink_file: call((dirfd, p, n) => {
      const path = pathAt(dirfd, p, n);
      if (!path) return E.BADF;
      fsys.unlink(path);
    }),
    path_rename: call((fd, p, n, tofd, tp, tn) => {
      const from = pathAt(fd, p, n), to = pathAt(tofd, tp, tn);
      if (!from || !to) return E.BADF;
      fsys.rename(from, to);
    }),
    path_link: () => E.NOTSUP,
    path_symlink: () => E.NOTSUP,
    path_readlink: () => E.INVAL,
    environ_sizes_get(count, size) { const m = dv(); m.setUint32(count, 0, true); m.setUint32(size, 0, true); return 0; },
    environ_get() { return 0; },
    args_sizes_get(count, size) { const m = dv(); m.setUint32(count, 0, true); m.setUint32(size, 0, true); return 0; },
    args_get() { return 0; },
    clock_res_get(id, out) { dv().setBigUint64(out, 1000n, true); return 0; },
    clock_time_get(id, precision, out) {
      const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
      dv().setBigUint64(out, ns, true);
      return 0;
    },
    random_get(p, n) {
      // getRandomValues takes at most 64 KiB at a time.
      for (let at = 0; at < n; at += 65536) crypto.getRandomValues(bytes(p + at, Math.min(65536, n - at)));
      return 0;
    },
    proc_exit(code) { throw new Error("asuka: the core exited (" + code + ")"); },
    proc_raise() { return E.NOSYS; },
    sched_yield() { return 0; },
    poll_oneoff() { return E.NOSYS; },
    sock_accept() { return E.NOSYS; },
    sock_recv() { return E.NOSYS; },
    sock_send() { return E.NOSYS; },
    sock_shutdown() { return E.NOSYS; },
  };
}
