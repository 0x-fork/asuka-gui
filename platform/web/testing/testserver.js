// The server std/http's and std/websocket's tests talk to on every
// platform (internal/testserver): started with go run, its base URL read
// from its first line; it serves until its input closes.

import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";

const repo = fileURLToPath(new URL("../../../", import.meta.url));

/** startServer starts it: its base URL (http://127.0.0.1:<port>) and stop. */
export function startServer() {
  const p = spawn("go", ["run", "./internal/testserver/cmd"], { cwd: repo, stdio: ["pipe", "pipe", "inherit"] });
  const stop = () => new Promise((resolve) => {
    if (p.exitCode !== null || p.signalCode !== null) return resolve();
    p.once("exit", () => resolve());
    p.stdin.end();
  });
  return new Promise((resolve, reject) => {
    const lines = createInterface({ input: p.stdout });
    lines.once("line", (url) => {
      lines.close();
      resolve({ url: url.trim(), stop });
    });
    p.once("error", reject);
    p.once("exit", (code) => reject(new Error(`the test server exited (${code})`)));
  });
}
