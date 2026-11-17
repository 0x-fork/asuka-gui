# unhandled-error

受け取り手のいない場所からエラーが出ていきうる。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

エラーが関数から出ていけるのは、その関数が `throws` と書いているときだけです。クロージャー、ハンドラー、コンポーネントの `let`、ファイルのトップレベルの値には、エラーを渡せる呼び出し元がありません。

## 直し方

関数に `throws` を付けるか、エラーを捕捉します（`try … catch { fallback }`、`do { } catch { }`、`try?`）。

## 誤った例

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) -> int = try parse(s)
```

## 正しい例

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) -> int = try parse(s) catch { 0 }
```
