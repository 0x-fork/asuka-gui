# Asuka から C を呼び出す

アプリからは C を直接呼び出せます。アプリ自身の C でも、ライブラリの C でも、システムの C でもかまいません。`extern` 宣言では、C の関数が何を受け取り何を返すかを Asuka で書きます。値を変換して関数を呼び出すグルーコードは、コンパイラーが生成します。libffi もシリアライズも使いません。呼び出しで変換するのは型が必要とする分だけなので、`Int32` にはコストがかからず、文字列は Asuka がすでに持っているポインターのまま渡されます。

```asuka
extern fn zlibVersion() -> CString
extern fn crc32_of(data: bytes) -> UInt32

fn checksum(text: string) -> int = crc32_of(text.bytes())
```

C はアプリと一緒に、すべてのプラットフォーム向けにコンパイルされます。iOS ではアプリの実行ファイルに、Android では `libasuka.so` に、デスクトップではコアのライブラリに、Web では WebAssembly モジュールに組み込まれ、`asuka test` のランナーにも組み込まれます。何をコンパイルし、何をリンクするかは `asuka.toml` の `[c]` に書きます。

C を使うのは、C の API を持つライブラリ（SQLite、コーデック、ベンダーの SDK）を使うとき、C で書いたほうが速いコードのとき、ほかのプログラムとコードを共有するときです。プラットフォーム自身の言語からしか使えないもの（UIKit、Android の API、DOM）には、代わりに[ネイティブパッケージ](native.md)を書きます。

## 最初の呼び出し

アプリの C はアプリのディレクトリに置き（慣例では `c/` の下）、`asuka.toml` で指定します。

```toml
# asuka.toml
[c]
sources = ["c/*.c"]
include = ["c/include"]

[c.ios]
libraries = ["z"]
[c.android]
libraries = ["z"]
```

```c
// c/stats.c
#include <stddef.h>
#include <stdint.h>

int64_t stats_sum(const uint8_t *data, size_t len) {
    int64_t s = 0;
    for (size_t i = 0; i < len; i++) s += data[i];
    return s;
}

int32_t stats_clamp(int32_t x, int32_t lo, int32_t hi) {
    return x < lo ? lo : x > hi ? hi : x;
}
```

```asuka
extern fn stats_sum(data: bytes) -> Int64
extern fn stats_clamp(x: Int32, lo: Int32, hi: Int32) -> Int32

component Total {
  state text = "hello"

  Column {
    TextField { bind text: text }
    Text("Sum: {stats_sum(text.bytes())}")
    Text("Clamped: {stats_clamp(text.count(), 0, 3)}")
  }
}

app { Total() }

test "C adds the bytes" {
  expect(stats_sum("ab".bytes()) == 195)
  expect(stats_clamp(10, 0, 3) == 3)
}
```

`asuka dev` は C を組み込んだ開発用アプリをビルドし、`asuka test` はその C と一緒にテストを実行します。extern fn はほかの関数と同じように呼び出せます。Asuka からは、`stats_sum` は `fn(bytes) -> int` に見えます。

## C の宣言

宣言はファイルの先頭に、パッケージのほかのコードと並べて書きます。`pub` を付けることもできます。名前は C の名前そのものです。

### 関数

`extern fn name(params) -> Result` は、C の関数 `name` を宣言します。本体はなく、ジェネリックにも `async` にもできず、`throws` も付けられません。C には投げるエラーがないので、失敗しうる関数はステータスを返し、Asuka の側でそれを読み取ります（必要なら、それを独自のエラーとして投げます）。パラメーターにはデフォルト値を付けられ、呼び出し箇所で補われます。

```asuka
extern fn compress_level() -> Int32
extern fn deflate_into(data: bytes, level: Int32 = 6) -> Int64

enum CodecError: Error { failed(int) }

fn deflate(data: bytes) throws -> int {
  let n = deflate_into(data)
  if n < 0 {
    throw CodecError.failed(n)
  }
  return n
}
```

### C が所有する型

`extern type Name with release fn` は、C が渡し、Asuka が保持するポインター（`sqlite3 *` やハンドル）です。Asuka はその中身を見ません。最後の参照がなくなると、Asuka は `fn` を呼んでそれを解放します。`fn` は同じパッケージの extern fn で、`Name` を一つ受け取るものです。この関数を自分で呼ぶと、その場で解放されます。解放は一度しか行われず、そのあとで値を使う呼び出しは `the Db was released` で失敗します。`with release` を付けなければ、Asuka は何も解放しません。

