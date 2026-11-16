# 数据

应用的数据放在几个地方：小数据用 `asuka/storage`，记录用 `asuka/sqlite` 存进 SQLite，用户各台设备共享的值用 `asuka/icloud` 存进 iCloud，文档的文件用 `asuka/document`。本页逐一介绍它们，也介绍如何用 `asuka/http` 访问网络、用 `asuka/watch` 监视文件，以及哪些工作会在主线程之外进行。

## 数据存在哪里

每个包都把数据放在平台自家应用放数据的地方：

| | `asuka/storage`（以及 `asuka/store`） | `asuka/sqlite` 用普通名字打开的数据库 |
|---|---|---|
| iOS | UserDefaults | 应用容器里的 Application Support |
| macOS | UserDefaults | `~/Library/Application Support/<bundle ID>` |
| Android | SharedPreferences | 应用的 files 目录 |
| Windows | `%LOCALAPPDATA%\<name>` 里的 `store.json` | `%LOCALAPPDATA%\<name>` |
| Linux | `$XDG_DATA_HOME/<id>` 里的 `store.json` | `$XDG_DATA_HOME/<id>`（即 `~/.local/share/<id>`；在 Flatpak 里是沙盒自己的目录） |
| Web | `localStorage` | 暂不支持 |

`<name>` 是 `asuka.toml` 的 `name`，`<id>` 是它的 `id`。`storage.dirName()` 会告诉你，在应用当前运行的平台上这些值存在哪里。

在 `asuka dev` 下，桌面应用的数据目录是 `asuka dev` 自己的一个目录（在用户的缓存目录里，每个应用一个），所以开发时试过的东西永远不会和已安装版本的数据混在一起。

## 小数据：asuka/storage

