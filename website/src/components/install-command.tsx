import { CheckIcon, CopyIcon } from "lucide-react"
import * as React from "react"

import { cn } from "@/lib/utils"

/** The CLI's install scripts, from the repository (each release attaches them too). */
const commands = {
  unix: { prompt: "$", command: "curl -fsSL https://raw.githubusercontent.com/egoist/asuka/main/scripts/install.sh | sh" },
  windows: { prompt: "PS>", command: "irm https://raw.githubusercontent.com/egoist/asuka/main/scripts/install.ps1 | iex" },
}

/** The visitor's system's: macOS and Linux's command, or, in a browser on Windows, PowerShell's. Rendered as the former first (there's no navigator on the server). */
function useSystem(): keyof typeof commands {
  const [system, setSystem] = React.useState<keyof typeof commands>("unix")
  React.useEffect(() => {
    const nav = navigator as Navigator & { userAgentData?: { platform?: string } }
    const platform = nav.userAgentData?.platform || nav.platform || nav.userAgent
    if (/win/i.test(platform)) setSystem("windows")
  }, [])
  return system
}

export function InstallCommand({ className }: { className?: string }) {
  const { prompt, command } = commands[useSystem()]
  const [copied, setCopied] = React.useState(false)
  return (
    <button
      type="button"
      aria-label={`Copy "${command}"`}
      title={command}
      onClick={() =>
        void navigator.clipboard.writeText(command).then(() => {
          setCopied(true)
          setTimeout(() => setCopied(false), 1500)
        })
      }
      className={cn(
        "group inline-flex h-11 max-w-full items-center lg:max-w-[34rem] gap-3 hud border bg-card pr-3 pl-4 font-code text-[14px] transition-colors hover:border-foreground/30",
        className
      )}
    >
      <span className="text-signal-ink select-none">{prompt}</span>
      <span className="min-w-0 truncate">{command}</span>
      <span className="ml-2 shrink-0 text-muted-foreground transition-colors group-hover:text-foreground">
        {copied ? <CheckIcon className="size-4" /> : <CopyIcon className="size-4" />}
      </span>
    </button>
  )
}
