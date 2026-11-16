import fs from "node:fs/promises"
import path from "node:path"
import type { Code, Root } from "mdast"
import remarkParse from "remark-parse"
import { unified } from "unified"

import type { HomeData } from "@/lib/home"
import { highlightCode } from "@/lib/markdown.server"

/** The example at the top of the docs: an app, in Asuka. */
export async function loadHome(): Promise<HomeData> {
  const readme = await fs.readFile(path.resolve(process.cwd(), "../docs/README.md"), "utf8")
  const tree = unified().use(remarkParse).parse(readme) as Root
  const app = tree.children.find((n): n is Code => n.type === "code" && n.lang === "asuka")
  if (!app) throw new Error("docs/README.md: expected an asuka example")
  return { main: await highlightCode(app.value, "asuka") }
}
