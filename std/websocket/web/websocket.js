// std/websocket on the web: the browser's WebSocket. It sends no headers
// of the page's (the package's headers are ignored here), and a client
// may close with 1000 or 3000 to 4999 only.
import { FrameKind } from "./websocket.asuka.js";

export function makeClient() {
  if (typeof WebSocket === "undefined") {
    throw new globalThis.asuka.native.UnsupportedError("no WebSocket in this browser");
  }
  return {
    // connect answers on the socket's open; an error or a close before
    // it, the timeout, or a cancel fails it (and drops the connection).
    connect(signal, url, headers, protocols, timeout) {
      return new Promise((resolve, reject) => {
        const ws = new WebSocket(url, protocols);
        ws.binaryType = "arraybuffer";
        // Listening from now, so nothing the server sends right after
        // the open is missed.
        const s = socketOf(ws);
        let timer = 0;
        const settle = (e) => {
          clearTimeout(timer);
          signal.removeEventListener("abort", abort);
          ws.removeEventListener("open", opened);
          ws.removeEventListener("error", failed);
          ws.removeEventListener("close", failed);
          if (!e) return resolve(s);
          if (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN) ws.close();
          reject(e);
        };
        const opened = () => settle(null);
        const failed = () => settle(new Error(`can't connect to ${url}`));
        const abort = () => settle(signal.reason ?? new DOMException("canceled", "AbortError"));
        ws.addEventListener("open", opened);
        ws.addEventListener("error", failed);
        ws.addEventListener("close", failed);
        if (signal.aborted) return abort();
        signal.addEventListener("abort", abort);
        if (timeout > 0) {
          timer = setTimeout(() => settle(new DOMException(`no answer within ${timeout} ms`, "TimeoutError")), timeout);
        }
      });
    },
  };
}

// A Socket comes from Client.connect only.
export function makeSocket() {
  throw new globalThis.asuka.native.UnsupportedError("a socket comes from Client.connect");
}

const empty = new Uint8Array(0);

// socketOf queues what ws receives for receive, in order; once it closes,
// a closed frame (its code: 1005 when it gave none, 1006 when it ended
// without a close) comes after them, and always.
function socketOf(ws) {
  const frames = [];
  const waiting = []; // resolve functions of the receives waiting
  let closed = null;
  const put = (f) => {
    const w = waiting.shift();
    if (w) w(f);
    else frames.push(f);
  };
  ws.addEventListener("message", (e) => {
    if (typeof e.data === "string") put({ kind: FrameKind.text, text: e.data, data: empty, code: 0 });
    else put({ kind: FrameKind.binary, text: "", data: new Uint8Array(e.data), code: 0 });
  });
  ws.addEventListener("close", (e) => {
    closed = { kind: FrameKind.closed, text: e.reason, data: empty, code: e.code || 1005 };
    while (waiting.length) waiting.shift()(closed);
  });

  const open = () => {
    if (ws.readyState !== WebSocket.OPEN) throw new Error("the connection is closed");
  };

  return {
    protocol: () => ws.protocol,

    receive(signal) {
      if (frames.length) return Promise.resolve(frames.shift());
      if (closed) return Promise.resolve(closed);
      return new Promise((resolve, reject) => {
        const got = (f) => {
          signal.removeEventListener("abort", abort);
          resolve(f);
        };
        const abort = () => {
          const i = waiting.indexOf(got);
          if (i >= 0) waiting.splice(i, 1);
          reject(signal.reason ?? new DOMException("canceled", "AbortError"));
        };
        if (signal.aborted) return abort();
        signal.addEventListener("abort", abort);
        waiting.push(got);
      });
    },

    // The browser buffers what's sent: it's on its way when send returns.
    async sendText(signal, text) {
      open();
      ws.send(text);
    },

    async sendBinary(signal, data) {
      open();
      ws.send(data);
    },

    // shutdown sends a close with code and reason. A browser refuses a
    // code other than 1000 or 3000 to 4999 from a page (1001's going
    // away included): those close normally (1000). A reason past 123
    // bytes of UTF-8 is cut at a character's end.
    shutdown(code, reason) {
      if (ws.readyState === WebSocket.CLOSING || ws.readyState === WebSocket.CLOSED) return;
      ws.close(code === 1000 || (code >= 3000 && code <= 4999) ? code : 1000, fit(reason, 123));
    },

    close() {
      if (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN) ws.close(1000);
    },

    // Let go, it closes.
    released() {
      this.close();
    },
  };
}

// fit is s cut to at most n bytes of UTF-8, at a character's end.
function fit(s, n) {
  const enc = new TextEncoder();
  if (enc.encode(s).length <= n) return s;
  let out = "";
  let size = 0;
  for (const c of s) {
    size += enc.encode(c).length;
    if (size > n) break;
    out += c;
  }
  return out;
}
