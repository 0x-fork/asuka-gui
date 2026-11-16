// asuka/vision on the web: the browser's Shape Detection API, where it
// has one: BarcodeDetector (Chrome on Android and macOS); FaceDetector and
// TextDetector only where the browser turns them on (no words).
const native = () => globalThis.asuka.native;

const formats = {
  qr_code: "qr", aztec: "aztec", data_matrix: "datamatrix", pdf417: "pdf417", ean_13: "ean13", ean_8: "ean8",
  upc_a: "upca", upc_e: "upce", code_128: "code128", code_39: "code39", code_93: "code93", codabar: "codabar", itf: "itf",
};

async function decodeImage(src) {
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

export function makeAnalyzer() {
  return {
    async analyze(signal, path, text, faces, codes, languages) {
      const want = {
        codes: codes && typeof BarcodeDetector !== "undefined",
        faces: faces && typeof FaceDetector !== "undefined",
        text: text && typeof TextDetector !== "undefined",
      };
      if (!want.codes && !want.faces && !want.text) {
        throw new (native().UnsupportedError)("this browser has no Shape Detection API (BarcodeDetector, FaceDetector, TextDetector)");
      }
      const img = await decodeImage(path);
      const W = img.width, H = img.height;
      const box = (r) => ({ x: r.x / W, y: r.y / H, w: r.width / W, h: r.height / H });
      const out = { width: W, height: H, lines: [], faces: [], codes: [] };
      if (want.codes) out.codes = (await new BarcodeDetector().detect(img)).map((c) => ({ kind: formats[c.format] || c.format, value: c.rawValue, box: box(c.boundingBox) }));
      if (want.faces) out.faces = (await new FaceDetector({ fastMode: false }).detect(img)).map((f) => box(f.boundingBox));
      if (want.text) out.lines = (await new TextDetector().detect(img)).map((t) => ({ text: t.rawValue, box: box(t.boundingBox), words: [] }));
      img.close?.();
      return out;
    },
  };
}
