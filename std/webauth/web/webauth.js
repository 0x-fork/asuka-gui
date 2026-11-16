// asuka/webauth on the web: a popup, done when it comes back to a page of
// the app's own origin (a URL of a custom scheme can't open a page), or
// of the callback scheme; closing it cancels. A page can't ask the
// browser for an ephemeral session.
import { WebauthError } from "./webauth.asuka.js";

let opened = 0;

export function makeSheet() {
  return {
    open(signal, url, scheme, _ephemeral) {
      return new Promise((resolve, reject) => {
        const w = window.open(url, "asuka-webauth-" + ++opened, "popup,width=500,height=700");
        if (!w) return reject(new WebauthError("canceled"));
        const end = (u, err) => {
          clearInterval(t);
          signal.removeEventListener("abort", onAbort);
          try { w.close(); } catch {}
          err ? reject(err) : resolve(u);
        };
        const onAbort = () => end("", signal.reason ?? new DOMException("aborted", "AbortError"));
        signal.addEventListener("abort", onAbort, { once: true });
        const t = setInterval(() => {
          if (w.closed) return end("", new WebauthError("canceled"));
          let href = "";
          try { href = w.location.href; } catch { return; } // another site's
          if (!href || href === "about:blank") return;
          if (scheme && !href.startsWith(scheme + ":") && new URL(href).origin !== location.origin) return;
          end(href, null);
        }, 200);
      });
    },
  };
}
