# recursive-type

自分自身の完全なコピーを含む型。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

struct のフィールドはその内側にあるので、自分自身をフィールドに持つ struct は無限に大きくなり、値を作れません。型エイリアスは自分を指せず、C の struct も自分を含められません。

## 直し方

途中のフィールドをオプショナル（`next: Node?`）か配列（`children: [Node]`）にします。

## 誤った例

```asuka
struct Node {
  value: int
  next: Node
}
```

## 正しい例

```asuka
struct Node {
  value: int
  next: Node? = none
}
```
