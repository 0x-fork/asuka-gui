// asuka/imageedit on the web: decoding with createImageBitmap (turned
// upright as the image's EXIF says), the actions on a canvas, JPEG or PNG
// written to the app's files (/tmp/asuka-images); videoFrame seeks a
// <video> to the time.
import { Op, Format, EditError } from "./imageedit.asuka.js";

const native = () => globalThis.asuka.native;
const noImage = () => new EditError("noImage");
const ops = { [Op.resize]: "resize", [Op.crop]: "crop", [Op.rotate]: "rotate", [Op.flipHorizontal]: "flipH", [Op.flipVertical]: "flipV", [Op.fill]: "fill", [Op.pixelate]: "pixelate" };

function canvas(w, h) {
  if (typeof OffscreenCanvas !== "undefined") return new OffscreenCanvas(w, h);
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  return c;
}

const uuid = () => globalThis.crypto?.randomUUID?.() ?? Math.random().toString(36).slice(2) + Date.now().toString(36);

/** writeImage writes a canvas to the app's files: PNG, or JPEG on white. Its path. */
async function writeImage(src, w, h, png, quality) {
  let c = src;
  if (!png) {
    c = canvas(w, h);
    const g = c.getContext("2d");
    g.fillStyle = "#fff";
    g.fillRect(0, 0, w, h);
    g.drawImage(src, 0, 0, w, h);
  }
  const type = png ? "image/png" : "image/jpeg";
  const blob = c.convertToBlob
    ? await c.convertToBlob({ type, quality })
    : await new Promise((resolve) => c.toBlob(resolve, type, quality));
  if (!blob) throw new Error("can't write the image");
  const path = `/tmp/asuka-images/${uuid()}.${png ? "png" : "jpg"}`;
  native().fs.writeFile(path, new Uint8Array(await blob.arrayBuffer()));
  return path;
}

async function decodeImage(src) {
  let blob;
  if (src.startsWith("file://")) src = decodeURIComponent(src.slice(7));
  if (src.startsWith("/")) {
    const b = native().fs.readFile(src);
    if (!b) throw noImage();
    blob = new Blob([b]);
  } else {
    const r = await fetch(native().assetURL(src)).catch(() => null);
    if (!r?.ok) throw noImage();
    blob = await r.blob();
  }
  try {
    return await createImageBitmap(blob, { imageOrientation: "from-image" });
  } catch {
    throw noImage();
  }
}

/** target is a resize's size: a 0 keeps the aspect ratio from the other. */
function target(a, w, h) {
  let tw = a.width || 0, th = a.height || 0;
  if (tw <= 0 && th <= 0) return [w, h];
  if (tw <= 0) tw = Math.max(1, Math.round(w * th / h));
  if (th <= 0) th = Math.max(1, Math.round(h * tw / w));
  return [tw, th];
}

/** rotatedSize is the size of a w×h image turned by degrees: whole pixels for quarter turns, enough to hold it otherwise. */
function rotatedSize(w, h, degrees) {
  const t = degrees * Math.PI / 180;
  const nw = w * Math.abs(Math.cos(t)) + h * Math.abs(Math.sin(t)), nh = w * Math.abs(Math.sin(t)) + h * Math.abs(Math.cos(t));
  return degrees % 90 === 0 ? [Math.round(nw), Math.round(nh)] : [Math.ceil(nw), Math.ceil(nh)];
}

/** pixelate averages squares of block pixels, aligned to the image, within x0..x1, y0..y1 of RGBA data w wide. */
function pixelate(data, w, h, x0, y0, x1, y1, block) {
  for (let by = Math.floor(y0 / block) * block; by < y1; by += block) {
    const by1 = Math.min(by + block, h);
    for (let bx = Math.floor(x0 / block) * block; bx < x1; bx += block) {
      const bx1 = Math.min(bx + block, w);
      const sum = [0, 0, 0, 0];
      for (let y = by; y < by1; y++) for (let x = bx; x < bx1; x++) {
        const i = (y * w + x) * 4;
        for (let k = 0; k < 4; k++) sum[k] += data[i + k];
      }
      const n = (by1 - by) * (bx1 - bx);
      const avg = sum.map((s) => Math.floor(s / n));
      for (let y = Math.max(by, y0); y < Math.min(by1, y1); y++) for (let x = Math.max(bx, x0); x < Math.min(bx1, x1); x++) {
        const i = (y * w + x) * 4;
        for (let k = 0; k < 4; k++) data[i + k] = avg[k];
      }
    }
  }
}

const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);

