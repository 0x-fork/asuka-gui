# import-cycle

互いにインポートし合うパッケージ。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

パッケージは、インポートしたものを検査したあとに検査されます。直接でも間接でも互いにインポートし合う 2 つのパッケージには、検査する順番がありません。

## 直し方

両方が必要とするものを 3 つ目のパッケージに移し、両方からそれをインポートします。

## 誤った例

`app.asuka`

```asuka
import ./a

let n = a.one()
```

`a/a.asuka`

```asuka
import ../b

pub fn one() -> int = 1
pub fn two() -> int = b.two()
```

`b/b.asuka`

```asuka
import ../a

pub fn two() -> int = a.one() + 1
```

## 正しい例

`app.asuka`

```asuka
import ./a

let n = a.one()
```

`a/a.asuka`

```asuka
import ../b

pub fn one() -> int = 1
pub fn two() -> int = b.two()
```

`b/b.asuka`

```asuka
pub fn two() -> int = 2
```
