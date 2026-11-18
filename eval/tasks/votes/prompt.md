Make this app a poll.

- The root component is `Poll`, and the `app` block shows it.
- A text field with the placeholder `Option` and a button titled `Add
  option` add an option to the poll (not an empty one, nor one it has).
- Each option is a row: its name and its votes, `<name>: N`, and a
  button titled `Vote <name>` that adds a vote.
- A text says `Leading: <name>`, the option with the most votes (of
  those tied, the one added first), or `No votes yet`.
