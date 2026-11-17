// asqlite.wasm for the tests: std/sqlite's C built to WebAssembly, the
// module the page's SQLite worker runs. ASUKA_SQLITE_WASM names one; else
// asqlite.go builds it (with Go and Zig, into the build cache, once) and
// says where.

import { execFileSync } from "node:child_process";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const repo = fileURLToPath(new URL("../../../", import.meta.url));

/** wasmPath is the module's path. */
export function wasmPath() {
  if (process.env.ASUKA_SQLITE_WASM) return process.env.ASUKA_SQLITE_WASM;
  const out = execFileSync("go", ["run", "./platform/web/testing/asqlite.go"], { cwd: repo, encoding: "utf8", stdio: ["ignore", "pipe", "inherit"] });
  return out.trim();
}

/** loadWasm is the module's bytes. */
export async function loadWasm() {
  return await readFile(wasmPath());
}

/** sqliteVersion is the version of std/sqlite's SQLite, as asq_version says it (3054001): its sqlite3.h's. */
export async function sqliteVersion() {
  const header = execFileSync("go", ["run", "./platform/web/testing/asqlite.go", "-header"], { cwd: repo, encoding: "utf8", stdio: ["ignore", "pipe", "inherit"] }).trim();
  const h = await readFile(header, "utf8");
  return Number(/#define SQLITE_VERSION_NUMBER (\d+)/.exec(h)[1]);
}
