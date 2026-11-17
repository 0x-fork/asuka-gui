# row-without-key

変わるデータをたどる、`key` のないビューの `for`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

key がないと、行は位置で保たれます。項目が移動すると、元の位置の行が次の項目を表示しますが、状態、タスク、ビューは元の行のままです。

## 直し方

項目を識別するものを書きます（`for t in todos key t.id`）。位置で保ちたいときは `key i` と書きます。

## 誤った例

```asuka
struct Todo {
  id: int
  title: string
}

component Todos {
  state todos: [Todo] = []
  Column {
    for t in todos { Text(t.title) }
  }
}
```

## 正しい例

```asuka
struct Todo {
  id: int
  title: string
}

component Todos {
  state todos: [Todo] = []
  Column {
    for t in todos key t.id { Text(t.title) }
  }
}
```
