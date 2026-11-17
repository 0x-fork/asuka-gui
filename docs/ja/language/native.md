# ネイティブコード

プラットフォームにしかできないことは、ネイティブパッケージに入れます。Asuka で宣言し、各プラットフォームの言語で実装します。

```asuka
pub native interface Haptics {
  fn impact(style: Feedback)
}

pub native component MapView(region: Region) {
  prop pins: [Pin] = []
  event regionChanged(region: Region)
}
```

モジュールを作ることも呼び出すことも失敗する可能性がある（そのプラットフォームにコードがないかもしれない）ので、どちらにも `try` を付けます（`let h = try Haptics()`、`try h.impact(.light)`）。`asuka native generate` は、各プラットフォームが実装する Swift、Kotlin、JavaScript、C のコードを生成します。詳しくは[ネイティブコード](../native.md)を参照してください。

C は直接呼び出せます。`extern` 宣言では、C の関数が何を受け取り何を返すかを C の型で書きます。Asuka からは、それぞれが Asuka 自身の型に見えます（`Int32` は `int`、`CString` は `string`）。

```asuka check
extern type Db with release db_close
extern fn db_open(path: CString) -> Db?
extern fn db_close(db: Db)
extern fn db_each(db: Db, sql: CString, row: fn(CString?) -> bool) -> Int32

extern struct Point {
  x: Float32
  y: Float32
}
```

`extern type` は C が渡すポインターで、最後の参照がなくなると release で解放されます。`extern struct` は値渡しで受け渡しされ、C に渡した関数はコールバックになります。アプリがコンパイルしてリンクする C は、`asuka.toml` の `[c]` に書きます。詳しくは [Asuka から C を呼び出す](../c.md)を参照してください。
