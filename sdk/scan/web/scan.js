// platform/scan on the web: the browser's BarcodeDetector (Chrome on Android
// and macOS), on the image decoded upright.
const native = () => globalThis.asuka.native;

// platform/scan's kinds and the Shape Detection API's formats.
const formats = {
  qr: "qr_code", aztec: "aztec", datamatrix: "data_matrix", pdf417: "pdf417", ean13: "ean_13", ean8: "ean_8",
  upca: "upc_a", upce: "upc_e", code128: "code_128", code39: "code_39", code93: "code_93", codabar: "codabar", itf: "itf",
};
const kinds = Object.fromEntries(Object.entries(formats).map(([k, f]) => [f, k]));

/** decodeImage reads an image source (a file of the app's, a URL, an asset), upright. */
export async function decodeImage(src) {
  let blob;
  if (src.startsWith("file://")) src = decodeURIComponent(src.slice(7));
  if (src.startsWith("/")) {
    const b = native().fs.readFile(src);
    if (!b) throw new Error("no image");
    blob = new Blob([b]);
  } else {
    const r = await fetch(native().assetURL(src)).catch(() => null);
    if (!r?.ok) throw new Error("no image");
    blob = await r.blob();
  }
  try {
    return await createImageBitmap(blob, { imageOrientation: "from-image" });
  } catch {
    throw new Error("no image");
  }
}

export function makeReader() {
  if (typeof BarcodeDetector === "undefined") throw new (native().UnsupportedError)("this browser has no BarcodeDetector");
  return {
    async file(signal, path, want) {
      const img = await decodeImage(path);
      const supported = await BarcodeDetector.getSupportedFormats?.() ?? [];
      const fs = (want.length ? want : Object.keys(formats)).map((k) => formats[k]).filter((f) => f && (!supported.length || supported.includes(f)));
      const found = await new BarcodeDetector(fs.length ? { formats: fs } : undefined).detect(img);
      img.close?.();
      const out = [];
      for (const c of found) {
        const code = { kind: kinds[c.format] || c.format, value: c.rawValue };
        if (!out.some((x) => x.kind === code.kind && x.value === code.value)) out.push(code);
      }
      return out;
    },
  };
}
