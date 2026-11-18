# eval

Whether a coding agent can write Asuka from what a project tells it
(rfcs/0015). Each task is a small app to make:

- `tasks/<name>/prompt.md`: what to make, as a person would ask for it.
- `tasks/<name>/eval_test.asuka`: `asuka test` blocks that check the
  app's behavior on the fake host, copied into the agent's project once
  it's done.
- `tasks/<name>/reference.asuka`: an answer, which `go test ./eval`
  checks passes the tests (and that the core reports nothing about it as
  it runs), so a task can be done as it's written.

The tasks: a counter, a todo list, a search over a list, a settings
screen, a profile that loads (and holds the last one while the next
loads), a temperature converter, a stopwatch, a shopping cart, a sign-up
form, a tip calculator, a list that fails to load and retries, and a
poll.

## Running it

```sh
go run ./eval -agent 'claude -p --model claude-haiku-5-5 --no-session-persistence --allowedTools "Bash(asuka:*)" Read Write Edit Glob Grep'
go run ./eval -agent '…' -tasks counter,todos -timeout 10m -out /tmp/run
go run ./eval -report /tmp/run      # count a run's results again from its logs
```

For each task it makes a fresh project with `asuka new` (the CLI built
from this checkout), whose `AGENTS.md` is all the agent is told of the
language: it points at `asuka guide`, `asuka check --json`, `asuka
explain` and `asuka test --json`. It runs the agent's command in the
project with the prompt on its standard input (after a line saying where
it is), and `asuka` on its `PATH`: a shim (`shim/`) that runs the real
CLI and logs each call, its arguments, its exit code and the diagnostic
codes it printed. Then it adds the task's tests and runs them.

It records, per task: whether the task's tests pass; the compiler round
trips the agent made (its `asuka check`, `test`, `run` and `build`
calls); the codes those reported, and how often; and the codes `asuka
check` still reports of the project at the end. The projects, each
agent's transcript, the calls' logs and `results.json` stay in `-out`.

It needs a model and costs money, so it runs by hand, not in CI. The
codes agents hit most, and the ones they leave unfixed, are where the
messages, the pages and the guide need work.

## Results

### 2026-11-18: Claude Haiku 5.5, through Claude Code

The first run, with the CLI of this commit's tree:

```sh
go run ./eval -agent 'claude -p --model claude-haiku-5-5 --no-session-persistence --allowedTools "Bash(asuka:*)" Read Write Edit Glob Grep'
```

| Task | Passed | Round trips | Codes hit | Left |
|---|---|---|---|---|
| cart | yes | 5 | `bad-operand` ×1, `not-optional` ×1, `type-mismatch` ×2 |  |
| converter | yes | 2 |  |  |
| counter | yes | 3 |  |  |
| profile | yes | 4 |  |  |
| retry | yes | 6 |  |  |
| search | yes | 4 |  |  |
| settings | yes | 2 |  |  |
| signup | yes | 10 | `type-mismatch` ×6 |  |
| stopwatch | yes | 5 |  |  |
| tip | yes | 6 |  |  |
| todos | yes | 6 |  |  |
| votes | yes | 13 |  |  |

12 of 12 passed, in 66 round trips (5.5 a task), between 27 seconds
(counter) and 108 (signup) each. Every agent read `asuka guide` first
(once to five times per task); four looked a code up with `asuka
explain`. The codes it hit were the type checker's: `type-mismatch` most
(8 times, 6 in signup), then `bad-operand` and `not-optional` once each,
and it fixed them all: none is left in any project. None of the reactive
codes came up, and no run-time diagnostic. The round trips past two or
three were mostly the agents' own tests failing as they learned the test
driver (signup's: that `type(_, into:)` replaces a field's text rather
than adding to it; votes' took the most).

The run used this machine's Claude Code settings, so its global
instructions were in the agent's context; only `AGENTS.md` and `asuka
guide` said anything of Asuka.
