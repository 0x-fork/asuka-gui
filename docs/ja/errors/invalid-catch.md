# invalid-catch

到達しない、または捕捉するものがない `catch`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

catch は順番に試されるので、すべてを捕捉する catch のあとの catch には到達しません。throw しないコードのあとの catch にも捕捉するものがありません。`do` には catch が必要で、catch にはエラー型かその case を書きます。

## 直し方

具体的な catch を先に、すべてを捕捉する catch を最後に置き、throw しうるものだけを捕捉します。

## 誤った例

```asuka
enum ParseError: Error {
  empty
}

fn parse(s: string) throws -> int {
  if s.isEmpty() { throw ParseError.empty }
  return 1
}

fn size(s: string) -> int {
  do {
    return try parse(s)
  } catch e {
    return -1
  } catch ParseError.empty {
    return 0
  }
}
```

## 正しい例

```asuka
enum ParseError: Error {
  empty
}

fn parse(s: string) throws -> int {
  if s.isEmpty() { throw ParseError.empty }
  return 1
}

fn size(s: string) -> int {
  do {
    return try parse(s)
  } catch ParseError.empty {
    return 0
  } catch e {
    return -1
  }
}
```
