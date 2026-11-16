// The API reference: a page for each package of the standard library
// (/docs/std/time) and of the platform SDK (/docs/platform/camera), and
// one listing each set (/docs/std, /docs/platform). `asuka doc --all`
// writes the packages' pages from their source, once per build, into
// website/.reference; the lists are made here, in each language. Used by
// the Vite config (the pages to prerender, the Markdown downloads) and
// the server (the pages, the sidebar), so no React and no @/ imports.
import { execFileSync } from "node:child_process"
import fs from "node:fs"
import path from "node:path"

import { localized, type Locale } from "./locales.ts"
import { messages } from "./messages.ts"

/**
 * A part of the reference: the language (docs/language, written), the
 * standard library's packages or the platform SDK's (asuka doc's).
 */
export type ReferenceGroup = "language" | "std" | "platform"

export const referenceGroups: ReferenceGroup[] = ["language", "std", "platform"]

/** The parts asuka doc writes, from the packages' source. */
export const generatedGroups: ReferenceGroup[] = ["std", "platform"]

/** A package, as `asuka doc --all`'s index.json lists it. */
export interface ReferencePackage {
  /** Its import path: std/time. */
  path: string
  /** Its page: std/time, at /docs/std/time. */
  slug: string
  description: string
}

/** The reference's set of a page, or undefined for a page of the docs. */
export function referenceGroupOf(slug: string): ReferenceGroup | undefined {
  const m = /^(language|std|platform)(?:\/[a-z0-9-]+)?$/.exec(slug)
  return m ? (m[1] as ReferenceGroup) : undefined
}

let generated: string | undefined

/**
 * The directory of the packages' pages: `asuka doc --all` run once per
 * process from the repository (go run, as the docs are the checkout's),
 * into website/.reference.
 */
export function referenceDir(repoDir: string): string {
  if (generated) return generated
  const dir = path.join(repoDir, "website", ".reference")
  const tmp = `${dir}.${process.pid}.tmp`
  fs.rmSync(tmp, { recursive: true, force: true })
  execFileSync("go", ["run", "./cmd/asuka", "doc", "--all", "-o", tmp], { cwd: repoDir, stdio: ["ignore", "ignore", "inherit"] })
  fs.rmSync(dir, { recursive: true, force: true })
  fs.renameSync(tmp, dir)
  generated = dir
  return dir
}

/** The packages, sorted by path. */
export function referencePackages(repoDir: string): ReferencePackage[] {
  return JSON.parse(fs.readFileSync(path.join(referenceDir(repoDir), "index.json"), "utf8")) as ReferencePackage[]
}

/** Every page asuka doc's part of the reference has: the two lists, then each package's. */
export function referenceSlugs(repoDir: string): string[] {
  return [...generatedGroups, ...referencePackages(repoDir).map((p) => p.slug)]
}

/**
 * A page of the reference as Markdown, or undefined for none: a
 * package's, as asuka doc wrote it; a list's, in the language of locale.
 */
export function referenceSource(repoDir: string, locale: Locale, slug: string): string | undefined {
  const found = referenceGroupOf(slug)
  if (!found || !generatedGroups.includes(found)) return undefined
  const group = found as "std" | "platform"
  const words = messages[locale].reference
  const packages = referencePackages(repoDir)
  if (slug === group) {
    const rows = packages
      .filter((p) => p.path.startsWith(group + "/"))
      .map((p) => `| [\`${p.path}\`](${localized(locale, `/docs/${p.slug}`)}) | ${p.description.replace(/\|/g, "\\|")} |`)
    return [
      `# ${words[group]}`,
      words[`${group}Intro`],
      `| ${words.package} | ${words.about} |\n|---|---|\n${rows.join("\n")}`,
    ].join("\n\n") + "\n"
  }
  if (!packages.some((p) => p.slug === slug)) return undefined
  const source = fs.readFileSync(path.join(referenceDir(repoDir), `${slug}.md`), "utf8")
  // In another language, a note that the page is in English.
  return words.english ? source.replace(/\n\n/, `\n\n> ${words.english}\n\n`) : source
}

/** Where a package's source is in the repository: std/time, sdk/camera. */
export function referenceSourceDir(slug: string): string | undefined {
  const [group, name] = slug.split("/")
  if (!name) return undefined
  return group === "std" ? `std/${name}` : `sdk/${name}`
}
