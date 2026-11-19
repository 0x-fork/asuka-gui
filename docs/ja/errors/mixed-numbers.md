# mixed-numbers

型の違う数値に演算子を使っている。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

Asuka は数値を勝手に変換しません。`int` と `float`、`u8` と `int` は `+`、`<`、`&` の両辺に並べられません。片方を黙って広げると、値が切り捨てられたり、ラップしたり、小数部を失ったりする場所が見えなくなるからです。整数リテラルは反対側の型になるので、`level + 1` は問題ありません。

## 直し方

片方をもう片方の型に変換します：`int(level) + offset`、`float(n) * 0.5`、`u8(n)`（n が収まらなければエラーを投げます。`u8(truncating: n)` は下位ビットを残します）。

## 誤った例

```asuka
fn total(level: u8, bonus: int) -> int = level + bonus
```

## 正しい例

```asuka
fn total(level: u8, bonus: int) -> int = int(level) + bonus
```
