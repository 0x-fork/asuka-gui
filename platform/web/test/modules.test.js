// The host's module registry (modules.js): the app's modules first, the
// host's own made on their first call, one file at a time, and a clear
// error for a module the web hasn't: node --test platform/web.
import { test } from "node:test";
import assert from "node:assert/strict";
import "../src/host.js"; // as on a page: the host loaded, then modules/ as they're called
import { Modules, files } from "../src/modules.js";

const fakeHost = () => ({ options: {}, assets: "assets", nodes: new Map() });

test("the app's module first, then the host's; none for the rest", async () => {
  const m = new Modules(fakeHost());
  m.registered.set("dock", { progress: () => "mine" });
  assert.equal(await (await m.method("dock", "progress"))(), "mine");
  // A method the app's module hasn't is the host's.
  assert.deepEqual([...await (await m.method("dock", "attention"))()], [0, 0, 0, 0]);
  assert.equal(await m.method("camera", "takePhoto"), null);
  assert.equal(await m.method("secureStore", "get"), null);
  assert.equal(await m.method("recent", "nothing"), null);
});

test("a file's modules are made once, on the first call of one of them", async () => {
  const m = new Modules(fakeHost());
  assert.equal(m.made.size, 0);
  const a = await m.builtin("contacts");
  const b = await m.builtin("calendar");
  assert.ok(a && b);
  assert.equal(m.made.size, 1);
  assert.equal(await m.builtin("contacts"), a);
});

test("every module the registry names, its file has", async () => {
  const m = new Modules(fakeHost());
  for (const [name, file] of Object.entries(files)) {
    if (file === "system") continue; // notify and push need a browser's service worker
    assert.ok(await m.builtin(name), name);
  }
});
