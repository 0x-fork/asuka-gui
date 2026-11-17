// The pantry package's JavaScript on the web: what pantry.asuka declares,
// implemented (platform/web's test and the GUI tests' plugin app call it).
import { PantryError, Level } from "./pantry.asuka.js";

export function makePantry() {
  return {
    name: () => "home",
    items: (filter) => [{
      name: filter, weight: 1.5, level: Level.high, tags: ["a", "b"], notes: { k: "v" },
      next: { name: "inner" }, data: new Uint8Array([1, 2, 3]), took: 2500, count: -3, fresh: true,
      price: "19.990", discount: "-1.5e-1",
    }],
    add(item) {
      if (!item.name) throw new PantryError("busy");
    },
    async cook(signal, recipe, progress) {
      if (recipe === "slow") {
        await new Promise((resolve, reject) => {
          if (signal.aborted) return reject(signal.reason);
          const t = setTimeout(resolve, 10_000);
          signal.addEventListener("abort", () => { clearTimeout(t); reject(signal.reason); });
        });
      }
      progress(0.5);
      progress(1);
      return { name: recipe + " done" };
    },
    ask: (question) => question("ok?"),
    open(name) {
      if (name === "none") throw new RangeError("no shelf");
      return makeShelf();
    },
    show() {},
    counts: () => ({ a: 1, b: -2 }),
    // Decimals are their exact text: summed here by their parts.
    total: (prices) => {
      let [c, e] = [0n, 0n];
      for (const p of prices) {
        let [pc, pe] = asuka.native.decimalParts(p);
        while (e > pe) [c, e] = [c * 10n, e - 1n];
        while (pe > e) [pc, pe] = [pc * 10n, pe - 1n];
        c += pc;
      }
      return asuka.native.decimalText(c, Number(e));
    },
    shows: (view) => view != null,
  };
}

export function makeShelf() {
  return {
    stack(count, level) {
      if (level === Level.high && count > 10) throw new PantryError("empty");
    },
    close() {},
  };
}

export function makeDial(events) {
  const el = document.createElement("input");
  el.type = "range";
  return {
    el,
    setValue(v) {
      el.value = String(v * 100);
      events.changed(v * 2);
    },
    setLabel(v) { el.setAttribute("aria-label", v); },
    setItems(items) {
      if (items.length) events.picked(items[items.length - 1], items.length - 1);
    },
  };
}
