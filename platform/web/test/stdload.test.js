// The standard library's packages load on the web as the page loads them:
// their generated module (std/*/web/*.asuka.js) imported first, which
// imports the package's own (*.js) before its body runs. A package's
// module that reads the generated enums as it loads, rather than as it's
// called, throws there (the enums aren't made yet).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readdirSync, existsSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import path from "node:path";
import * as native from "../src/native.js";

globalThis.asuka = { native };

const std = fileURLToPath(new URL("../../../std/", import.meta.url));

for (const pkg of readdirSync(std).sort()) {
  const web = path.join(std, pkg, "web");
  if (!existsSync(web)) continue;
  for (const f of readdirSync(web).filter((f) => f.endsWith(".asuka.js")).sort()) {
    test(`std/${pkg}: ${f} loads`, async () => {
      await assert.doesNotReject(import(pathToFileURL(path.join(web, f)).href));
    });
  }
}
