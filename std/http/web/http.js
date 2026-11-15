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
  };
}