```asuka
extern type Db with release db_close

extern fn db_open(path: CString) -> Db?
extern fn db_close(db: Db)
extern fn db_changes(db: Db) -> Int32

fn changes(path: string) -> int {
  if let db = db_open(path) {
    return db_changes(db)
  } // 最後の参照がなくなるので db_close(db) が呼ばれる
  return -1
}
```

`Db?` を返す関数は、C が `NULL` を返すと `none` を返します。`Db` を返す関数は、C が `NULL` を返すと失敗します（`db_open returned NULL`）。そのため、C が `NULL` を返しうるところでは `?` を付けて宣言してください。extern type の二つの値は、同じポインターを持つときに等しくなります。

### 構造体

`extern struct Name { fields }` は C の構造体で、値渡しで受け渡しされます。フィールドに使えるのは C の値型、つまり数値、`bool`、`Pointer`、ほかの extern struct です。Asuka からは、フィールドをそれぞれの Asuka の型で持つ構造体に見え、ほかの構造体と同じように作成して読み取れます。デフォルト値やメソッドを付けることもできます。フィールドは C での順番で並ぶので、C の構造体と同じ順番で宣言しなければなりません。

```asuka
extern struct Point {
  x: Float32
  y: Float32
}

extern struct Rect {
  origin: Point
  size: Point = Point(x: 0, y: 0)
}

extern fn rect_union(a: Rect, b: Rect) -> Rect

fn area(r: Rect) -> float = r.size.x * r.size.y
```

### コールバック

関数型のパラメーターはコールバックになります。C には関数ポインターとそのコンテキストという二つのパラメーターが渡り、C はコンテキストを最初の引数にして関数を呼び出します。

```asuka
extern fn each_row(sql: CString, row: fn(CString?, Int64) -> bool) -> Int32

fn names(sql: string) -> [string] {
  var out: [string] = []
  each_row(sql, (name, id) => {
    out.push(name ?? "#{id}")
    return out.count() < 100 // false を返すと止まる
  })
  return out
}
```

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int32_t each_row(const char *sql, bool (*row)(void *ctx, const char *name, int64_t id), void *ctx) {
    int32_t n = 0;
    for (int64_t id = 1; id <= 3; id++, n++) {
        if (!row(ctx, id == 2 ? NULL : "row", id)) break;
    }
    return n;
}
```

コールバックが受け取れるのは数値、`bool`、`CString`、`CString?`、`Pointer`、`Pointer?` で、返せるのは数値、`bool`、`Pointer` のいずれかか、何も返さないかです。`async` にはできず、エラーも投げません。`(fn(…))?` のパラメーターは、none のときに `NULL` を渡します。

C が extern の呼び出しの最中に、その呼び出しを行ったスレッドでコールバックを呼ぶと、コールバックはすぐに実行され、C はその結果を受け取ります。コールバックが失敗すると（インデックスが範囲外など）、それ以降の C からの呼び出しは何もせずにゼロを返し、C から戻った時点で extern の呼び出しがそのエラーを投げます。コールバックの関数が有効なのはその呼び出しのあいだだけです。C に保持させる方法は[スレッド](#スレッド)を参照してください。

## 型

| 宣言での書き方 | C | Asuka での見え方 |
|---|---|---|
| `Int8`、`Int16`、`Int32`、`Int64` | `int8_t` … `int64_t` | `int` |
| `UInt8`、`UInt16`、`UInt32`、`UInt64` | `uint8_t` … `uint64_t` | `int` |
| `int` | `int64_t` | `int` |
| `Float32` | `float` | `float` |
| `float` | `double` | `float` |
| `bool` | `bool` | `bool` |
| `CString`、`CString?` | `const char *` | `string`、`string?` |
| `bytes`、`bytes?`（パラメーターのみ） | `const uint8_t *, size_t`：2 つのパラメーター | `bytes` |
| `Pointer`、`Pointer?` | `void *` | `Pointer`：不透明な値で、`==` で比較する |
| `extern type T`、`T?` | ポインター | `T` |
| `extern struct S` | 構造体（値渡し） | `S`（構造体） |
| `fn(…) -> R`、`(fn(…) -> R)?`（パラメーターのみ） | `R (*)(void *ctx, …), void *ctx` | 関数 |

`int` をより狭い型に変換するときは、C と同じように変換されます。値は折り返され（`300` を `Int8` にすると `44`）、`int` の範囲を超える `UInt64` は負の値として返ってきます。`Int32` などの型を書けるのは extern 宣言の中だけで、それ以外の場所では `int` です（`let n: Int32` は、その旨を伝えるエラーになります）。`Pointer` はそれ自体が独立した Asuka の型で、C から渡された値を表します。Asuka はそれを保持し、比較し、C に渡し返しますが、中身を読むことはありません。

## 文字列、バイト列、メモリ

- **C に渡す `CString`** は文字列自身のバイト列で、末尾は NUL で終わっています。コピーは行いません。有効なのは呼び出しのあいだだけなので、保持したい場合は C の側でコピーします。途中に NUL を含む文字列は、C ではその NUL までとして読まれます。
- **返された `CString`** は、Asuka の文字列にコピーされます。Asuka は C の文字列を解放しません。多くの C の API は自分が所有する文字列を返す（`sqlite3_errmsg`、`zlibVersion`）ので、関数が返した文字列を解放するのは C の側です。呼び出し側が解放すべきメモリを返す関数には、渡したバッファーにコピーする数行の C を書くか、`Pointer` として返して別の extern fn で解放します。
- **`bytes`** は、バイト列へのポインターとその長さです。有効なのは呼び出しのあいだだけで、C はそれを変更しません（`const`）。`bytes?` の none は `NULL` と `0` です。長さを一緒に返せないので、関数は `bytes` を返せません。extern type か `Pointer` を返し、ほかの関数で読み出してください。
- **C が確保したもの**は C のものです。extern type なら、Asuka が使い終わったときに release が解放します。それ以外は、自分で extern fn を呼んで解放します。

## スレッド

extern の呼び出しは、それを行ったスレッドで実行されます。コンポーネント、ハンドラー、タスクの中ならメインスレッドです。そのため、時間のかかる呼び出しは、ほかの同期処理と同じように UI を止めてしまいます。`spawn` を使えば別のスレッドで実行できますが、渡せるのは値だけです（extern type の値はメインスレッドのものなので渡せません）。

```asuka
extern fn hash_file(path: CString) -> UInt64

