// Files the user hands the page and the page hands back: the browser's
// picker (the picked files copied into the app's caches, /tmp, as iOS and
// Android give them), its downloads, and MIME types from names. The host's
// share and compose modules use them, as packages' JavaScript does
// (native.js's files).

import * as fs from "./fs.js";

/** pickFiles asks for files with the browser's picker (limit 0: any number); their copies in the app's caches. */
export function pickFiles(types, limit) {
  return new Promise((resolve) => {
    const i = document.createElement("input");
    i.type = "file";
    if (types.length && !types.some((t) => t === "*/*" || t === "")) i.accept = types.join(",");
    i.multiple = limit !== 1;
    i.style.display = "none";
    let done = false;
    const finish = async (list) => {
      if (done) return;
      done = true;
      i.remove();
      const out = [];
      for (const f of limit > 0 ? list.slice(0, limit) : list) {
        out.push({ path: await fs.saveUpload(f), name: f.name, type: f.type || typeOf(f.name), size: f.size });
      }
      resolve(out);
    };
    i.addEventListener("change", () => finish([...i.files]));
    i.addEventListener("cancel", () => finish([]));
    document.body.appendChild(i);
    i.click();
  });
}

/** download saves a file with the browser's download. */
export function download(file) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(file);
  a.download = file.name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 60000);
}

const types = {
  png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp", avif: "image/avif",
  bmp: "image/bmp", svg: "image/svg+xml", heic: "image/heic", heif: "image/heif", ico: "image/x-icon", tif: "image/tiff", tiff: "image/tiff",
  mp4: "video/mp4", m4v: "video/mp4", mov: "video/quicktime", webm: "video/webm", ogv: "video/ogg",
  mp3: "audio/mpeg", m4a: "audio/mp4", aac: "audio/aac", wav: "audio/wav", flac: "audio/flac", ogg: "audio/ogg", oga: "audio/ogg", opus: "audio/ogg",
  pdf: "application/pdf", txt: "text/plain", md: "text/markdown", csv: "text/csv", html: "text/html", htm: "text/html",
  json: "application/json", xml: "application/xml", js: "text/javascript", css: "text/css", zip: "application/zip",
  doc: "application/msword", docx: "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
  xls: "application/vnd.ms-excel", xlsx: "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
  ppt: "application/vnd.ms-powerpoint", pptx: "application/vnd.openxmlformats-officedocument.presentationml.presentation",
  ics: "text/calendar", vcf: "text/vcard", epub: "application/epub+zip",
};

/** typeOf is a file's MIME type from its name's extension ("" if not known). */
export function typeOf(name) {
  const base = String(name).split(/[?#]/)[0];
  const dot = base.lastIndexOf(".");
  return dot < 0 ? "" : types[base.slice(dot + 1).toLowerCase()] || "";
}
