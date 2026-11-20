# tree-sitter-asuka

The [Asuka language](../../docs/language/README.md)'s grammar for
[tree-sitter](https://tree-sitter.github.io), which Zed's extension
([editors/zed](../zed)) highlights with, and Neovim and Helix can too.

`lang/syntax` is the language's parser and its `GRAMMAR.md` the
grammar; this one follows them:

- `grammar.js`: the productions. Expressions come twice, as they are
  and in a control clause's head (`if f(x) {`), where a `{` starts the
  body rather than a call's trailing block.
- `src/scanner.c`: the tokens a regular expression can't make: the
  newlines that end items (not before a line that continues the one
  before: `.name`, a binary operator, `else` or `catch`), `"""` strings'
  text, raw strings, and the first `>` of `>>` (so `Box<Box<int>>`
  closes twice).
- `src/parser.c`, `src/grammar.json`, `src/node-types.json`: generated
  by `tree-sitter generate`, and committed (editors build the grammar
  from `src`).
- `queries/highlights.scm`: the highlights.
- `test/corpus`: what the grammar must make of the hard cases.

```sh
npm install                  # tree-sitter-cli
npx tree-sitter generate     # after changing grammar.js
npx tree-sitter test         # test/corpus
npx tree-sitter parse ../../examples/counter/app.asuka
```

`go test ./lang/syntax` checks that its keywords are the scanner's, and
soft where the parser's are. Before committing a change, parse the
repository's `.asuka` files: every one but those that are errors on
purpose (`lang/check/testdata/errors`) parses without an `ERROR` node.

```sh
find ../.. -name '*.asuka' -not -path '*/node_modules/*' -not -path '*/.claude/*' > /tmp/files
npx tree-sitter parse --paths /tmp/files -q -s
```
