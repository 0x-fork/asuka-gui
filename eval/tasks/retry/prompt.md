Make this app a list that loads, and can try again.

- The root component is `Inbox`, and the `app` block shows it.
- Copy this function into the app as it is: the server fails the first
  time it's asked, and answers the second.

  ```asuka
  async fn fetchMessages(attempt: int) throws -> [string] {
    await sleep(300ms)
    if attempt == 1 { throw Error("offline") }
    return ["Hello", "Lunch?", "Invoice"]
  }
  ```

- The inbox asks for attempt 1 at first, showing `Loading` meanwhile.
- When it fails, it shows `Couldn't load: <the error's message>` and a
  button titled `Retry`, which asks again with the next attempt.
- Loaded, it shows each message as a text, and `3 messages`.
