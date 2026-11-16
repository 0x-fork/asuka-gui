// std/http on the web: fetch, aborted when the call is cancelled or its
// timeout passes (a timeout throws TimeoutError: the call wasn't
// cancelled, it failed).
export function makeTransport() {
  return {
    async send(signal, method, url, headers, body, timeout) {
      const ctl = new AbortController();
      const g = guard(ctl, signal, timeout);
      try {
        const r = await fetch(url, request(method, headers, body, ctl));
        return { ...head(r, url), body: new Uint8Array(await r.arrayBuffer()) };
      } catch (e) {
        throw g.error(e);
      } finally {
        g.done();
      }
    },

    // download reads the body's stream as it comes, reporting progress (a
    // few times a second), then writes it into the app's files. timeout
    // is the longest wait for more.
    async download(signal, method, url, headers, body, timeout, path, progress) {
      const ctl = new AbortController();
      const g = guard(ctl, signal, timeout);
      try {
        const r = await fetch(url, request(method, headers, body, ctl));
        const length = Number(r.headers.get("content-length") ?? -1);
        // A compressed answer's length is its compressed size: unknown.
        const total = r.headers.get("content-encoding") ? -1 : length;
        const chunks = [];
        let got = 0, reported = 0;
        if (r.body) {
          const reader = r.body.getReader();
          for (;;) {
            g.restart();
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(value);
            got += value.length;
            const now = performance.now();
            if (now - reported > 100) {
              reported = now;
              progress(got, total);
            }
          }
        }
        const all = new Uint8Array(got);
        let at = 0;
        for (const c of chunks) {
          all.set(c, at);
          at += c.length;
        }
        globalThis.asuka.native.fs.writeFile(path, all);
        progress(got, got);
        return { ...head(r, url), body: new Uint8Array(0) };
      } catch (e) {
        throw g.error(e);
      } finally {
        g.done();
      }
    },

    // open answers once the response's head is in (fetch resolves then),
    // its body read by the Body as it comes. The request is the Body's
    // from then on: its close, or a read's cancel, aborts it.
    async open(signal, method, url, headers, body, timeout) {
      const ctl = new AbortController();
      const g = guard(ctl, signal, timeout);
      let r;
      try {
        r = await fetch(url, request(method, headers, body, ctl));
      } catch (e) {
        throw g.error(e);
      } finally {
        g.done();
      }
      return bodyOf(r, url, ctl, timeout);
    },
  };
}

// A Body comes from Transport.open only.
export function makeBody() {
  throw new globalThis.asuka.native.UnsupportedError("a body comes from Transport.open");
}

// bodyOf reads r's body a piece at a time: each read waits for the next
// one the network delivers, at most timeout (0: no limit).
function bodyOf(r, url, ctl, timeout) {
  const h = { ...head(r, url), body: new Uint8Array(0) };
  const reader = r.body ? r.body.getReader() : null;
  let ended = !reader, closed = false;
  return {
    head: () => h,

    async read(signal) {
      if (closed) throw new Error("the body is closed");
      if (ended) return new Uint8Array(0);
      const g = guard(ctl, signal, timeout);
      try {
        for (;;) {
          const { done, value } = await reader.read();
          if (done) {
            ended = true;
            return new Uint8Array(0);
          }
          if (value.length) return value;
        }
      } catch (e) {
        throw g.error(e);
      } finally {
        g.done();
      }
    },

    close() {
      closed = true;
      ctl.abort();
    },

    // Let go, it stops the request.
    released() {
      this.close();
    },
  };
}

function request(method, headers, body, ctl) {
  return {
    method,
    headers,
    body: body.length && method !== "GET" && method !== "HEAD" ? body : undefined,
    signal: ctl.signal,
  };
}

// head is a response's status, URL and headers (by lower-case name).
function head(r, url) {
  const out = {};
  r.headers.forEach((v, k) => { out[k.toLowerCase()] = v; });
  return { status: r.status, url: r.url || url, headers: out };
}

// guard aborts ctl when the call's signal does, or when timeout passes
// (0: never) from its start or its last restart; error is what a failure
// was: TimeoutError when the timeout aborted it. done lets the call go.
function guard(ctl, signal, timeout) {
  let timer = 0, timedOut = false;
  const abort = () => ctl.abort(signal.reason);
  if (signal.aborted) abort();
  else signal.addEventListener("abort", abort);
  const g = {
    restart() {
      clearTimeout(timer);
      if (timeout > 0) {
        timer = setTimeout(() => {
          timedOut = true;
          ctl.abort(timedOutError(timeout));
        }, timeout);
      }
    },
    error: (e) => (timedOut ? timedOutError(timeout) : e),
    done() {
      clearTimeout(timer);
      signal.removeEventListener("abort", abort);
    },
  };
  g.restart();
  return g;
}

function timedOutError(timeout) {
  return new DOMException(`no answer within ${timeout} ms`, "TimeoutError");
}

// A page can't listen: there's no server of the app's on the web (sign in
// with a redirect to the app's own site instead).
export function makeLocalServer() {
  throw new globalThis.asuka.native.UnsupportedError("a browser can't listen on the loopback: redirect to the app's site instead");
}
