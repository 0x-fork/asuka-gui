// asuka/svg on the web: the icon's layers (the compiler's encoding:
// sdk/svg's Encode) as an <svg> of <path>s the browser draws, fitted to
// the view (xMidYMid meet); currentColor is the view's color, a semantic
// one a variable that follows light and dark.
const NS = "http://www.w3.org/2000/svg";

/** decode reads an icon's data: its viewBox, size and layers (each path as SVG's path data). */
function decode(b) {
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  let at = 0;
  const u8 = () => (at < b.length ? b[at++] : 0);
  const u32 = () => { const x = at + 4 <= b.length ? v.getUint32(at, true) : 0; at += 4; return x; };
  const f32 = () => { const x = at + 4 <= b.length ? v.getFloat32(at, true) : 0; at += 4; return x; };
  const out = { viewBox: [0, 0, 0, 0], width: 0, height: 0, layers: [] };
  if (b.length < 24) return out;
  out.viewBox = [f32(), f32(), f32(), f32()];
  out.width = f32();
  out.height = f32();
  const paint = () => {
    switch (u8()) {
      case 1: { const r = u8(), g = u8(), bl = u8(), a = u8(); return { color: `rgb(${r} ${g} ${bl} / ${(a / 255).toFixed(3)})` }; }
      case 2: return { current: true, opacity: f32() };
      default: return null;
    }
  };
  const n = u32();
  for (let i = 0; i < n && at < b.length; i++) {
    const fill = paint(), stroke = paint(), width = f32();
    const cap = ["butt", "round", "square"][Math.min(u8(), 2)], join = ["miter", "round", "bevel"][Math.min(u8(), 2)];
    const end = at + u32();
    const d = [];
    const nums = (k) => { const a = []; for (let j = 0; j < k; j++) a.push(+f32().toFixed(4)); return a; };
    while (at < Math.min(end, b.length)) {
      switch (u8()) {
        case 1: d.push("M", ...nums(2)); break;
        case 2: d.push("L", ...nums(2)); break;
        case 3: d.push("Q", ...nums(4)); break;
        case 4: d.push("C", ...nums(6)); break;
        case 5: d.push("Z"); break;
        case 6: { const [x, y, w, h] = nums(4); d.push("M", x, y, "h", w, "v", h, "h", -w, "Z"); break; }
        case 7: {
          const [x, y, w, h, r0] = nums(5), r = Math.min(r0, w / 2, h / 2);
          d.push("M", x + r, y, "H", x + w - r, "A", r, r, 0, 0, 1, x + w, y + r, "V", y + h - r, "A", r, r, 0, 0, 1, x + w - r, y + h,
            "H", x + r, "A", r, r, 0, 0, 1, x, y + h - r, "V", y + r, "A", r, r, 0, 0, 1, x + r, y, "Z");
          break;
        }
        case 8: {
          const [x, y, w, h] = nums(4), rx = w / 2, ry = h / 2;
          d.push("M", x + w, y + ry, "A", rx, ry, 0, 1, 1, x, y + ry, "A", rx, ry, 0, 1, 1, x + w, y + ry, "Z");
          break;
        }
        default: at = end;
      }
    }
    at = end;
    out.layers.push({ fill, stroke, width, cap, join, d: d.join(" ") });
  }
  return out;
}

const paintOf = (p) => (p ? (p.current ? "currentColor" : p.color) : "none");

export function makeView(events) {
  const el = document.createElement("div");
  el.style.cssText = "display:flex;line-height:0";
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("preserveAspectRatio", "xMidYMid meet");
  svg.style.cssText = "width:100%;height:100%;overflow:visible";
  el.appendChild(svg);
  let size = [0, 0];
  return {
    el,
    setIcon(icon) {
      const ic = decode(icon.data);
      size = [ic.width, ic.height];
      svg.setAttribute("viewBox", ic.viewBox.join(" "));
      svg.textContent = "";
      for (const l of ic.layers) {
        const p = document.createElementNS(NS, "path");
        p.setAttribute("d", l.d);
        p.setAttribute("fill", paintOf(l.fill));
        if (l.fill?.current && l.fill.opacity < 1) p.setAttribute("fill-opacity", l.fill.opacity);
        p.setAttribute("stroke", paintOf(l.stroke));
        if (l.stroke) {
          p.setAttribute("stroke-width", l.width);
          p.setAttribute("stroke-linecap", l.cap);
          p.setAttribute("stroke-linejoin", l.join);
          if (l.stroke.current && l.stroke.opacity < 1) p.setAttribute("stroke-opacity", l.stroke.opacity);
        }
        svg.appendChild(p);
      }
    },
    setColor(c) { el.style.color = globalThis.asuka.native.color(c) || "var(--asuka-label)"; },
    measure() { return size; },
  };
}
