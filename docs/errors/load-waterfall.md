# load-waterfall

A load that waited for another one it doesn't use.

Reported by the core as the app runs, in development builds (`asuka dev`, `asuka run`, `asuka test`), once per place in the code: release builds don't check it.

## Why

A load in content that appeared because another load answered starts only after it. When its arguments don't read that answer, it could have run at the same time, and the screen waits for the two one after the other.

## How to fix

Start both loads in the same component, side by side, and pass the results down.

## Wrong

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

## Right

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
