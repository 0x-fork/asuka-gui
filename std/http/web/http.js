// asuka/http on the web: fetch, aborted when the call is cancelled.
export function makeTransport() {
  return {
    async send(signal, method, url, headers, body, timeout) {
      const ctl = new AbortController();
      const abort = () => ctl.abort();
      signal.addEventListener("abort", abort);
      const timer = timeout > 0 ? setTimeout(abort, timeout) : 0;
      try {
        const r = await fetch(url, {
          method,
          headers,
          body: body.length && method !== "GET" && method !== "HEAD" ? body : undefined,
          signal: ctl.signal,
        });
        const out = {};
        r.headers.forEach((v, k) => { out[k.toLowerCase()] = v; });
        return { status: BigInt(r.status), url: r.url || url, headers: out, body: new Uint8Array(await r.arrayBuffer()) };
      } finally {
        clearTimeout(timer);
        signal.removeEventListener("abort", abort);
      }
    },

    // download reads the body's stream as it comes, reporting progress (a
    // few times a second), then writes it into the app's files. timeout
    // is the longest wait for more.
    async download(signal, method, url, headers, body, timeout, path, progress) {
      const ctl = new AbortController();
      const abort = () => ctl.abort();
      signal.addEventListener("abort", abort);
      let timer = 0;
      const idle = () => {
        clearTimeout(timer);
        if (timeout > 0) timer = setTimeout(abort, timeout);
      };
      idle();
      try {
        const r = await fetch(url, {
          method,
          headers,
          body: body.length && method !== "GET" && method !== "HEAD" ? body : undefined,
          signal: ctl.signal,
        });
        const out = {};
        r.headers.forEach((v, k) => { out[k.toLowerCase()] = v; });
        const length = Number(r.headers.get("content-length") ?? -1);
        // A compressed answer's length is its compressed size: unknown.
        const total = r.headers.get("content-encoding") ? -1 : length;
        const chunks = [];
        let got = 0, reported = 0;
        if (r.body) {
          const reader = r.body.getReader();
          for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            idle();
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
        return { status: BigInt(r.status), url: r.url || url, headers: out, body: new Uint8Array(0) };
      } finally {
        clearTimeout(timer);
        signal.removeEventListener("abort", abort);
      }
    },
  };
}