/** applyActions does asuka/imageedit's actions on an image (anything drawImage takes), returning a canvas. */
function applyActions(src, w, h, actions) {
  let c = canvas(w, h);
  c.getContext("2d").drawImage(src, 0, 0, w, h);
  const replace = (nw, nh, draw) => {
    const n = canvas(nw, nh);
    const g = n.getContext("2d");
    g.imageSmoothingQuality = "high";
    draw(g);
    c = n;
  };
  for (const a of actions) {
    const W = c.width, H = c.height;
    switch (a.op) {
      case "resize": {
        const [tw, th] = target(a, W, H);
        if (tw !== W || th !== H) { const old = c; replace(tw, th, (g) => g.drawImage(old, 0, 0, tw, th)); }
        break;
      }
      case "crop": {
        const x = clamp(a.x || 0, 0, W), y = clamp(a.y || 0, 0, H);
        const cw = Math.min(a.width || 0, W - x), ch = Math.min(a.height || 0, H - y);
        if (cw < 1 || ch < 1) throw new Error(`the crop is outside the ${W}×${H} image`);
        const old = c;
        replace(cw, ch, (g) => g.drawImage(old, x, y, cw, ch, 0, 0, cw, ch));
        break;
      }
      case "rotate": {
        const d = (a.degrees || 0) % 360;
        if (d === 0) break;
        const [ow, oh] = rotatedSize(W, H, d);
        const old = c;
        replace(ow, oh, (g) => {
          g.translate(ow / 2, oh / 2);
          g.rotate(d * Math.PI / 180); // clockwise: the canvas's y is down
          g.drawImage(old, -W / 2, -H / 2);
        });
        break;
      }
      case "flipH":
      case "flipV": {
        const old = c;
        replace(W, H, (g) => {
          if (a.op === "flipH") { g.translate(W, 0); g.scale(-1, 1); } else { g.translate(0, H); g.scale(1, -1); }
          g.drawImage(old, 0, 0);
        });
        break;
      }
      case "fill":
      case "pixelate": {
        const x = clamp(a.x || 0, 0, W), y = clamp(a.y || 0, 0, H);
        const x1 = clamp((a.x || 0) + (a.width || 0), 0, W), y1 = clamp((a.y || 0) + (a.height || 0), 0, H);
        if (x1 <= x || y1 <= y) break;
        const g = c.getContext("2d");
        if (a.op === "fill") {
          g.fillStyle = "#" + ((a.rgb || 0) & 0xffffff).toString(16).padStart(6, "0");
          g.fillRect(x, y, x1 - x, y1 - y);
        } else {
          const block = Math.max(a.block || 1, 1);
          // The squares' whole area, so the averages see all they cover.
          const bx = Math.floor(x / block) * block, by = Math.floor(y / block) * block;
          const ex = Math.min(Math.ceil(x1 / block) * block, W), ey = Math.min(Math.ceil(y1 / block) * block, H);
          const img = g.getImageData(bx, by, ex - bx, ey - by);
          pixelate(img.data, ex - bx, ey - by, x - bx, y - by, x1 - bx, y1 - by, block);
          g.putImageData(img, bx, by);
        }
        break;
      }
      default:
        throw new Error(`unknown action ${a.op}`);
    }
  }
  return c;
}

function videoFrame(src, atMs) {
  return new Promise((resolve, reject) => {
    const url = native().assetURL(src);
    if (!url) return reject(noImage());
    const v = document.createElement("video");
    v.muted = true;
    v.preload = "auto";
    v.crossOrigin = "anonymous";
    v.onerror = () => reject(noImage());
    v.onloadedmetadata = () => { v.currentTime = Math.min(Math.max(atMs, 0) / 1000, v.duration || 0); };
    v.onseeked = () => resolve(v);
    v.src = url;
  });
}

/** The package's steps as the actions applyActions takes. */
const actionsOf = (steps) => steps.map((s) => ({ op: ops[s.op], x: s.x, y: s.y, width: s.width, height: s.height, degrees: s.degrees, block: s.block, rgb: s.rgb }));

async function run(src, w, h, steps, format, quality, frame) {
  if (!w || !h) throw noImage();
  const c = applyActions(src, w, h, actionsOf(steps));
  src.close?.();
  if (frame) src.removeAttribute("src");
  const png = format === Format.png;
  const path = await writeImage(c, c.width, c.height, png, quality);
  return { path, width: c.width, height: c.height };
}

export function makeEditor() {
  return {
    async edit(signal, source, steps, format, quality) {
      const img = await decodeImage(source);
      return run(img, img.width, img.height, steps, format, quality, false);
    },
    async size(signal, source) {
      const img = await decodeImage(source);
      const out = { path: "", width: img.width, height: img.height };
      img.close?.();
      return out;
    },
    async videoFrame(signal, source, atMs, steps, format, quality) {
      const v = await videoFrame(source, atMs);
      return run(v, v.videoWidth, v.videoHeight, steps, format, quality, true);
    },
  };
}
