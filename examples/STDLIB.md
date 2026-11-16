# What the examples still ask of the platforms

Every example compiles and its tests pass on the fake host: the
prelude, the elements and the standard library they call are declared
and implemented (lang/prelude, std/, core/src/ui). What's below is what
Asuka's earlier SDK did that its library doesn't yet, each of which
needs code on the platforms (Swift, Kotlin, JavaScript, the desktop
hosts) rather than in the compiler or the core. The examples get by
without them, as each entry says.

| What | Needs | The examples meanwhile |
|---|---|---|
| `asuka/update`: the desktop's updater (checks, release notes, a signed download, relaunch) | the Go updater, ported to the Swift and C hosts (std/update, the hosts' `update` module) | none uses it |
| The app block's `shareSheet: fn(ShareSheet) -> View` (a sheet of the app's over another app's share) and `pushRewrite` (a notification rewritten before it shows) | an iOS share extension and notification service extension built with the app, and Android's share target activity | downloader takes shares with `on share(items)`, which opens the app |
