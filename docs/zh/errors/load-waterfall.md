# load-waterfall

一个 load 等待了另一个它并不使用的 load。

应用运行时由核心报告，只在开发构建中检查（`asuka dev`、`asuka run`、`asuka test`），代码里的每个位置只报告一次；发布构建不做这项检查。

## 原因

如果一个 load 所在的内容是因为另一个 load 返回结果才出现的，它就要等到那个 load 完成后才开始。只要它的参数没有读取那个结果，两者本可以同时运行，屏幕却要先后等它们两个。

## 如何修正

在同一个组件里并排启动这两个 load，再把结果传下去。

## 错误示例

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
  app.expect("1 posts")
}
```

## 正确示例

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
  app.expect("1 posts")
}
```
