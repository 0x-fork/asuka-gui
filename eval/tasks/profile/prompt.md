Make this app a profile that loads.

- The root component is `Profile`, and the `app` block shows it.
- Copy this function into the app as it is: it's the server.

  ```asuka
  async fn fetchName(id: int) throws -> string {
    await sleep(500ms)
    if id == 3 { throw Error("no such user") }
    return "User {id}"
  }
  ```

- The profile shows user 1 at first: the text `Loading` until its name
  arrives, then the name.
- A button titled `Next` shows the next user. While the next one loads,
  the last name stays on screen, and a text says `Updating`; it goes away
  once the name arrives.
- When a name can't be loaded, a text says `Couldn't load` (and `Next`
  still goes on to the next user).
