# load-waterfall

使ってもいない別の load を待った load。

アプリの実行中にコアが報告します。検査するのは開発ビルド（`asuka dev`、`asuka run`、`asuka test`）だけで、コードの場所ごとに 1 回だけ報告されます。リリースビルドでは検査しません。

## 理由

別の load の答えが届いたことで現れたコンテンツの中にある load は、その答えのあとでしか始まりません。引数がその答えを読んでいないなら同時に実行できたはずなのに、画面は 2 つを順番に待つことになります。

## 直し方

同じコンポーネントで 2 つの load を並べて始め、結果を下に渡します。

## 誤った例

```asuka
async fn fetchUser() -> string {
  await sleep(300ms)
  return "Ana"
}

async fn fetchPosts() -> [string] {
  await sleep(300ms)
  return ["Hello"]
}

component Posts {
  let posts = load fetchPosts()
  Text("{(posts.value ?? []).count()} posts")
}

component Profile {
  let user = load fetchUser()
  match user {
    .pending => Text("…")
    .failed(e) => Text(e.message)
    .ready(name) => Column {
      Text(name)
      Posts()
    }
  }
}

test "one after the other" {
  let app = mount(Profile())
  await app.idle()
  app.expect("1 posts")
}
```

## 正しい例

```asuka
async fn fetchUser() -> string {
  await sleep(300ms)
  return "Ana"
}

async fn fetchPosts() -> [string] {
  await sleep(300ms)
  return ["Hello"]
}

component Profile {
  let user = load fetchUser()
  let posts = load fetchPosts()
  match user {
    .pending => Text("…")
    .failed(e) => Text(e.message)
    .ready(name) => Column {
      Text(name)
      Text("{(posts.value ?? []).count()} posts")
    }
  }
}

test "side by side" {
  let app = mount(Profile())
  await app.idle()
  app.expect("1 posts")
}
```
