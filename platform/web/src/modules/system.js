// The system's modules on the web that the standard library's packages
// still reach by name (asuka/host), and the share sheet the core's
// ui.ShareFiles opens; the rest are the packages' own JavaScript
// (std/<name>/web):
//
//   - share: the system's share sheet with the files (Web Share), or, where
//     the browser can't share files, a download of each.
//   - compose (asuka/compose): mailto: and sms: links; files go through the
//     share sheet.
//   - notify (asuka/notify): the Notification API (system-notify.js),
//     through the app's service worker (sw.js).
//   - push (asuka/push): Web Push (system-push.js).
//
// A method a browser can't do answers with an error that says why.

import { Reader } from "../ops.js";
import * as fs from "../fs.js";
import { download, typeOf } from "../files.js";
import { notifyFor } from "./system-notify.js";
import { pushModule } from "./system-push.js";

const dec = new TextDecoder();

const notOnWeb = (module, method, why) => new Error(`asuka: ${module}.${method} isn't on the web: ${why}`);

export function modules(h) {
  return {
    share: { files: (b) => shareFiles(b) },
    compose: {
      can: () => composeCan(),
      mail: (b) => composeMail(h, JSON.parse(dec.decode(b))),
      message: (b) => composeMessage(h, JSON.parse(dec.decode(b))),
    },
    notify: notifyFor(h),
    push: pushModule(h),
  };
}

// --- sharing ---

/** decodeShare decodes share.files' args: u32 view, string MIME, UTI, title, u32 count, the paths. */
export function decodeShare(b) {
  const r = new Reader(b);
  const view = r.u32();
  const mime = r.string(), uti = r.string(), title = r.string();
  const n = r.u32();
  const paths = [];
  for (let i = 0; i < n; i++) paths.push(r.string());
  return { view, mime, uti, title, paths };
}

async function shareFiles(b) {
  const a = decodeShare(b);
  const files = a.paths.map((p, i) => {
    const f = fs.fileOf(p, undefined, typeOf(p) || (i === 0 && a.mime) || "application/octet-stream");
    if (!f) throw new Error("no file at " + p);
    return f;
  });
  const data = a.title ? { files, title: a.title } : { files };
  if (navigator.canShare?.(data)) {
    try {
      await navigator.share(data);
    } catch (e) {
      if (e?.name === "AbortError") return; // closed without sharing: done
      if (e?.name !== "NotAllowedError") throw e;
      for (const f of files) download(f); // no gesture left to show the sheet with
    }
    return;
  }
  // No share sheet for files (a desktop browser): saved, as Downloads.
  for (const f of files) download(f);
}

// --- files from sources (compose) ---

/** sourceURL is where a source (a URL, a file of the app's, an asset) loads from. */
function sourceURL(h, src) {
  if (/^(https?|blob|data):/.test(src)) return src;
  if (src.startsWith("file://")) src = decodeURIComponent(src.slice(7));
  if (src.startsWith("/")) return null;
  return new URL(h.assets + "/" + src.replace(/^\.?\//, ""), location.href).href;
}

/** sourceBlob is a source's bytes, as a Blob of type. */
async function sourceBlob(h, src, type) {
  const url = sourceURL(h, src);
  if (url == null) {
    const path = src.startsWith("file://") ? decodeURIComponent(src.slice(7)) : src;
    const b = fs.readFile(path);
    if (!b) throw new Error("no file at " + path);
    return new Blob([b.slice()], { type });
  }
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status} ${r.statusText}`);
  const blob = await r.blob();
  return type && blob.type !== type ? new Blob([blob], { type }) : blob;
}

const phone = () => /iPhone|iPod|iPad|Android/.test(navigator.userAgent) || (/Macintosh/.test(navigator.userAgent) && navigator.maxTouchPoints > 1);
const apple = () => /iPhone|iPod|iPad|Macintosh/.test(navigator.userAgent);

/** composeCan: mail (a mailto: link: the user's mail app), no composer of the page's own, messages (an sms: link) where there's a messages app. */
export function composeCan() {
  return new Uint8Array([1, 0, phone() || /Macintosh/.test(navigator.userAgent) ? 1 : 0, 0]);
}

function openLink(url) {
  const a = document.createElement("a");
  a.href = url;
  a.rel = "noopener";
  a.click();
}

async function shareWith(h, as, data) {
  const files = [];
  for (const at of as) {
    const blob = await sourceBlob(h, at.source, at.type);
    files.push(new File([blob], at.name, { type: at.type }));
  }
  const d = { ...data, files };
  if (!navigator.canShare?.(d)) return false;
  try {
    await navigator.share(d);
    return new Uint8Array([3]);
  } catch (e) {
    if (e?.name === "AbortError") return new Uint8Array([0]);
    throw e;
  }
}

async function composeMail(h, a) {
  if (a.attachments?.length) {
    // A mailto: link carries no files: the share sheet does, to a mail app.
    const r = await shareWith(h, a.attachments, { title: a.subject || undefined, text: a.text || undefined });
    if (r) return r;
    throw notOnWeb("compose", "Mail", "a mailto: link carries no attachments, and this browser can't share files");
  }
  openLink(a.mailto);
  return new Uint8Array([3]); // unknown: the mail app has it
}

/** smsURL is an sms: link to the recipients with the body (iOS and the Mac take it after &, the rest after ?). */
export function smsURL(to, body, isApple) {
  let u = "sms:" + to.map((t) => encodeURIComponent(t).replace(/%2B/g, "+")).join(",");
  if (body) u += (isApple ? "&" : "?") + "body=" + encodeURIComponent(body);
  return u;
}

async function composeMessage(h, a) {
  const body = [a.subject, a.body].filter(Boolean).join("\n");
  if (a.attachments?.length) {
    const r = await shareWith(h, a.attachments, { text: body || undefined });
    if (r) return r;
    throw notOnWeb("compose", "Message", "an sms: link carries no attachments, and this browser can't share files");
  }
  if (!composeCan()[2]) throw new Error("unavailable");
  openLink(smsURL(a.to || [], body, apple()));
  return new Uint8Array([3]);
}
