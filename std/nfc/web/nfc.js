// asuka/nfc on the web: Web NFC's NDEFReader, which Chrome on Android
// has (and no other browser: notAvailable). It reads and writes NDEF
// messages only: no tag IDs but its serial number, no raw commands, no
// capacity. A session is a scan(); one tag at a time. Writing and making
// read-only act on the tag in range (Web NFC's write() and makeReadOnly()
// wait for one). A page has no NFC settings, and is opened by no tag.
//
// The package's messages are their bytes; Web NFC's are records of its
// own: fromWeb and toWeb turn one into the other.
import { Format, NfcError } from "./nfc.asuka.js";

const enc = new TextEncoder();
const dec = new TextDecoder();

const cat = (...parts) => {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let i = 0;
  for (const p of parts) {
    out.set(p, i);
    i += p.length;
  }
  return out;
};

/** decodeNDEF reads an NDEF message's bytes: its records ({tnf, type, id, payload}), chunks joined. */
function decodeNDEF(b) {
  const out = [];
  let p = 0;
  let chunk = null;
  const bad = () => new Error("not an NDEF message");
  while (p < b.length) {
    if (b.length - p < 3) throw bad();
    const h = b[p];
    const typeLen = b[p + 1];
    p += 2;
    let len;
    if (h & 0x10) len = b[p++];
    else {
      if (b.length - p < 4) throw bad();
      len = new DataView(b.buffer, b.byteOffset + p, 4).getUint32(0);
      p += 4;
    }
    const idLen = h & 0x08 ? b[p++] : 0;
    if (p + typeLen + idLen + len > b.length) throw bad();
    const r = { tnf: h & 7, type: b.slice(p, p + typeLen), id: b.slice(p + typeLen, p + typeLen + idLen), payload: b.slice(p + typeLen + idLen, p + typeLen + idLen + len) };
    p += typeLen + idLen + len;
    if (chunk) {
      if (r.tnf !== 6) throw bad();
      chunk.payload = cat(chunk.payload, r.payload);
      if (!(h & 0x20)) {
        out.push(chunk);
        chunk = null;
      }
    } else if (r.tnf === 6) throw bad();
    else if (h & 0x20) chunk = r;
    else out.push(r);
    if (h & 0x40) break;
  }
  if (chunk) throw bad();
  if (out.length === 1 && out[0].tnf === 0) return [];
  return out;
}

/** encodeNDEF is the records' message, as a tag holds it (an empty one: one empty record). */
function encodeNDEF(records) {
  if (records.length === 0) return new Uint8Array([0xd0, 0, 0]);
  const parts = records.map((r, i) => {
    const short = r.payload.length < 256;
    let h = r.tnf & 7;
    if (i === 0) h |= 0x80;
    if (i === records.length - 1) h |= 0x40;
    if (short) h |= 0x10;
    if (r.id.length) h |= 0x08;
    const len = short ? [r.payload.length] : [r.payload.length >>> 24, (r.payload.length >>> 16) & 0xff, (r.payload.length >>> 8) & 0xff, r.payload.length & 0xff];
    return cat(new Uint8Array([h, r.type.length, ...len, ...(r.id.length ? [r.id.length] : [])]), r.type, r.id, r.payload);
  });
  return cat(...parts);
}

// The URI record's abbreviations, by code (NFC Forum's URI RTD).
const uriPrefixes = ["", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:",
  "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://", "sftp://", "smb://", "nfs://", "ftp://", "dav://",
  "news:", "telnet://", "imap:", "rtsp://", "urn:", "pop:", "sip:", "sips:", "tftp:", "btspp://", "btl2cap://",
  "btgoep://", "tcpobex://", "irdaobex://", "file://", "urn:epc:id:", "urn:epc:tag:", "urn:epc:pat:",
  "urn:epc:raw:", "urn:epc:", "urn:nfc:"];

const bytesOf = (data) => (data ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice() : new Uint8Array(0));
const wk = (type, payload, id) => ({ tnf: 1, type: enc.encode(type), id, payload });

/** fromWeb is a Web NFC NDEFRecord as the wire's record. */
function fromWeb(r) {
  const id = enc.encode(r.id ?? "");
  const data = bytesOf(r.data);
  switch (r.recordType) {
    case "empty":
      return { tnf: 0, type: new Uint8Array(0), id: new Uint8Array(0), payload: new Uint8Array(0) };
    case "text": {
      const lang = enc.encode(r.lang || "en");
      const utf16 = (r.encoding ?? "utf-8").startsWith("utf-16");
      return wk("T", cat(new Uint8Array([(utf16 ? 0x80 : 0) | (lang.length & 0x3f)]), lang, data), id);
    }
    case "url": // data is the URL whole, its prefix spelled out
      return wk("U", cat(new Uint8Array([0]), data), id);
    case "absolute-url":
      return { tnf: 3, type: data, id, payload: new Uint8Array(0) };
    case "mime":
      return { tnf: 2, type: enc.encode(r.mediaType ?? "application/octet-stream"), id, payload: data };
    case "smart-poster": // data is its payload: the poster's message
      return wk("Sp", data, id);
    case "unknown":
      return { tnf: 5, type: new Uint8Array(0), id, payload: data };
    default:
      // ":t": a local type (a well-known type in a nested message); else
      // an external type, domain:type.
      if (r.recordType.startsWith(":")) return wk(r.recordType.slice(1), data, id);
      return { tnf: 4, type: enc.encode(r.recordType), id, payload: data };
  }
}

