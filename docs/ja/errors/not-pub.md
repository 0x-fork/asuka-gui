# not-pub

ほかのパッケージ（またはストア、ワーカー）が公開していない宣言。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

パッケージの宣言は、`pub` と書かない限りそのパッケージだけのものです。ストアとワーカーのメンバーも同じです。そのおかげで、パッケージはほかから使われていない部分を自由に変えられます。

## 直し方

宣言に `pub` を付けるか、パッケージが公開しているものを使います。

## 誤った例

`app.asuka`

```asuka
import ./util

let answer = util.secret()
```

`util/util.asuka`

```asuka
fn secret() -> int = 42
```

## 正しい例

`app.asuka`

```asuka
import ./util

let answer = util.secret()
```

`util/util.asuka`

```asuka
pub fn secret() -> int = 42
```
