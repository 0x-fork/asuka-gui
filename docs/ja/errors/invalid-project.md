# invalid-project

プロジェクトの asuka.toml やディレクトリを、書いてあるとおりに読めない。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

asuka.toml はアプリが何であるか（アクセントカラー、カタログ、依存関係）を記述します。形の間違った値は使えません。

## 直し方

メッセージのとおりに asuka.toml の値を直します。

## 誤った例

`app.asuka`

```asuka
component Home {
  Text("Notes")
}

app { Home() }
```

`asuka.toml`

```toml
name = "Notes"
accent = "purple"
```

## 正しい例

`app.asuka`

```asuka
component Home {
  Text("Notes")
}

app { Home() }
```

`asuka.toml`

```toml
name = "Notes"
accent = "#7c3aed"
```
