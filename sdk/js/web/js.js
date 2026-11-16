// platform/js on the web: a hidden frame, sandboxed (an origin of its own:
// it can't reach the app or its storage), made for each script and let go
// after.
import { JsError } from "./js.asuka.js";

function runScript(signal, ms, script) {
  return new Promise((resolve, reject) => {
    const token = globalThis.crypto?.randomUUID?.() ?? String(Math.random()).slice(2);
    const f = document.createElement("iframe");
    f.setAttribute("sandbox", "allow-scripts");
    f.setAttribute("aria-hidden", "true");
    f.tabIndex = -1;
    f.style.cssText = "position:fixed;left:-10000px;top:0;width:1px;height:1px;border:0;visibility:hidden";
    let timer = 0;
    const end = (ok, v) => {
      clearTimeout(timer);
      removeEventListener("message", onMessage);
      signal.removeEventListener("abort", onAbort);
      f.remove();
      ok ? resolve(v) : reject(v);
    };
    const onMessage = (e) => {
      if (e.source !== f.contentWindow || !e.data || e.data.t !== token) return;
      if (e.data.ready) f.contentWindow.postMessage({ t: token, s: script }, "*");
      else end(e.data.ok, e.data.ok ? e.data.v : new Error(e.data.v));
    };
    const onAbort = () => end(false, signal.reason ?? new DOMException("aborted", "AbortError"));
    addEventListener("message", onMessage);
    signal.addEventListener("abort", onAbort, { once: true });
    timer = setTimeout(() => end(false, new JsError("timeout")), Math.max(0, ms));
    const t = JSON.stringify(token);
    f.srcdoc = `<!doctype html><meta charset="utf-8"><script>
addEventListener("message", async function (e) {
  if (e.source !== parent || !e.data || e.data.t !== ${t}) return;
  var out;
  try {
    var v = await (0, eval)(e.data.s);
    var j = JSON.stringify(v === undefined ? null : v);
    out = { t: ${t}, ok: true, v: j === undefined ? "null" : j };
  } catch (err) {
    out = { t: ${t}, ok: false, v: String(err && err.message !== undefined ? err.message : err) };
  }
  parent.postMessage(out, "*");
});
parent.postMessage({ t: ${t}, ready: true }, "*");
<\/script>`;
    document.body.appendChild(f);
  });
}

export function makeEngine() {
  return {
    run(signal, script, timeout) { return runScript(signal, timeout, script); },
  };
}
