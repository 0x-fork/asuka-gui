# Asuka for Zed

Support for the [Asuka language](../../docs/language/README.md) in
[Zed](https://zed.dev):

- **Highlighting**, the **outline**, bracket matching, indentation and
  text objects, from Asuka's tree-sitter grammar
  ([editors/tree-sitter-asuka](../tree-sitter-asuka)).
- **Run buttons** on each `test "…"` (`asuka test --run`), `bench "…"`
  (`asuka test --bench --run`) and `app { … }` (`asuka dev`), and tasks
  that test and check the file's package.
- Everything the language server, `asuka lsp`, does: diagnostics as you
  type, completion, hover with `///` docs, go to definition, references,
  rename, formatting on save (`asuka fmt`'s), symbols, signature help.

## Install

In Zed's command palette, run **zed: install dev extension** and pick
this directory. Zed builds the extension with Rust, which must be
installed with [rustup](https://rustup.rs), and fetches the grammar from
this repository at the commit `extension.toml`'s `rev` names.

## The language server

The extension runs `asuka lsp` with, in order:

1. the command `lsp.asuka.binary.path` names in Zed's settings;
2. the `asuka` on your `PATH` (the install script puts it there);
3. the CLI of Asuka's latest release, which it downloads into its own
   directory (macOS on Apple silicon, Linux, Windows).

```json
{
  "lsp": {
    "asuka": {
      "binary": {
        "path": "/path/to/asuka",
        "arguments": ["lsp", "--verbose"]
      }
    }
  }
}
```

`--verbose` logs what the server does to Zed's log (**zed: open log**).
After rebuilding `asuka`, **editor: restart language server** runs the
new one.

## Working on it

- `languages/asuka/highlights.scm` is a copy of the grammar's
  `queries/highlights.scm` (`go test ./lang/syntax` says when they
  differ); the other queries are Zed's own.
- A change to the grammar takes a commit of it: `rev` in
  `extension.toml` is the commit Zed fetches.
