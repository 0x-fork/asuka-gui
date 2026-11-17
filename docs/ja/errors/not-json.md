# not-json

JSON で読み書きできない型。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

JSON にあるのは数値、文字列、真偽値、配列、オブジェクト、null です。それらからなる struct、オプショナル、ペイロードのない列挙型は対応づけられますが、クロージャー、ビュー、ストアには対応する JSON がありません。

## 直し方

データだけの struct にデコードし、ほかのものはそこから作ります。

## 誤った例

```asuka
import std/json

struct Settings {
  onChange: fn()
}

fn read(text: string) throws -> Settings = try json.decode<Settings>(text)
```

## 正しい例

```asuka
import std/json

struct Settings {
  theme: string
}

fn read(text: string) throws -> Settings = try json.decode<Settings>(text)
```
