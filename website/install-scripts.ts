// Serve the CLI's install scripts from the site: asuka.egoist.dev/install.sh
// and /install.ps1, copied from the repository's scripts/ as the site
// builds (one source; each release attaches the same files).
import fs from "node:fs/promises"
import path from "node:path"
import type { Plugin } from "vite"

const scripts = ["install.sh", "install.ps1"]

export function installScripts(repoDir: string): Plugin {
  const read = (name: string) => fs.readFile(path.join(repoDir, "scripts", name), "utf8")
  return {
    name: "asuka-install-scripts",
    applyToEnvironment: (environment) => environment.name === "client",
    async generateBundle() {
      for (const name of scripts) this.emitFile({ type: "asset", fileName: name, source: await read(name) })
    },
    configureServer(server) {
      server.middlewares.use(async (req, res, next) => {
        const name = (req.url?.split("?", 1)[0] ?? "").slice(1)
        if (!scripts.includes(name)) return next()
        try {
          res.setHeader("Content-Type", "text/plain; charset=utf-8")
          res.end(await read(name))
        } catch (error) {
          next(error)
        }
      })
    },
  }
}
