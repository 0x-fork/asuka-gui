# invalid-extern

C が裏付けられない extern 宣言。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

extern fn の名前は C の関数の名前です。extern type の `release` は、同じパッケージにある、その型を 1 つ受け取る extern fn です。extern struct は C のメモリレイアウトなので、ほかに宣言するものはありません。

## 直し方

解放する関数を `extern fn close(x: T)` と宣言し、ほかは C のとおりに宣言します。

## 誤った例

```asuka
extern type Handle with release close

fn close(h: Handle) {}
```

## 正しい例

```asuka
extern type Handle with release close

extern fn close(h: Handle)
```
