// The pantry package's JavaScript on the web: what pantry.asuka declares,
// implemented (platform/web's test and the GUI tests' plugin app call it).
import { PantryError, Level } from "./pantry.asuka.js";

export function makePantry() {
  return {
    name: () => "home",
    items: (filter) => [{
      name: filter, weight: 1.5, level: Level.high, tags: ["a", "b"], notes: { k: "v" },
      next: { name: "inner" }, data: new Uint8Array([1, 2, 3]), took: 2500, count: -3, fresh: true,
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