async fn hashed(path: string) -> int {
  return await spawn { hash_file(path) }
}
```

C がコールバックを保持して、あとから呼んだり自分のスレッドから呼んだりするには、`asuka_ffi.h` の二つの関数が必要です。`asuka_ffi.h` は `[c]` のどのソースからでもインクルードできます。

```c
#include <stddef.h>
#include <stdint.h>

#include "asuka_ffi.h"

static void (*listener)(void *ctx, int64_t value);
static void *listener_ctx;

void on_change(void (*f)(void *ctx, int64_t value), void *ctx) {
    if (listener_ctx) asuka_callback_release(listener_ctx);
    asuka_callback_retain(ctx); // 呼び出しから戻る前に
    listener = f;
    listener_ctx = ctx;
}

void stop_listening(void) {
    if (listener_ctx) asuka_callback_release(listener_ctx);
    listener = NULL;
    listener_ctx = NULL;
}
```

`asuka_callback_retain` は、`asuka_callback_release` を呼ぶまで関数を保持します。`asuka_callback_release` はどのスレッドから呼んでもかまいません。保持したコールバックを C が extern の呼び出しの外で、または別のスレッドから呼ぶと、その呼び出しはメインスレッドにポストされ、すぐにゼロを返します。関数はその少しあとにメインスレッドで、コールバックを渡したコンポーネントのもとで実行されます（エラーはそのコンポーネントを囲む `Boundary` に届きます）。そのコンポーネントがすでになくなっていれば、関数は実行されません。`spawn` の中で作ったコールバックは、その呼び出しのあいだだけ有効です。

## ビルド

`asuka.toml` の `[c]` には、アプリがコンパイルする C と、リンクするものを書きます。各プラットフォームのテーブルは、そこに追加する分を書きます。

| キー | 説明 |
|---|---|
| `sources` | C のファイルです。`asuka.toml` からの相対パスの glob で指定します（`c/*.c`、`c/**/*.c`）。 |
| `include` | ソースがインクルードするディレクトリです（`-I`）。 |
| `defines` | マクロです。`NAME` または `NAME=value` と書きます（`-D`）。 |
| `flags` | コンパイラーに渡す追加のフラグです（`-Wno-unused-function`）。 |
| `libraries` | プラットフォームにあるときにリンクするライブラリです。`sqlite3` は `-lsqlite3` になり、パス（`lib/libfoo.a`）ならそのファイルをリンクします。 |
| `frameworks` | iOS と macOS でリンクする Apple のフレームワークです（`Security`）。 |

```toml
[c]
sources = ["c/*.c", "vendor/sqlite3.c"]
include = ["vendor"]
defines = ["SQLITE_THREADSAFE=0", "SQLITE_OMIT_LOAD_EXTENSION"]

