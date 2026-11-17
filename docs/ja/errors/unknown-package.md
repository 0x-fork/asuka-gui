# unknown-package

インポートがどのパッケージも指していない。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

インポートできるのは、標準ライブラリ（`std/…`）、プラットフォーム SDK（`platform/…`）、ファイルの隣のディレクトリ（`./util`）、asuka.toml にある依存関係です。そのどれでもないパス、名前が変わったパッケージは読み込めません。

## 直し方

パスを直すか、依存関係を追加する（`asuka add`）か、パッケージの新しい名前を使います。

## 誤った例

```asuka
import std/clock

let started = clock.now()
```

## 正しい例

```asuka
import std/time

let started = time.now()
```
