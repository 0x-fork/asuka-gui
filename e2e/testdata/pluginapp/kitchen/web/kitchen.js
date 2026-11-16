// The kitchen package's JavaScript: its module, on the web. It reads the
// package's enum as it loads (what the generated module makes first).

import { Unit } from "./kitchen.asuka.js";

const salt = { weight: 1.5, unit: Unit.gram, tags: ["dry", "fine"] };

export function makeKitchen() {
  return {
    name: () => "home",
    items: (filter) => [{ name: filter, ...salt }],
  };
}
