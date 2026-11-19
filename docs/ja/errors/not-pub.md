# not-pub

ほかのパッケージ、ほかのファイル（private）、ストア、ワーカーが公開していない宣言。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

パッケージの宣言は、`pub` と書かない限りそのパッケージだけのものです。ストアとワーカーのメンバーも同じです。`private` なもの（宣言や、型のフィールドとメソッド）は、そのファイルとパッケージのテストだけのものです。そのおかげで、パッケージはほかから使われていない部分を自由に変えられ、型の不変条件も型自身が守れます。

## 直し方

宣言に `pub` を付ける（または `private` を外す）か、公開されているものを使います。private なフィールドに既定値のない型は、その static fn で作ります。

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
