import { createServerFn } from "@tanstack/react-start"
import { staticFunctionMiddleware } from "@tanstack/start-static-server-functions"

import { loadHome } from "@/lib/home.server"

export interface HomeData {
  /** The docs' first example, an app in Asuka, highlighted. */
  main: string
}

export const getHome = createServerFn({ method: "GET" })
  .middleware([staticFunctionMiddleware])
  .handler(() => loadHome())