[c.macos]
frameworks = ["Security"]

[c.windows]
libraries = ["bcrypt"]

[c.web]
defines = ["SQLITE_OS_OTHER=1"]
```

テーブルは `[c.ios]`、`[c.android]`、`[c.macos]`、`[c.windows]`、`[c.linux]`、`[c.web]` です。ソースはターゲットごとに、そのプラットフォームのコンパイラー（Xcode のもの、NDK のもの、Web とクロスビルドでは Zig）でコンパイルされます。リリースでは最適化され、警告はアプリの設定のまま扱われます。グルーコードはコアと一緒にコンパイルされます。

- **iOS**：C はコアの静的ライブラリに入り、ライブラリとフレームワークはアプリの実行ファイルにリンクされます。SDK には `libz`、`libsqlite3`、`libxml2`、`libc++` と Apple のフレームワークがあります。
- **Android**：ABI ごとに `libasuka.so` に入ります。NDK には `libz`、`libm`、`liblog`、`libandroid` など、NDK の安定したライブラリがあります。それ以外のものは、ソースとして、または ABI ごとのビルド済みの `.a` として持ち込みます。
- **macOS、Windows、Linux**：コアのライブラリに入ります。
- **Web**：WASI 向けにビルドされる WebAssembly モジュールに入ります。Web にはシステムのライブラリもスレッドもないので、ライブラリはソースとして持ち込み、変更点（`defines`、別の `sources`）を `[c.web]` に書きます。

パッケージの C は、そのパッケージ自身の `asuka.toml` の `[c]` に、パッケージからの相対パスで書きます。パッケージをインポートしたアプリは、その C もビルドします。アプリ自身のパッケージ（`./util`）は、アプリの `[c]` を共有します。

## 開発

- `asuka dev` は、アプリの C を組み込んだ開発用アプリをビルドします。C のファイルやヘッダー、`asuka.toml`、extern 宣言を変更すると、開発用アプリをビルドし直して起動し直します。それ以外の Asuka のコードを変更したときは、状態を保ったままリロードするだけです。
- `asuka test` と `asuka run` は、フェイクホストのランナーを C と一緒に、手元のコンピューター向け（`[c.macos]`、`[c.linux]`、`[c.windows]` のテーブル）にビルドします。そのため、テストはアプリと同じ C を呼び出します。`asuka run file.asuka` は、そのファイルと同じ場所にある `asuka.toml` を読み込みます。
- コンパイラーはすべての宣言をチェックします。C との境界を越えられない型には、その理由と代わりに書くべきものを示します（`parameter s is a string: it crosses to C as a CString`、`field name of extern struct User can't be a CString: a struct crosses by value, and the string's memory wouldn't outlive the call`）。
- C 自身の誤り（シグネチャーの間違い、解放済みのポインター）は C の側の問題です。コンパイラーには C の側が見えないので、C のヘッダーが宣言しているとおりの型で宣言し、呼び出しを `asuka test` でテストしてください。

## 仕組み

extern 宣言を持つパッケージごとに、コンパイラーは次の C を生成します。

- 関数ごとのラッパー。コアのビルトイン（標準ライブラリと同じ呼び出し規約）で、引数を変換し、関数を呼び出し、結果を変換します。
- コールバックのシグネチャーごとのトランポリン。C がコンテキストを渡して呼び出し、トランポリンが Asuka の関数を呼び出します。
- `asuka_ffi_register`。ラッパーを `ffi.<package>.<name>` という名前のビルトインとして登録します。

extern fn の本体はそのビルトインの呼び出しです。そのため、開発用アプリが実行するバイトコードからも、リリースでアプリをコンパイルした C からも、同じように呼び出されます。グルーコードは extern 宣言から各 C 関数を宣言するので、ライブラリのヘッダーは必要ありません。詳しくは `lang/LOWERING.md` の「C from Asuka」を参照してください。
