// asuka/appearance on the web: CSS's AccentColor where the browser has
// it (Firefox, Safari: the system's accent), prefers-contrast: more and
// prefers-reduced-transparency: reduce, followed as their media queries
// change (and the color scheme, which the accent may follow).

const queries = ["(prefers-contrast: more)", "(prefers-reduced-transparency: reduce)", "(prefers-color-scheme: dark)"];
const query = (q) => (typeof matchMedia === "function" ? matchMedia(q) : null);

/** parseRGB reads a computed color ("rgb(r, g, b)", "rgba(...)", "color(srgb r g b)") as [r, g, b], or null. */
export function parseRGB(s) {
  let m = /^rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)/.exec(s ?? "");
  if (m) return [m[1], m[2], m[3]].map((v) => Math.round(Math.min(Math.max(+v, 0), 255)));
  m = /^color\(srgb\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)/.exec(s ?? "");
  if (m) return [m[1], m[2], m[3]].map((v) => Math.round(Math.min(Math.max(+v, 0), 1) * 255));
  return null;
}

/** accentColor is the system's accent, from CSS's AccentColor, or null where the browser has none. */
function accentColor() {
  if (typeof CSS === "undefined" || !CSS.supports?.("color", "AccentColor") || !document.body) return null;
  const probe = document.createElement("span");
  probe.style.cssText = "position:absolute;visibility:hidden;color:AccentColor";
  document.body.appendChild(probe);
  const c = parseRGB(getComputedStyle(probe).color);
  probe.remove();
  return c;
}

function current() {
  const a = accentColor();
  return {
    hasAccent: !!a, red: a ? a[0] : 0, green: a ? a[1] : 0, blue: a ? a[2] : 0,
    increaseContrast: !!query(queries[0])?.matches, reduceTransparency: !!query(queries[1])?.matches,
  };
}

export function makeSystem() {
  return {
    current,
    async watch(signal, changed) {
      let last = "";
      const report = () => {
        const s = current();
        const k = JSON.stringify(s);
        if (k !== last) {
          last = k;
          changed(s);
        }
      };
      report();
      for (const q of queries) query(q)?.addEventListener?.("change", report);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      for (const q of queries) query(q)?.removeEventListener?.("change", report);
    },
  };
}
