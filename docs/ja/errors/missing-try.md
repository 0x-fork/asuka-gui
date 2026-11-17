# missing-try

throw する関数を `try` なしで呼び出している。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

失敗しうる関数は `throws` と書き、呼び出すたびに `try` と書きます。そうすることで、エラーが出ていきうる場所がすべて書き表されます。

## 直し方

`try f(…)` と書きます。エラーのときに `none` がほしいなら `try? f(…)`、代わりの値を返すなら `try f(…) catch { fallback }` です。

## 誤った例

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) throws -> int = parse(s)
```

## 正しい例

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) throws -> int = try parse(s)
```
