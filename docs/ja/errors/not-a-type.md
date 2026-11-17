# not-a-type

型として使われているが、型ではないもの。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

型を書く場所（パラメーター、フィールド、`:` のあと）の名前は、組み込みの型、struct、enum、interface、型エイリアスのいずれかでなければなりません。`void?` や `T??` も型ではありません。

## 直し方

意図した型を書きます。`void?` なら `bool`、オプショナルなら `?` は 1 つです。

## 誤った例

```asuka
fn shown(flag: void?) -> bool = flag != none
```

## 正しい例

```asuka
fn shown(flag: bool) -> bool = flag
```