/**
 * toWeb is the wire's record as a Web NFC NDEFRecordInit; nested, in a
 * smart poster's message, its well-known types are local (":act").
 */
function toWeb(r, nested = false) {
  const init = {};
  if (r.id.length) init.id = dec.decode(r.id);
  const type = dec.decode(r.type);
  switch (r.tnf) {
    case 0:
      return { recordType: "empty" };
    case 1:
      if (type === "T" && r.payload.length > 0) {
        const n = r.payload[0] & 0x3f;
        const utf16 = (r.payload[0] & 0x80) !== 0;
        return { ...init, recordType: "text", lang: dec.decode(r.payload.slice(1, 1 + n)), encoding: utf16 ? "utf-16be" : "utf-8", data: r.payload.slice(1 + n) };
      }
      if (type === "U" && r.payload.length > 0) {
        return { ...init, recordType: "url", data: (uriPrefixes[r.payload[0]] ?? "") + dec.decode(r.payload.slice(1)) };
      }
      if (type === "Sp") {
        return { ...init, recordType: "smart-poster", data: { records: decodeNDEF(r.payload).map((x) => toWeb(x, true)) } };
      }
      if (!nested) throw new Error(`not supported: Web NFC writes no well-known type ${type} outside a smart poster`);
      return { ...init, recordType: ":" + type, data: r.payload };
    case 2:
      return { ...init, recordType: "mime", mediaType: type, data: r.payload };
    case 3:
      return { ...init, recordType: "absolute-url", data: type };
    case 4:
      return { ...init, recordType: type, data: r.payload };
    case 5:
      return { ...init, recordType: "unknown", data: r.payload };
  }
  throw new Error(`not supported: a record of TNF ${r.tnf}`);
}

/** serial is a reading's serial number ("04:a2:...") as bytes. */
function serial(s) {
  if (!s) return new Uint8Array(0);
  return Uint8Array.from(s.split(":").filter(Boolean), (x) => parseInt(x, 16));
}

function failure(e) {
  switch (e?.name) {
    case "NotAllowedError": return new NfcError("denied");
    case "NotSupportedError": return new NfcError("notAvailable");
    case "NotReadableError": return new NfcError("disabled");
    case "AbortError": return new NfcError("canceled");
    case "NetworkError": return new NfcError("tagLost");
    default: return e;
  }
}

const has = () => typeof globalThis.NDEFReader === "function";

let session = null; // { id, reader, ctrl, waiting, queue, held }
let lastEnded = null;
let ids = 0;
let handles = 0;

function end(s, e) {
  if (!s || session !== s) return;
  session = null;
  lastEnded = [s.id, e];
  s.held = null;
  s.ctrl.abort();
  if (s.waiting) {
    s.waiting.reject(e);
    s.waiting = null;
  }
}

export function makeReader() {
  return {
    state() {
      return { supported: has(), enabled: has() };
    },

    async watchState(signal, changed) {
      changed({ supported: has(), enabled: has() });
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
    },

    async begin(signal, o) {
      if (!has()) throw new NfcError("notAvailable");
      if (o.poll.length) throw new NfcError("notSupported");
      if (session) end(session, new NfcError("canceled"));
      const s = { id: ++ids, reader: new NDEFReader(), ctrl: new AbortController(), waiting: null, queue: [], held: null };
      session = s;
      s.reader.onreading = (e) => {
        if (session !== s || s.held) return;
        const records = Array.from(e.message?.records ?? []).map((r) => fromWeb(r));
        const handle = ++handles;
        s.held = handle;
        const f = { handle, id: serial(e.serialNumber), techs: ["ndef"], hasMessage: true, message: encodeNDEF(records),
          format: Format.readWrite, capacity: -1, canMakeReadOnly: true, type: "" };
        if (s.waiting) {
          s.waiting.resolve(f);
          s.waiting = null;
        } else s.queue.push(f);
      };
      s.reader.onreadingerror = () => {};
      try {
        await s.reader.scan({ signal: s.ctrl.signal });
      } catch (e) {
        end(s, failure(e));
        throw failure(e);
      }
      return s.id;
    },

    next(signal, id) {
      const s = session;
      if (!s || s.id !== id) {
        throw lastEnded && lastEnded[0] === id ? lastEnded[1] : new NfcError("canceled");
      }
      // The app is done with the tag it held: the next one may come.
      s.held = null;
      if (s.queue.length) return s.queue.shift();
      return new Promise((resolve, reject) => {
        s.waiting = { resolve, reject };
        signal.addEventListener("abort", () => reject(new NfcError("canceled")), { once: true });
      });
    },

    setAlert() {},

    end(id) {
      if (session && session.id === id) end(session, new NfcError("canceled"));
    },

    async read() {
      throw new NfcError("notSupported");
    },

    async write(signal, tag, message) {
      if (!session || session.held !== tag) throw new NfcError("tagLost");
      try {
        await session.reader.write({ records: decodeNDEF(message).map((r) => toWeb(r)) }, { overwrite: true });
      } catch (e) {
        throw failure(e);
      }
    },

    async makeReadOnly(signal, tag) {
      if (!session || session.held !== tag) throw new NfcError("tagLost");
      if (typeof session.reader.makeReadOnly !== "function") throw new NfcError("notSupported");
      try {
        await session.reader.makeReadOnly();
      } catch (e) {
        throw failure(e);
      }
    },

    async transceive() {
      throw new NfcError("notSupported");
    },

    async opened(signal) {
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
    },

    async openSettings() {
      throw new NfcError("notAvailable");
    },
  };
}