`asuka/storage` 用来在多次启动之间保存小数据：设置、已登录用户的名字、上次打开的标签页。`storage.Value` 是一个 [store](language.md#store)，它的 `value` 会被保存下来：一开始是上次保存的值，第一次则是默认值；之后每次给它赋值都会被保存。和任何状态一样，读取它会被跟踪，写入它会立刻显示出来：

```asuka
import asuka/storage

let wifiOnly = storage.Value("wifiOnly", false)

component DownloadSettings {
  Column {
    Toggle { bind on: wifiOnly.value; title: "Download on Wi-Fi only" }
    if wifiOnly.value { Text("Large files wait for Wi-Fi") { color: .secondaryLabel } }
  }
}
```

值可以是 JSON 能表示的任何东西：数字、字符串、数组、结构体、枚举。默认值看不出类型时，把类型写出来：

```asuka
struct Account {
  id: string
  email: string
}

let accounts: storage.Value<[Account]> = storage.Value("accounts", [])

fn signIn(a: Account) {
  accounts.value.push(a)
}
```

保存的内容放进平台的偏好设置：iOS 和 macOS 上是 UserDefaults，Android 上是 SharedPreferences，Web 上是 `localStorage`，Windows 和 Linux 上是一个文件 `store.json`，改动后稍等片刻才写入。`storage.flush()` 会立即写入，适合在应用可能被结束之前调用（比如后台任务的最后一步）。`value.reset()` 会忘掉保存的值。保存的值在 store 创建时读取：如果它已经不能解码成这个类型（类型改了），得到的就是默认值。

`asuka/store` 做的是同样的事，只是不包成 store：`store.read(key, default)`、`store.write(key, value)` 和 `store.remove(key)`，适合在你自己选定的时机保存某个值的代码。

它不是数据库：记录很多或很大时，用 SQLite。

## 记录：asuka/sqlite

`asuka/sqlite` 是应用的 SQLite 数据库。它就是 SQLite 本身，同一个版本在每个平台上用同样的方式构建，来自 sqlite [插件](plugins.md)：导入这个包，CLI 就会把插件的库随应用一起发布。核心每次调用它执行一整条语句，在主线程之外运行，并把返回的行直接解码成你要的类型。[`examples/notes`](../../examples/notes) 用的就是它。

### 打开与迁移

```asuka
import asuka/sqlite

struct Note {
  id: int
  text: string
  done: bool
}

fn openDB() throws -> sqlite.DB {
  let db = try sqlite.open("notes.db")
  try db.migrate([
    // 1
    "create table notes (id integer primary key, text text not null, done bool not null default 0)",
    // 2
    "alter table notes add column created integer not null default 0",
  ])
  return db
}

let db = (try? openDB())!
```

`sqlite.open(name)` 打开数据库（不存在就创建）：普通名字是应用数据目录里的一个文件（见[数据存在哪里](#数据存在哪里)），绝对路径就是那个文件，`":memory:"` 是内存中的数据库。文件数据库使用预写式日志（WAL），并打开外键约束。

`db.migrate(steps)` 把表结构更新到最新。steps 是数据库按顺序排列的各个版本：每一步的 SQL（用分号分隔多条语句）只运行一次，在一个事务里执行，版本号记在 `PRAGMA user_version` 里。处于版本 1 的数据库会从第 2 步开始执行；新数据库会全部执行。新步骤加在末尾，已经发布的步骤不要再改。它在主线程上运行，所以在应用启动时调用。

### 查询

```asuka
async fn undone() throws -> [Note] = try await db.query("select * from notes where done = ? order by id", [false])

async fn noteCount() throws -> int {
  let n: [int] = try await db.query("select count(*) from notes", [])
  return n.first() ?? 0
}
```

`db.query<T>(sql, args)` 在主线程之外运行查询，把结果行作为若干个 `T` 返回。结构体的字段按列名填充（`select *` 会填满 `Note`）；没有对应列的字段取零值（`0`、`""`、`false`、`[]`、`none`），没有对应字段的列会被忽略。其他类型的 `T` 取第一列：计数用 `[int]`，名字用 `[string]`。

列会尽量转换成字段的类型：整数可以转成 `int`、`float`、`bool`（非零为真）或 `string`；实数可以转成 `float` 或 `int`；文本可以转成 `string`、`bytes`、它所写的数字、按 case 名对应的无负载枚举，以及（当文本是 JSON 时）结构体、数组或映射；blob 可以转成 `bytes` 或 JSON；null 是字段的零值，可选类型则是 `none`。转换不了的列会让查询失败，并给出列名。

### 参数

语句的参数可以是元组或数组，按位置对应（`?`、`?1`）；也可以是结构体或 `[string: T]`，按名字（`:name`、`@name`、`$name`）以及位置对应：

```asuka
async fn add(text: string) throws {
  try await db.exec("insert into notes (text, created) values (?, ?)", (text, 1700000000))
}

struct Rename {
  id: int
  text: string
}

async fn rename(r: Rename) throws {
  try await db.exec("update notes set text = :text where id = :id", r)
}
```

每个参数可以是 `int`、`float`、`bool`（存成 0 或 1）、`string`、`bytes`（blob）、没有负载的枚举 case（存它的序号），或者它们的可选类型（`none` 就是 null）。`[]` 表示没有参数。

### 执行

`db.exec(sql, args)` 在主线程之外运行一条语句（insert、update、delete、create……），或用分号分隔的多条语句，并且排在它之前的语句之后：紧跟在 `exec` 后面的查询能看到它写入的内容。事务就是 SQL：在一次 `exec` 里写 `begin`、各条语句、`commit`。

### 实时查询

`db.watch<T>(sql, args)` 是一个实时查询：一个装着结果行的 store，现在就在主线程之外查询一次，之后每当通过这个数据库对它读过的表的写入提交时，再查询一次。在组件里创建时，它会随组件一起停止。

```asuka
let search = "select * from notes where ?1 = '' or instr(lower(text), lower(?1)) > 0 order by done, id desc"

component Notes {
  state query = ""
  let notes: sqlite.Live<Note> = db.watch(search, [query])

  Column {
    TextField { bind text: query; placeholder: "Search" }
    if notes.loading { Text("Loading…") }
    if let e = notes.error { Text(e) { color: .red } }
    for n in notes.rows key n.id { Text(n.text) }
  }
}
```

`Live<T>` 有 `rows`（最新的结果行）、`loading`（第一次查询完成前为 true）和 `error`（上次查询失败的原因，成功一次之后为 none）。它们都是状态：显示它们的地方会跟着变化。

实时查询只为它读过的表重新运行。它在 SQLite 准备语句时得知这些表，包括视图背后的表；当对其中某张表的写入提交时它就重新运行，触发器和外键动作写入的行也算在内。对别的表的写入不会让它重新运行。

它的参数可以读取状态，就像 `[query]` 那样：参数变化时，`let` 会重新创建这个实时查询。新的实时查询会先用同一条 SQL 上次的结果，等新结果到来，所以用户打字时列表不会变空。

`db.changed()` 让所有实时查询重新运行：用于数据库自己看不到的变化（另一个进程的修改、文件被替换）。

### 错误

SQLite 返回的错误，消息就是 SQLite 的消息，后面带上结果码：`sqlite: UNIQUE constraint failed: users.email (2067)`。

```asuka
async fn signUp(email: string) -> string {
  do {
    try await db.exec("insert into users (email) values (?)", [email])
    return ""
  } catch e {
    if e.message.contains("UNIQUE constraint failed") { return "That email is taken" }
    return e.message
  }
}
```

### 库文件

SQLite 在每个平台上都是 3.54.1 版，内置 FTS5、R*Tree、Geopoly、数学函数、`dbstat`、`STAT4` 和 `soundex`，不支持可加载扩展，临时表放在内存里。它是插件的库 `asqlite`（由 `sdk/sqlite` 构建和发布），`asuka build` 会把它放到核心找得到的地方：Mac 的 `Contents/Frameworks`、Windows 应用的 `.exe` 旁边、Linux 安装前缀的 `lib/<app>` 里、iOS 应用 `Frameworks` 里的一个 framework、APK 每个 ABI 里 `libasuka.so` 的旁边。桌面上的 `asuka dev`、`asuka test` 和 `asuka run` 会让核心使用插件的构建产物或它发布的文件（`ASUKA_LIB_ASQLITE`），后者只下载一次，存进用户的缓存。Web 上暂时还没有 `asuka/sqlite`：`open` 会抛出错误。

## 用户各设备共享的值：asuka/icloud

在 iOS 和 macOS 上，`icloud.Value(key, default)` 是一个保存在 iCloud 键值存储里的 store，另一台设备修改它时会重新读取，就像 `storage.Value` 保存在本机一样：

```asuka
import asuka/icloud

let textSize = icloud.Value("textSize", 17.0)
```

`icloud.account()` 和 `icloud.files(scope)` 会跟踪账户和应用在 iCloud 里的文件，`readFile`、`writeFile`、`conflicts` 和 `resolve` 用来操作这些文件：见 [iCloud](icloud.md)。在其他平台上，值保持默认值以及你在本机给它设置的值。

## 文档的文件：asuka/document

`document.open(path, read:, write:)` 把一个文件的内容变成一个 store：用应用的 `read` 在主线程之外读取，每次 `edit` 之后稍等片刻用它的 `write` 保存，可以还原；如果在读取之后另一个程序改了这个文件，就成为冲突：

```asuka
import asuka/document

component Editor(path: string) {
  let doc = document.open(path, read: data => data.string(), write: text => text.bytes())

  Column {
    if let text = doc.content {
      TextField { text: text; multiline: true; on change(t) { doc.edit(t) } }
    }
    if doc.state == .conflict {
      Button("Keep Mine") { on press { await doc.saveAnyway() } }
      Button("Take Theirs") { on press { await doc.revert() } }
    }
  }
}
```

## 网络：asuka/http

`asuka/http` 通过平台自己的客户端发送请求，所以代理、受信任的证书、缓存和 cookie 都和系统的行为一致。请求需要等待，所以它是 `async` 的：

```asuka
import asuka/http

struct Release {
  name: string
}

async fn latest() throws -> string {
  let r = try await http.get("https://api.github.com/repos/egoist/asuka/releases/latest")
  if !r.ok() { throw Error("HTTP {r.status}") }
  let rel: Release = try r.json()
  return rel.name
}
```

在浏览器里，向其他源发请求需要那个源返回 CORS 头。

## 监视文件：asuka/watch

`watch.path(dir)` 跟踪一个文件夹（或文件）的变化：应用显示的文件夹、编辑器里打开的项目、另一个程序会写入的设置文件。它的 `events` 是一个按批到来的变化流：

```asuka
import asuka/watch

component Folder(dir: string) {
  state changes: [string] = []
  state problem = ""

  task(dir) {
    do {
      let w = try await watch.path(dir, ignore: [".*", "node_modules/"])
      for await events in w.events {
        for e in events { changes.push("{e.op} {e.path}") }
      }
    } catch e {
      problem = e.message
    }
  }

  Column {
    if !problem.isEmpty() { Text(problem) }
    for c in changes { Text(c) }
  }
}
```

监视会记住它看到的文件，每当系统说有东西变了，就再看一遍并比较：事件是前后的差异，而不是造成差异的每一步。一个文件连续写了很多次，只是一次 `write`；创建后又删掉的文件什么都不算；在原路径上被替换的文件（编辑器和原子保存就是这么做的）是一次 `write`；在文件夹内移动的文件或文件夹是一次 `rename`，带上它的 `oldPath`。把事件当作“该重新读文件了”的提示。系统丢掉变化时，监视会把所有东西重新看一遍，这一批以一个 `overflow` 开头。

`ignore` 接受 `.gitignore` 的简单模式（`*.log`、`node_modules/`、`/dist`、`src/**/gen`）；`recursive: true` 也监视其中的子文件夹，`attributes: true` 会报告权限的变化。它用的是各个桌面系统自己的方式：macOS 上是 FSEvents（一个流就能覆盖整棵目录树），Linux 上是 inotify（每个文件夹一个 watch，上限是 `fs.inotify.max_user_watches`），Windows 上是 `ReadDirectoryChangesW`。手机和 Web 上没有可用的监视方式：`watch.path` 会抛出 unsupported 错误。离开循环（或组件被移除）就会停止监视。[`examples/files`](../../examples/files) 展示了一个实时更新的文件夹。

## 主线程

界面运行在主线程上，在主线程上等待的工作会让界面卡住。对数据来说：

- **在主线程之外，用 await 等待**：`db.exec`、`db.query` 和实时查询的每次查询；`icloud` 的调用；`document` 的读写；`http` 的请求。它们的 `await` 让界面可以继续运行。
- **在主线程上，很快完成**：`sqlite.open` 和 `db.migrate`（在应用启动时）；`storage.Value` 和 `asuka/store`，它们读写的是平台的偏好设置（在 Windows 和 Linux 上是内存：文件稍后才写）。

你自己的、会拖住主线程的工作（解析大文件、缩放图片）放进 `spawn { … }`，它会在另一个线程上运行（[异步](language.md#异步)）。
