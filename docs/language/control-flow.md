# Control flow

`if` and `match` are values too. As one, an `if` needs an `else`, and a
block's value is its last expression:

```asuka
let label = if unread > 0 { "{unread} new" } else { "Inbox" }
let icon = match (online, unread) {
  (false, _) => "wifi.slash"
  (true, 0) => "tray"
  _ => "tray.full"
}
```

A tuple pattern (`(true, 0)`) matches each element with a pattern of
its own.

`for` goes over an array, a map's `(key, value)` pairs, or a range:
`a..<b` is the ints from `a` up to `b`, without it, and `a..b` with it
(`0..<3` is `[0, 1, 2]`, and a `for` over a range makes no array).
`while cond { … }` loops while cond holds; `break` and `continue` leave
the loop, or go on to its next turn.

`if let` and `while let` take an optional's value, or match a pattern,
and run their block when there's one: `if let u = user { … }`, `while
let job = queue.pop() { … }`, `if let .ready(v) = state { … }`. Their
condition may be a chain: lets and bools, separated by commas, in any
order. Each sees what the ones before it bound, and the block runs
when all of them hold (they're tried in order, up to the first that
doesn't):

```asuka
if let user = session.user, user.isAdmin, let team = teams[user.teamId] {
  Text("{team.name}'s admin")
} else {
  Text("Not an admin")
}
```

`let … else` binds a value or leaves. Its `else` block runs when the
optional is none (or the pattern doesn't match), and must leave:
`return`, `throw`, `break`, `continue` or `panic(…)` (the compiler says
so when it can go on). After it, the name holds the value, not an
optional:

```asuka
fn summary(id: string) -> string {
  let message = cache[id] else { return "(deleted)" }
  let .ready(body) = message.body else { return message.subject }
  return "{message.subject}: {body}"
}
```

`throw`, `return`, `break` and `continue` may go where a value does:
they leave instead of giving one (their type is `never`, which fits
any). After `??`, they say what to do with none:

```asuka
let user = cache[id] ?? throw MailError.missing(id)
for line in lines {
  let n = line.toInt() ?? continue
  total += n
}
```

They go likewise in an `if`'s or a `match` arm's branch, and as a
closure's body: `x => x ?? throw Error("empty")`.
