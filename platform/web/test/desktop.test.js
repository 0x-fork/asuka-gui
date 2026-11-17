// The desktop modules platform/host's packages reach by name (dock,
// recent, login), as a page has them: node --test platform/web.
import { test } from "node:test";
import assert from "node:assert/strict";
import { modules } from "../src/modules/desktop.js";

test("dock: a page has no icon: nothing asked", async () => {
  const { dock } = modules();
  assert.equal(await dock.progress(new Uint8Array(9)), null);
  assert.deepEqual([...await dock.attention(new Uint8Array([1]))], [0, 0, 0, 0]);
  assert.equal(await dock.cancelAttention(new Uint8Array(4)), null);
});

test("recent and login: a page has neither", () => {
  const { recent, login } = modules();
  assert.equal(recent.list(), "[]");
  assert.deepEqual([...login.enabled()], [0]);
  assert.throws(() => login.set(), /login.set isn't on the web/);
});
