#!/usr/bin/env node
// Runs the asuka binary for this computer, which npm installed as one of
// @asuka/cli's optional dependencies (only the one whose os and cpu
// match). Nothing is downloaded or built at install time.
"use strict";
const { spawnSync } = require("node:child_process");
const path = require("node:path");

const pkg = `@asuka/cli-${process.platform}-${process.arch}`;
const exe = process.platform === "win32" ? "asuka.exe" : "asuka";
let bin;
try {
  bin = path.join(path.dirname(require.resolve(`${pkg}/package.json`)), "bin", exe);
} catch {
  console.error(`asuka: no build for ${process.platform}-${process.arch} (${pkg} isn't installed).
Install with optional dependencies (npm i -g @asuka/cli, without --no-optional), or get a build from https://github.com/egoist/asuka/releases.`);
  process.exit(1);
}
const r = spawnSync(bin, process.argv.slice(2), { stdio: "inherit" });
if (r.error) {
  console.error("asuka:", r.error.message);
  process.exit(1);
}
process.exit(r.status === null ? 1 : r.status);
