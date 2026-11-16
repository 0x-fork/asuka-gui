// A worker under Node (run.mjs): platform/web/src/spawn-worker.js, or the
// page's SQLite worker (sqlite-worker.js), on a worker thread's port.
import { parentPort, workerData } from "node:worker_threads";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

// fetch of files: SQLite's module.
globalThis.fetch = async (u) => new Response(await readFile(fileURLToPath(u)), { headers: { "content-type": "application/wasm" } });

const mod = await import(workerData.src);
const post = (m, transfer) => parentPort.postMessage(m, transfer);
if (workerData.serve === "sqlite") {
  parentPort.on("message", (data) => mod.handle(data, post));
} else {
  mod.serve({ postMessage: post, set onmessage(f) { parentPort.on("message", (data) => f({ data })); } });
}
