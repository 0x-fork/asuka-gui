# 程序

除了应用和库，Asuka 还能写**程序**：没有窗口、也没有视图的命令行工具。只要一个包的文件里声明了 `fn main()`，它就是程序，就像 Go 的 `package main`、Cargo 的 `src/main.rs`。程序的构建方式和应用一样：同一门语言、同一套标准库（文件、JSON、时间、SQLite、HTTP……），并编译成 macOS、Windows 和 Linux 上的原生可执行文件。

```asuka
import std/os

fn main() {
  let name = os.args().first() ?? "world"
  print("Hello, {name}!")
}
```

## 创建程序

```sh
asuka new --bin greet   # asuka.toml，以及带 main 和一个测试的 main.asuka
cd greet
asuka run -- Ada        # Hello, Ada!
asuka test              # 运行它的测试块
asuka build             # 生成可执行文件 dist/greet
```

`asuka new` 创建应用，`asuka new --lib` 创建库，`asuka new --bin` 创建程序。一个应用项目也可以自带程序，放在各自的目录里，见[应用自带的程序](#应用自带的程序)。

## main

程序从 `fn main()` 开始。它不接受参数（参数通过 `os.args()` 读取），也不返回值（退出码用 `os.exit(code)` 指定）；它可以是 `async` 的，也可以抛出错误。带 `fn main` 的包不能有 `app` 块，也不能声明组件：程序不显示视图。只有构成程序的那个包才有入口；它导入的包里如果也有 `main`，那只是一个普通函数。

```asuka
import std/fs

async fn main() throws {
  let text = try await spawn { try fs.readText("notes.txt") }
  print("{text.lines().count()} lines")
}
```

程序的运行过程如下：

- 先创建各个包的值（它们的 `let`），然后运行 `main`。`async` 的 `main` 作为一个 task 运行：和在应用里一样，`await` 等待时其他工作照常进行，不会占住主线程。
- `main` 返回时，程序以退出码 0 结束。在此之前，它启动后仍在运行的东西（正在读取的 stream、某个包的 task）会先被取消，它们的 `defer` 会执行。
- `main` 抛出（或没有捕获）的错误会让程序以 1 结束：`error: ` 加上错误的消息写到 stderr，后面跟着抛出的位置，最内层的函数在前：

  ```text
  error: bad name fail
      check (main.asuka:18)
      main (main.asuka:8)
  ```

- `os.exit(code)` 立即以 `code` 结束程序：stdout 和 stderr 会被刷新，但 `defer` 和其他 task 不会执行。
- Ctrl-C 会像 `main` 自己结束那样，以取消的方式结束异步的 `main`：先执行它的 `defer`，再执行其他所有 task 的，退出码是 130（128 加上信号的编号：终止信号，也就是 `kill` 默认发送的信号，是 143；挂断信号是 129）。再按一次 Ctrl-C 会立即结束程序；不是异步的 `main` 也是这样立即结束，因为不 await 的代码无法取消。用 `os.signals()` 可以把这些信号交给程序自己处理（见[信号](#信号)）。
- `print(a, b)` 向 stdout 写一行，各个值之间用空格隔开；`eprint` 则写到 stderr。两者都接受 `separator:` 和 `terminator:`（`print("…", terminator: "")` 写完不换行）。

## 进程：std/os

| 函数 | 作用 |
|---|---|
| `os.args()` | 参数，不含程序本身 |
| `os.program()` | 程序启动时的名字或路径（`argv[0]`） |
| `os.env(name)` | 环境变量的值，没有设置时为 `none` |
| `os.environ()` | 所有环境变量，`[string: string]` |
| `os.cwd()` | 工作目录 |
| `os.exit(code)` | 立即结束进程（类型是 `never`） |
| `os.readLine()` | 读取 stdin 的一行（不含行尾），读到末尾时为 `none`；会等待这一行 |
| `os.readAll()` | stdin 剩下的全部内容，`bytes` |
| `os.lines()` | 逐行读取 stdin（一个 `Stream<string>`），在另一个线程上读，程序可以同时做别的事 |
| `os.writeTo(.stdout, text)`、`writeBytesTo` | 原样写出文本或字节，不加行尾，目标是 `.stdout` 或 `.stderr` |
| `os.terminal(.stdout)` | 某个流是否是终端（有人在看），而不是管道或文件：只在终端上输出颜色和提示 |
| `os.signals(kinds)` | 按到达顺序给出这些种类的信号（一个 `Stream<os.Signal>`：`.interrupt`、`.term`、`.hangup`），被读取的信号不再结束程序 |
| `os.hostname()`、`os.pid()`、`os.ppid()` | 机器名、本进程的 id 和父进程的 id |
| `os.user()` | 进程以哪个用户身份运行：一个 `User`，包含 `name`、`home`、`uid` 和 `gid`（Windows 上 `uid`、`gid` 为 `none`） |
| `os.platform()`、`os.arch()` | 运行所在的系统和 CPU：一个 `Platform`（`.macOS`、`.windows`、`.linux`……）和一个 `os.Arch`（`.arm64`、`.amd64`……） |
| `os.executable()` | 程序自己的可执行文件，绝对路径，链接已解析：和程序一起发布的文件就在 `fs.dir(os.executable() ?? ".")` 里 |

### 信号

如果程序有要先收尾的事情（写了一半的文件、服务器上还在处理的请求），可以自己读取信号：只要有流在读某一种信号，这种信号就不会结束程序，由程序决定怎么处理。`os.signals()` 读取 Ctrl-C 和终止信号；`os.signals([.hangup])` 读取挂断信号，守护进程通常把它当作“重新读取配置”。

```asuka check
import std/os

async fn main() throws {
  let jobs = ["a", "b", "c"]
  for await s in os.signals() {
    eprint("{s}: finishing {jobs.count()} jobs first")
    break
  }
}
```

在 Windows 上，Ctrl-C 和 Ctrl-Break 是 `.interrupt`；控制台关闭、用户注销或系统关机是 `.term`，这时 Windows 会等待几秒钟。手机和 Web 上没有信号，这个流不会给出任何信号。

```asuka
import std/os

async fn main() throws {
  var total = 0
  for await line in os.lines() {
    total += line.toInt() ?? 0
  }
  print(total)
  if os.env("DEBUG") != none {
    eprint("summed stdin")
  }
}
```

在手机和 Web 上，应用的 `os.args()` 为空，stdin 也是空的，`os.exit` 会抛出错误：那里的应用不能结束自己的进程。桌面应用的 `os.args()` 是它自己进程的参数。

## 命令行参数：std/flag

`flag.parse<T>(os.args())` 根据编译器对 `T` 的了解，把命令行读成一个 `T`：不需要另外维护一张和字段对应的参数表。

```asuka
import std/flag
import std/os

enum Unit { lines, words, bytes }

/// Counts what's in files.
struct Options {
  /// -v Say each file's count.
  verbose: bool = false
  /// -u What to count.
  unit: Unit = .lines
  /// How many files at once.
  jobs: int = 4
  /// Names to leave out.
  skip: [string] = []
  /// The files to count.
  files: [string]
}

fn main() {
  let o = flag.parse<Options>(os.args())
  print("{o.unit} in {o.files.count()} files, {o.jobs} at once")
}
```

- **有默认值**的字段是选项：`dryRun` 对应 `--dry-run`。它的文档注释就是帮助里的那一行；如果注释的第一个词是 `-x`（一个字母或数字），那就是它的短名。
- `bool` 是开关：`--verbose`、`-v`；默认开启的开关（`color: bool = true`）用 `--no-color` 关闭，也可以写 `--color=false`。
- `int`、`float`、`string` 和没有附带值的枚举成员（`--unit words`：成员名的 kebab-case 写法）需要一个值：`--jobs 8`、`--jobs=8`、`-j8`、`-j 8`。数组可以反复给出（`--skip a --skip b`）；可选类型可以省略。
- 短开关可以合写：`-vf` 就是 `-v -f`，`-vu words` 把值交给 `-u`。
- **没有默认值**的字段是位置参数，顺序和字段一致：数组接收剩下的全部参数（所以它是最后一个），可选类型可以省略（放在不能省略的之后）。
- `--` 结束选项：之后的内容都是位置参数，不管是否以 `-` 开头。
- `-h` 和 `--help` 打印帮助并以 0 退出；`--version` 打印 asuka.toml 里的 `version` 并以 0 退出。
- 用法出错时，打印问题所在和用法，并以 2 退出：

  ```text
  error: unknown option --jbos (did you mean --jobs?)

  Usage: wc [options] [<files>...]

  For more, try 'wc --help'.
  ```

结构体的文档注释是帮助的第一行：

```text
Counts what's in files.

Usage: wc [options] [<files>...]

Arguments:
  [<files>...]  The files to count.

Options:
  -v, --verbose                   Say each file's count.
  -u, --unit <lines|words|bytes>  What to count. (default: lines)
      --jobs <n>                  How many files at once. (default: 4)
      --skip <skip>...            Names to leave out.
  -h, --help                      Show this help.
      --version                   Show the version.
```

解析器处理不了的类型，会在对应字段处报编译错误：没有直接写出来的默认值（比如函数的返回值）、map、把 `-h` 用作短名、没有默认值的开关。`flag.tryParse<T>(args)` 做 `parse` 的工作，但不打印也不退出：它抛出 `flag.FlagError`（`.help(text)`、`.version(text)`、`.usage(problem, usage, program)`）；`flag.help<T>()` 返回帮助文本。

### 子命令

如果一个枚举的成员各自附带一个选项结构体，它就是一组子命令，就像 `git add` 和 `git commit`：用 `flag.parse<Command>(args)`，或者在结构体里放一个这个枚举类型的字段，它出现在结构体自己的选项之后。成员的文档注释是它在帮助里的那一行；`tool add --help` 显示这个子命令自己的帮助。

```asuka
import std/flag
import std/os

struct AddOptions {
  /// -f Add ignored files too.
  force: bool = false
  /// The files to add.
  paths: [string]
}

struct CommitOptions {
  /// -m The commit's message.
  message: string = ""
}

/// A tiny version control.
enum Command {
  /// Add files to the index.
  add(AddOptions)
  /// Record the index.
  commit(CommitOptions)
  /// Show the working tree's state.
  status
}

struct Cli {
  /// -q Say less.
  quiet: bool = false
  command: Command
}

fn main() {
  let cli = flag.parse<Cli>(os.args())
  match cli.command {
    .add(o) => print("adding {o.paths}")
    .commit(o) => print("committing: {o.message}")
    .status => print("on main")
  }
}
```

## 运行其他程序：std/command

`std/command` 用来运行其他程序。参数是一个列表，原样传过去，绝不经过 shell：带空格或 `;` 的文件名就是一个参数。会阻塞的操作（读管道、等待结束）都在其他线程上进行，主线程不会等待。

```asuka
import std/command

async fn main() throws {
  let out = try await command.run("git", ["status", "--short"])
  print(out.text())

  let p = try command.start("ping", ["-c", "3", "example.com"])
  for await line in p.lines() {
    print("> {line}")
  }
  print("ping exited with {await p.wait()}")
}
```

- `command.run(program, args, cwd:, env:, clearEnv:, stdin:, check:)` 运行一个程序直到结束，返回 `Output`：`code`、`stdout` 和 `stderr`（字节；`text()` 和 `errorText()` 得到文本，还有 `ok()`）。`stdin:` 的内容会交给它；`check: true` 时，非 0 的退出码会变成 `CommandError.failed`。
- `command.status(program, args, …)` 让程序直接使用当前程序的 stdin、stdout 和 stderr（它打印的内容会显示出来），返回退出码。
- `command.start(program, args, …, stdin:, stdout:, stderr:)` 启动程序并返回一个 `Process`：`pid`，`stdout()` 和 `stderr()`（字节流）或 `lines()` 和 `errorLines()`，`write(text)` 和 `closeStdin()`，`wait()`，`output()`（读完全部输出再取退出码），以及 `kill(.term)`。每个流是一个 `Stdio`：`.piped`（stdout 和 stderr 的默认值）、`.inherit` 或 `.null`（stdin 的默认值）。
- 程序名不是路径时，会在 `PATH` 上查找；`env:` 叠加在当前程序的环境变量之上（`clearEnv: true` 时只用它）。找不到程序时抛出 `CommandError.notFound`。
- 等待程序（`run`、`output`、`wait`）的 task 被取消时，那个程序也会被结束。
- 在 macOS 和 Linux 上，被信号结束的程序，退出码是 128 加上信号的编号。在 Windows 上，程序按 `CreateProcess` 的规则查找（会补上 `.exe`）；`.bat` 或 `.cmd` 要通过 `cmd /c` 作为程序来运行。

这里没有 `command.shell("…")`：shell 会解释传给它的文本里的引号、`$` 和 `;`，文件名就是这样变成命令的。确实需要执行一行 shell 命令时，就显式地运行 shell：`command.run("sh", ["-c", line])`。

它可以在程序和桌面应用（macOS、Windows、Linux）里使用；手机和 Web 上的应用不能运行其他程序（`CommandError.unsupported`）。

## 终端：std/term

`std/term` 负责给程序的输出加上样式，以及向终端前的人提问。下面这个程序用来创建项目：它会询问项目名，覆盖已有目录前先确认，输出的颜色可以用 `--color` 参数打开或关闭。

```asuka check
import std/flag
import std/fs
import std/os
import std/term

/// Makes a project.
struct Options {
  /// When to use colors.
  color: term.ColorMode = .auto
  /// Where to make it (the name asked for, if not given).
  dir: string?
}

async fn main() throws {
  let o = flag.parse<Options>(os.args())
  term.setColors(o.color)
  let name = await term.prompt("Project name:", default: "demo") ?? "demo"
  let target = try fs.abs(o.dir ?? name)
  if fs.exists(target) && !(await term.confirm("{target} exists. Overwrite?")) {
    eprint("{term.red("error:", stream: .stderr)} nothing was made")
    os.exit(1)
  }
  try await spawn { try fs.mkdirAll(target) }
  print("{term.green("✓")} made {term.bold(target)}")
}
```

```text
$ new
Project name: [demo] notes
✓ made /home/ann/notes
```

- **样式。** `term.bold(text)`、`dim`、`italic`、`underline` 以及八种颜色（`red`、`green`、`yellow`、`blue`、`magenta`、`cyan`、`white`、`black`）会用终端的控制码把文本包起来。要同时设置好几项，就用 `term.Style`，它也是一个可以保存下来反复使用的值：`let warn = term.Style(color: .yellow, bold: true)`，然后调用 `warn.paint(text)`。`Style` 还提供亮色（`.brightRed`）和 `background`。带样式的文本嵌在另一段带样式的文本里时，内层保持自己的样式，结束后外层的样式会接着生效。
- **什么时候显示样式。** 只有当输出流是终端、没有设置 `NO_COLOR`（[no-color.org](https://no-color.org)）并且 `TERM` 不是 `dumb` 时才会显示；设置了 `FORCE_COLOR` 时，即使输出到管道也会显示（值为 `0` 或 `false` 则关闭）。每个样式函数都接受 `stream:` 参数，指明文本要打印到哪个流（默认是 `.stdout`）。`term.colors(.stderr)` 返回当前的判断结果，`term.setColors(.always)`、`.never` 或 `.auto` 可以覆盖它，作用于两个流，或用 `stream:` 只作用于其中一个。在选项结构体里写一个 `color: term.ColorMode = .auto` 字段，就得到了 `--color always|never|auto` 参数。在 Windows 上，第一次输出样式时会让控制台开始解析这些控制码（开启它的虚拟终端处理）。
- **提问。** `term.prompt(question, default:)` 提出问题，返回输入的那一行，去掉首尾空白：直接回车得到 `default`（显示为 `[demo]`），返回 `none` 表示 stdin 已经结束（按了 Ctrl-D，或管道已关闭）。`term.confirm(question, default: false)` 显示 `[y/N]`（默认值为 true 时是 `[Y/n]`），直到得到 y、yes、n、no 或空回答为止。`term.secret(question)` 在关闭终端回显的状态下读取一行，适合输入密码或 token；读完这一行、task 被取消、或者用 Ctrl-C 结束程序时，回显都会恢复。问题写到 stderr，和 git、gh 的做法一样，这样程序的输出通过管道传出去时（`new | tee log`），stdout 里只有程序真正的输出。这几个函数都是 `async` 的：这一行在另一个线程上读取，程序里的其他 task 照常运行。
- **尺寸。** `term.width()` 和 `term.height()` 是终端的列数和行数（如果 `COLUMNS` 和 `LINES` 是数字，优先使用它们）；没有任何一个流是终端时为 `none`：`term.width() ?? 80`。

在测试里，用 `os.fake(stdin:)` 回答这些问题，问题本身可以在 `o.stderr()` 里找到。测试的流都不是终端，所以不会输出样式，除非测试用 `os.fake(env: ["FORCE_COLOR": "1"])` 或 `term.setColors(.always)` 明确要求；这里只看 `os.fake` 的 `env:`，所以运行测试时的环境变量不会影响测试的输出。手机和 Web 上没有终端：没有样式，`prompt` 返回 `none`，`width` 也是 `none`。

### 对齐的表格

一列要对齐，每个单元格在终端里占的列数就得一样，而这和 `count()` 的结果不是一回事：中文、日文、韩文字符，全角字符，还有大多数 emoji 都占两列；组合用的重音符号和零宽连接符（ZWJ）不占列。`term.textWidth(text)` 按终端的显示方式数列数，一次数一个字素簇（和 Rust 的 unicode-width、`Bun.stringWidth` 一样），控制码不占列，所以带样式的文本量出来的宽度就是它显示的宽度。`term.padEnd`、`padStart`、`center`、`truncate`（在截断处加 `…`，不会把一个字符切成两半）和 `fit`（正好占满给定宽度：先截断再补齐，数字用 `align: .end` 右对齐）都按这个宽度计算，`term.table(rows)` 则能直接排好一整张表：

```asuka check
import std/term

struct Row {
  name: string
  city: string
  score: int
}

fn main() {
  let rows = [Row(name: "王小明", city: "北京", score: 98), Row(name: "Ana", city: "Lisboa 🇵🇹", score: 7), Row(name: "佐藤花子", city: "東京", score: 85)]
  var cells = [["Name", "City", "Score"]]
  for r in rows {
    cells.push([r.name, r.city, "{r.score}"])
  }
  print(term.table(cells, align: [.start, .start, .end], width: term.width()))
}
```

```text
Name      City       Score
王小明    北京          98
Ana       Lisboa 🇵🇹      7
佐藤花子  東京          85
```

每一列和它最宽的单元格一样宽，列与列之间隔 `gap:` 个空格（默认 2 个）；给了 `width:` 时，最宽的列会先让出空间，直到每行都放得下，放不下的单元格会被截断（这里用的是终端的宽度，输出到管道时为 none，不做限制）。东亚宽度为“模糊”的字符（`±`、`…`）占一列，这也是终端在没有特别设置时的显示方式；制表符和其他控制字符不占列，需要的话先把制表符展开。`std/flag` 生成的帮助也是这样对齐选项的。

## 日志：std/log

`std/log` 在程序运行的同时记录它做了什么：每一行有一个级别（`debug`、`info`、`warn`、`error`），由一条消息和若干字段组成，字段说明这件事和什么有关。下面这个程序用来备份文件：`-v` 会逐个显示文件，`--json` 则输出给机器读的 JSON 行。

```asuka check
import std/flag
import std/fs
import std/log
import std/os

/// 把文件复制到一个目录里。
struct Options {
  /// -v 逐个文件说明正在做什么。
  verbose: bool = false
  /// 输出 JSON 行，供机器读取。
  json: bool = false
  /// 复制到哪里。
  to: string = "backup"
  /// 要复制的文件。
  files: [string]
}

async fn main() throws {
  let o = flag.parse<Options>(os.args())
  if o.verbose {
    log.setLevel(.debug)
  }
  if o.json {
    log.setFormat(.json)
  }
  let run = log.with(["to": o.to])
  try await spawn { try fs.mkdirAll(o.to) }
  for file in o.files {
    run.debug("copying", ["file": file])
    let target = fs.join([o.to, fs.base(file)])
    try await spawn { try fs.copy(from: file, to: target) } catch e {
      run.error("copy failed", ["file": file, "error": "{e}"])
      os.exit(1)
    }
  }
  run.info("copied", ["files": o.files.count()])
}
```

```text
$ backup -v notes.md todo.md
2026-11-19 10:04:05.120 DEBUG copying                                  to=backup file=notes.md
2026-11-19 10:04:05.123 DEBUG copying                                  to=backup file=todo.md
2026-11-19 10:04:05.125 INFO  copied                                   to=backup files=2
```

- **字段。** 一行的字段是一个 `[string: json.Value]`：任何 JSON 能表示的值都可以原样放进去（`["port": port, "tls": true, "peer": peer]`；在需要 `json.Value` 的地方，值会自动转换成它），所以 JSON 行会把数字写成数字。写成文本时，字符串就是它的文本，其他值写成 JSON；含有空格、引号或 `=` 的值会加上引号（`path="my notes.md"`）。`log.with(fields)` 返回一个 `Logger`，它写出的每一行都带着这些字段；它的 `with` 还可以再添加字段。某一行自己的字段和 logger 的字段同名时，以这一行的为准。
- **级别。** 低于最低级别的行不会写出。最低级别默认是 info；`ASUKA_LOG` 可以改变它（`debug`、`info`、`warn`、`error` 或 `off`，和 Rust 的 `RUST_LOG` 一样），程序也可以用 `log.setLevel(.debug)` 设置，并且程序的设置优先：所以上面的程序只在给了 `-v` 时才设置它。不写出的行只花一次判断的开销，但它的字段照样会先算出来，就像任何函数调用的参数一样：生成起来费事的字段，用 `if log.enabled(.debug) { … }` 包起来。
- **格式。** 在终端上，时间（本地时间）会变暗，级别会带颜色，是否显示颜色由 `std/term` 对 stderr 的判断决定（`NO_COLOR`、`FORCE_COLOR`、`term.setColors`）。`log.setFormat(.json)` 每行写一个 JSON 对象，和 Go 的 `log/slog` 一样：`{"time":"2026-11-19T10:04:05.125+08:00","level":"INFO","msg":"copied","to":"backup","files":2}`；`time: false` 不写时间，适合日志已经由 journald 或容器运行时加上时间戳的服务。
- **写到哪里。** 程序的日志写到 stderr，这样 stdout 仍然只有程序的输出（`backup | tee list`）。应用的日志写到各平台开发者习惯查看的地方：iOS 和 macOS 上是统一日志系统（Xcode 的控制台、“控制台” App），Android 上是 logcat，Web 上是浏览器的控制台（按级别分别用 `console.debug` 到 `console.error`），Windows 上是调试器的输出；Linux 上写到 stderr，桌面应用从终端启动时也写到 stderr。`asuka dev` 也会把它们打印在自己的终端里。

在测试里，这些行可以在 `o.stderr()` 里找到，时间戳来自测试的时钟（从 `2026-01-01 00:00:00.000` 开始）；在测试中只有 `os.fake(env:)` 能设置 `ASUKA_LOG`。

## 文件和目录：std/fs

程序和应用一样，用 `std/fs` 读写文件（[数据](data.md)），另外还提供了命令行工具查找文件时需要的几个函数：

- `fs.home()`：用户的主目录（`HOME`，Windows 上是 `USERPROFILE`；在测试里，如果 `os.fake` 的 `env:` 提供了它们，就用其中的值）。
- `fs.tempDir()`：存放临时文件的目录（`TMPDIR`，没有时是 `/tmp`；Windows 上是 `GetTempPath` 返回的目录）。
- `fs.abs(path)`：把 `path` 相对于工作目录转成绝对路径，并做规范化。
- `fs.walk(dir, skip:)`：目录下所有层级的全部内容，以 `Entry`（`path`、`name`、`isDir`、`size`、`modified`）的列表返回：先是每个目录本身，再是它里面的内容，按名字排序。`skip:` 列出不进入的目录名；指向目录的链接也不会进入。

```asuka check
import std/fs

async fn main() throws {
  let root = try fs.abs(".")
  let entries = try await spawn { try fs.walk(root, skip: [".git", "node_modules"]) }
  let sources = entries.filter(e => !e.isDir && fs.ext(e.name) == ".asuka")
  print("{sources.count()} files in {root}")
  let config = fs.join([fs.home(), ".config", "count", "config.toml"])
  if fs.exists(config) {
    print("using {config}")
  }
}
```

会访问磁盘的调用（`walk`、`readText`、`exists`……）要等系统返回结果才会继续：遍历很大的目录树时放在 `spawn { }` 里，程序的其他 task 就能照常运行。失败时和 `std/fs` 的其他调用一样抛出 `FsError`：没有权限列出的目录是 `.denied(path)`。

### 大文件，分段读写

`readText` 会一次读入整个文件。太大的文件（日志、导出数据）要分段读：`fs.lines(path)` 以流的形式给出文件的每一行，每次读 64 KB，读完最后一行后关闭文件。`fs.open(path, mode:)` 返回一个 `File`，让程序边处理边读写：`readLine()`、`read(count:)`、`readAll()`、`write(data)`、`writeText(text)`、`seek(offset, from:)`、`size()`、`flush()` 和 `close()`，它们都在另一个线程上等待系统。打开方式有 `.read`、`.write`（从空文件开始）、`.append` 和 `.readWrite`。

```asuka check
import std/fs

/// 逐行把日志里的错误行复制到另一个文件。
async fn main() throws {
  let out = try await fs.open("errors.log", mode: .write)
  defer { try? await out.close() }
  var kept = 0
  for await line in fs.lines("server.log") {
    if line.contains("ERROR") {
      try await out.writeText(line + "\n")
      kept += 1
    }
  }
  print("{kept} errors")
}
```

写入的内容在缓冲区写满时、调用 `flush()` 时以及 `close()` 时才会真正写进文件；用完 `File` 要关闭它，就像上面用 `defer` 那样。

`fs.glob(pattern)` 按排序列出匹配模式的路径：`*`、`?`、`[a-z]`、`{a,b}`（写在原始字符串 `r"…"` 里，这样 `{` 不会被当作插值）以及表示任意层目录的 `**`。`fs.matches(pattern, path)` 检查单个路径。`fs.chmod(path, 0o755)` 设置权限，`fs.symlink(target, at:)` 和 `fs.readLink(path)` 创建和读取符号链接，`Entry` 里也有 `mode` 和 `isLink`。

```asuka check
import std/fs

async fn testFiles() throws -> [string] = try await spawn { try fs.glob(r"src/**/*_{test,spec}.asuka") }
```

## 归档与压缩：std/archive、std/compress

`std/archive` 用来列出、读取、解压和创建 zip 与 tar 归档（`.zip`、`.tar`、`.tar.gz`）；`std/compress` 用 gzip（也可以是 zlib 或原始 DEFLATE）压缩字节和文件，例如 `compress.encodeFile(from: log, to: log + ".gz")`。文件是逐块读写的，所以再大的归档也只占用很少的内存；不过这些调用在工作时会阻塞，要放在 `spawn { }` 里调用。

来自别处的归档也可以放心解压：如果某个条目会落到目标目录之外（`../../.profile`、绝对路径、经过链接的路径），就会抛出 `unsafePath`，那里不会写入任何东西；`limit:` 限制解压出的总量（默认 1 GB），所以 zip 炸弹没法把磁盘写满。

```asuka check
import std/archive
import std/os

/// pack DIR OUT.zip：把 DIR 打包成归档。unpack ARCHIVE DIR：把归档解压出来。
async fn main() throws {
  let a = os.args()
  if a.count() == 3 && a[0] == "pack" {
    try await spawn { try archive.create(a[2], from: a[1]) }
  } else if a.count() == 3 && a[0] == "unpack" {
    let entries = try await spawn { try archive.list(a[1]) }
    print("{entries.count()} entries")
    try await spawn { try archive.extract(a[1], to: a[2], limit: 8 << 30) }
  } else {
    eprint("usage: pack DIR OUT.zip | unpack ARCHIVE DIR")
    os.exit(2)
  }
}
```

## 同时处理多项

`all(items, f, limit:)` 用每一项同时调用一个异步函数，并按各项的顺序给出结果：适合并行下载、发请求、同时运行其他程序。`limit:` 指定同一时刻最多运行几个；第一个出错的会取消其余的，等它们都停下后 `all` 再抛出这个错误（见[异步](language/async.md#异步)）。

```asuka check
import std/flag
import std/http
import std/os

/// Downloads pages, a few at a time.
struct Options {
  /// -j How many at once.
  jobs: int = 4
  /// The pages' URLs.
  urls: [string]
}

async fn main() throws {
  let o = flag.parse<Options>(os.args())
  let sizes = try await all(o.urls, u => try await http.get(u).body.count(), limit: o.jobs)
  for (u, n) in o.urls.zip(sizes) {
    print("{n}\t{u}")
  }
}
```

## 从大量数据中取最大的几个：std/collections

优先队列能在元素不断进出时，始终让下一个该处理的排在最前面：这就是 `std/collections` 的 `Heap`，按一个“小于”比较排序。要从大量数据中保留最大的 k 个，只需一个容量为 k、最小值最先出队的堆，完全不必对整个输入排序：每个元素只需一步 O(log k) 的操作（见[标准库](stdlib.md)）。

```asuka check
import std/collections.{Heap}
import std/os

// stdin 中出现次数最多的十个词，以及它们的次数。
fn main() {
  var counts: [string: int] = [:]
  for line in os.readAll().string().lines() {
    for w in line.lower().fields() { counts[w, default: 0] += 1 }
  }
  var top = Heap<(string, int)>(by: (a, b) => a.1 < b.1)
  for (w, n) in counts {
    if top.count() < 10 { top.push((w, n)) } else { let _ = top.pushPop((w, n)) }
  }
  for (w, n) in top.sorted().reversed() { print("{n}\t{w}") }
}
```

## 任意结构的 JSON

程序知道结构的 JSON，用结构体来读（`json.decode<T>`，见[数据](data.md)）。结构无法预知的，比如工具的配置文件或者从管道传进来的任意内容，就用 `json.Value`：`json.parse(text)` 原样读入任意 JSON，用 `get(key)`、`at(i)` 以及 `asString()`、`asInt()` ……查看里面的内容，也可以用 `match` 匹配它的各个 case（`.object(members)`、`.array(elements)`、`.string(s)` ……）。打印它或者用 `json.encode` 写出它，得到的都是 JSON。

```asuka check
import std/json
import std/os

/// 打印 stdin 中 JSON 在某个路径上的值：pick user.emails.0
async fn main() throws {
  var v = try json.parse(os.readAll().string())
  for step in (os.args().first() ?? "").split(".") {
    if step.isEmpty() { continue }
    let next = if let i = step.toInt() { v.at(i) } else { v.get(step) }
    v = next ?? json.Value.null
  }
  match v {
    .string(s) => print(s)       // 作为文本输出，不带引号
    _ => print(json.encode(v, indent: 2))
  }
}
```

## 配置文件和数据：TOML 与 CSV

`std/toml` 读取配置文件的方式和 `std/json` 读取 JSON 一样：`toml.decode<Config>(text)` 按字段名把配置读进结构体（写成 `min-qty` 或 `min_qty` 的键都会读进 `minQty`），文件里没有的可选字段为 `none`；`toml.encode(config)` 把它写回去，`toml.parse(text)` 则把任意文档读成 `json.Value`。和 JSON 一样，文件里没写的字段取空值（0、`""`、none），而不是结构体里的默认值：可能缺省的设置就声明为可选类型，或者先用默认值创建结构体，只在文件存在时才去读它。出错时会指出位置：`toml: 2:11: min-qty: expected an integer, not a string`。

`std/csv` 按行读取：`csv.parse(text)` 得到 `[[string]]`，`csv.decode<T>(text)` 按表头一行把每行读进结构体（单元格按字段的类型读取：int、float、bool、枚举的 case；可选字段遇到空单元格为 `none`），`csv.encode(rows)` 再连同表头一起写回去。文件太大、不便整个读进内存时，可以从它各行组成的流里一次读一行：`csv.stream<T>(fs.lines("sales.csv"))`，标准输入则用 `csv.stream<T>(os.lines())`。下面的例子读取从管道传进来的销售数据，并按 `~/.config/sales/config.toml` 的设置汇总：

```toml
currency = "USD"
min-qty = 2
```

```
item,qty,price
"Tea, green",2,3.5
Cake,1,4.25
```

```asuka check
import std/csv
import std/fs
import std/os
import std/toml

/// 汇总从管道传进来的 CSV 销售数据。
struct Config {
  currency: string = "EUR"
  /// 件数少于它的行不计入。
  minQty: int? = none
}

struct Sale {
  item: string
  qty: int
  price: float
}

async fn main() throws {
  let path = fs.join([fs.home(), ".config", "sales", "config.toml"])
  var config = Config()
  if fs.exists(path) {
    config = try toml.decode<Config>(try await spawn { try fs.readText(path) })
  }
  var total = 0.0
  for await sale in csv.stream<Sale>(os.lines()) {
    if sale.qty >= (config.minQty ?? 1) {
      total += sale.price * float(sale.qty)
    }
  }
  print("{total} {config.currency}")
}
```

`separator: "\t"` 读取用制表符分隔的数据；`comment: "#"` 跳过以它开头的行；`flexible: true` 允许各行的字段数不同。

## 用模板生成文本：std/template

`std/template` 用一个模板和一个值生成文本：配置文件、发布说明、邮件、代码，或者服务返回的页面。语法采用 Jinja 的写法（MiniJinja、Nunjucks、Twig 也是这样写的）：`{{ value }}`、写在 `|` 后面的过滤器、`{% if %}`、`{% for %}`、`{% include %}`、`{% extends %}` 和 `{% block %}`。数据是一个结构体或 map，按 `json.encode` 写出的样子读取，所以模板里用的名字就是它的字段。模板最好写在原始字符串里，这样能原样保留（在普通字符串里，`{` 会开始一个插值）：

```asuka check
import std/template

struct Change {
  kind: string
  text: string
  issue: int? = none
}

struct Release {
  version: string
  date: string
  changes: [Change]
}

fn main() throws {
  let notes = try template.parse(r"""
    # {{ version }} ({{ date }})

    {% for c in changes %}
    - **{{ c.kind | capitalize }}**: {{ c.text }}{% if c.issue %} (#{{ c.issue }}){% endif %}
    {% else %}
    Nothing changed.
    {% endfor %}
    """)
  let release = Release(version: "1.4.0", date: "2026-11-19", changes: [Change(kind: "added", text: "term.table", issue: 412), Change(kind: "fixed", text: "a crash on empty input")])
  print(try notes.render(release))
}
```

```text
# 1.4.0 (2026-11-19)

- **Added**: term.table (#412)
- **Fixed**: a crash on empty input
```

单独占一行的语句会连同这一行一起去掉，所以模板里写逻辑的地方不会在输出里留下空行；`{{-` 和 `-}}` 会去掉标签前面或后面的空白。数据里没有的字段是“未定义”：在 `if` 里当作 false，可以用 `| default("…")` 替换，打印出来则是错误，未知的过滤器也一样，错误信息会说明位置（`template: notes.md:4:9: c.title is undefined`）。模板只需解析一次，之后想渲染多少次都可以。

生成 HTML 时，输出会被转义：打印出来的 `&`、`<`、`>`、`"` 和 `'` 都写成字符引用，除非标记了 `| safe`；`| json` 输出的值可以直接放进 `<script>`。用 `template.parseAll` 一起读进来的模板（比如 `std/embed` 编译进来的文件）可以按名字互相 include 和 extend，名字以 `.html` 结尾的会自动转义：

```asuka
import std/http
import std/template

struct Note {
  id: int
  text: string
}

// pages：try template.parseAll(embed.texts("templates/*.html"))，即编译进来的文件
fn site(pages: template.Templates, notes: [Note]) -> http.Router = http.Router()
  .get("/notes", req => .html(try pages.render("templates/notes.html", ["notes": notes])))
```

## Web 服务：std/http

`http.serve` 能把程序变成一个 HTTP/1.1 服务器，用法和 Go 的 `net/http`、`Bun.serve` 类似：每个请求交给一个处理函数，它是一个返回 `Reply` 的 async 函数。`Router` 按方法和路径把请求分发给各个处理函数，`:name` 匹配路径中的一段。

```asuka check
import std/http
import std/os

struct Note {
  id: int
  text: string
}

struct Draft {
  text: string
}

async fn main() throws {
  var notes: [Note] = []
  let api = http.Router()
    .get("/notes", req => .json(notes))
    .get("/notes/:id", req => {
      if let n = notes.find(n => n.id == req.param("id").toInt()) {
        return .json(n)
      }
      return .text("no such note", status: 404)
    })
    .post("/notes", req => {
      let draft = try req.json<Draft>()
      let note = Note(id: notes.count() + 1, text: draft.text)
      notes.push(note)
      return .json(note, status: 201)
    })
  let port = (os.env("PORT") ?? "8080").toInt() ?? 8080
  let server = try await http.serve(port, api.handler(), host: "0.0.0.0")
  print("listening on {server.url}")
  // Ctrl-C 或终止信号：先把正在处理的请求答复完。
  for await _ in os.signals() {
    break
  }
  await server.close(grace: 10s)
}
```

```sh
$ curl -d '{"text":"milk"}' localhost:8080/notes
{"id":1,"text":"milk"}
$ curl localhost:8080/notes/1
{"id":1,"text":"milk"}
```

- `http.serve(port, handler, host:, limits:, onError:)` 在 `port` 上监听（传 0 时由系统挑一个端口，实际端口见 `server.port`），开始监听后返回 `Server`。`host` 默认是 `127.0.0.1`，只有本机的客户端能连上。`"0.0.0.0"` 接受所有 IPv4 网卡上的连接（容器、其他机器），`"::"` 还包括 IPv6。无法监听的地址（比如端口已被占用）会抛出 `ServerError.listen`。
- 每个请求的处理函数都在主线程上作为一个独立的 task 运行，所以它可以 await 任何 task 能等的东西（数据库、`http.get`、其他程序），其他请求同时照常处理。读取和解析请求、写出回复都在服务器自己的线程上完成；它能同时服务很多连接，请求之间连接保持打开。
- `Request` 有 `method`、`path`（按客户端发来的原样，保留转义）、`rawQuery` 和 `query(name)`、按小写名字存放的 `headers` 和不区分大小写的 `header(name)`、`body`（会先完整读入）、`text()` 和 `json<T>()`、客户端地址 `remote`，以及路由匹配到的 `params`（`param(name)`）。
- `Reply` 用 `.text`、`.json`、`.html`、`.data`、`.redirect` 创建，或者直接写 `Reply(status: 204)`；头部的值如果有多行，每行作为一个单独的头部发送（比如多个 Set-Cookie）。`.stream(body)` 边生成边分块发送响应体，`.events(stream)` 发送服务器发送事件，`http.events` 和浏览器的 EventSource 都能读；发送过程中客户端断开，处理函数会被取消。
- 路由的模式由普通文本、`:name`（一段）和 `*name`（路径的剩余部分）组成。路由按添加顺序依次尝试；没有路由匹配的路径返回 404，路径匹配但方法不对的返回 405（带 `Allow`），GET 路由也会响应 HEAD。
- 抛出错误的处理函数会得到 500 Internal Server Error，错误交给 `onError`（没传时写到 stderr）。
- `http.Limits(headerSize:, bodySize:, connections:, headerTimeout:, readTimeout:, idleTimeout:)` 规定服务器接受多少数据、等待多久：头部 64 KiB，请求体 16 MiB，10000 个连接，请求头部 30 秒内要发完，请求体每次等待最多 30 秒，两次请求之间最多空闲 60 秒。超出限制时会返回相应状态（431、413、408）并关闭连接。
- `server.close(grace: 10s)` 关闭服务器：立即停止接受新连接，让正在处理的请求完成回复，超过 `grace` 后取消仍在运行的处理函数（它们的 `defer` 会执行）。`server.wait()` 一直等到服务器关闭。服务器属于启动它的 task，`main` 返回时它也会关闭，所以 `main` 要 await `server.wait()`，或者像上面那样等待结束它的信号。没有流读取信号时，Ctrl-C 会结束 `main`，服务器也随之立即关闭：仍在运行的处理函数会被取消。读取 `os.signals()` 再关闭服务器，就能先让它们答复完（见[信号](#信号)）。

传入 `tls:` 就能直接提供 HTTPS：`net.serverTLS` 接收证书和对应的私钥，都是 PEM 格式（Let's Encrypt 和 `mkcert` 生成的就是这种），之后每个连接都使用 TLS 1.2 或 1.3 通信（见 [TLS](#tls)）。

```asuka check
import std/fs
import std/http
import std/net

async fn main() throws {
  let tls = try net.serverTLS(certificate: try fs.readText("cert.pem"), key: try fs.readText("key.pem"))
  let server = try await http.serve(8443, req => .text("hello over TLS"), host: "0.0.0.0", tls: tls)
  print("listening on {server.url}")
  await server.wait()
}
```

也可以把 TLS 交给反向代理（Caddy、nginx），证书由它自己续期：它通过 HTTP 把请求转给监听在 `127.0.0.1` 上的服务器，客户端地址放在 `X-Forwarded-For` 里。桌面应用也可以用同样的方式在一个 task 里提供本地 API，手机应用在运行期间也可以；网页不能监听（`ServerError.unsupported`）。

处理函数就是一个函数，所以测试可以直接用一个 `Request` 调用它或者路由，中间不经过 socket：`let r = try await api.handle(http.Request(method: "GET", path: "/notes/1"))`。

## 其他协议：std/net

HTTP 以外的协议交给 `std/net`：Redis、SMTP、游戏服务器，或者你自己设计的协议。它提供 TCP 连接和服务器、UDP 数据报以及 DNS 查询，和 Go 的 `net`、Rust 的 `std::net`、Node 的 `net` 与 `dgram` 类似。所有等待都是 async 的，socket 的读写在运行时自己的线程上完成。下面是一个按行收发的服务器，保存客户端设置的值：

```asuka check
import std/net
import std/os

async fn main() throws {
  var values: [string: string] = [:]
  let server = try await net.serve(7000, c => {
    for await line in c.lines() {
      let words = line.split(" ", count: 3)
      if words.count() == 3 && words[0] == "SET" {
        values[words[1]] = words[2]
        try await c.write("OK\r\n")
      } else if words.count() == 2 && words[0] == "GET" {
        try await c.write("{values[words[1]] ?? "-"}\r\n")
      } else {
        try await c.write("ERR unknown command\r\n")
      }
    }
  })
  print("listening on {server.address.string()}")
  for await _ in os.signals() {
    break
  }
  await server.close()
}
```

它的客户端：

```asuka check
import std/net

async fn main() throws {
  let c = try await net.connect("localhost", 7000, timeout: 5s)
  try await c.write("SET greeting hello\r\n")
  print(try await c.readLine())   // OK
  try await c.write("GET greeting\r\n")
  print(try await c.readLine())   // hello
  c.close()
}
```

- `net.connect(host, port, timeout:, noDelay:, keepAlive:)` 返回一个 `Conn`。主机名会先解析，再按 Happy Eyeballs 的方式尝试它的各个地址：IPv6 和 IPv4 的地址交替尝试，上一个 250 毫秒内没连上就开始下一个，保留最先连上的那个。`timeout` 同时限制解析和连接的时间。
- `Conn` 的读取方法有 `read(max:)`（已经到达的数据，连接结束时为 `none`）、`readExactly(n)`、`readLine(max:)`（不含 `\n` 或 `\r\n`）、`readUntil(delimiter, max:)` 和 `readAll(max:)`，也可以用流的方式读：`lines()` 和 `chunks()`；写入用 `write(text)` 和 `writeBytes(data)`。`closeWrite()` 关闭本端的写入（对端会读到结束，但仍然可以回复），`close()` 关闭整个连接。`readTimeout` 和 `writeTimeout` 限制每次等待的时间（超时抛出 `NetError.timedOut`）；`localAddress` 和 `remoteAddress` 是两端的地址。同一时间只能有一个读取在等待；写入会排队，每次都完整发送。
- `net.serve(port, handler, host:, onError:)` 用 `handler` 处理每个客户端的连接：每个连接一个独立的 task，可以同时处理很多个；处理函数返回时连接随之关闭。`net.listen(port, host:)` 返回一个 `Listener`，可以自己调用 `accept()`。`host` 默认是 `127.0.0.1`；`"0.0.0.0"` 接受所有 IPv4 网卡上的客户端，`""` 或 `"::"` 接受所有网卡上的。端口传 0 时由系统挑选（见 `server.port`）。`server.close(grace:)` 停止接受连接，超过 `grace`（默认不等待）后取消仍在运行的处理函数；`server.wait()` 一直等到服务器停止。
- `net.udp(port:, host:, reuse:)` 返回一个 `UDPSocket`：`send(data, to:)`、`sendTo(data, host:, port:)`、`receive(timeout:)`（得到一个 `Datagram`，包含 `data` 和发送方 `from`）、`datagrams()`、`connect(host, port)`，以及用于组播的 `joinGroup(ip)` 和 `setBroadcast(on)`。
- `net.lookup(host)` 按系统的方式（hosts 文件、DNS）解析主机名的 IP 地址；还有 `reverseLookup(ip)`、`lookupCNAME`，以及查询 DNS 记录的 `lookupMX`、`lookupTXT`、`lookupSRV`、`lookupNS`。
- `net.IP` 是一个地址（`IP.parse("2001:db8::1")`、`isLoopback()`、`isPrivate()`、`string()`），`net.Prefix` 是一个网段（`Prefix.parse("10.0.0.0/8")!.contains(ip)`），`net.Address` 是 IP 加端口；`joinHostPort` 和 `splitHostPort` 会处理 IPv6 的方括号（`"[::1]:80"`）。
- 取消正在等待的 task（输掉了一次 `race`、`withTimeout` 到时、它的 owner 被销毁）会停止这次等待，而且此前已经到达的数据不会丢失：下一次读取会拿到它们。已经开始的写入会继续写完。socket 属于创建它的 task：那个 task 结束时 socket 随之关闭。
- 失败时抛出 `NetError`：`refused`、`reset`、`timedOut`、`unreachable`、`notFound`、`lookupFailed`、`closed`、`ended`、`addressInUse`、`addressUnavailable`、`denied`、`tooLarge`、`invalid`、`unsupported`、`failed`。

桌面应用和手机应用（运行期间）可以像程序一样使用 socket；网页没有 socket（`NetError.unsupported`），不过 IP 地址和 host:port 相关的辅助函数照样可用。

### TLS

`net.connectTLS` 先建立连接，再在这个连接上使用 TLS 1.2 或 1.3 通信，和 Go 的 `tls.Dial`、Node 的 `tls.connect` 一样。在你发送任何数据之前，它会先按主机名检查服务器的证书；之后的读写和上面的 `Conn` 完全相同。

```asuka check
import std/net

async fn main() throws {
  let c = try await net.connectTLS("example.com", 443, timeout: 10s)
  try await c.write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
  print(try await c.readLine())   // HTTP/1.1 200 OK
  c.close()
}
```

有些协议先用明文通信、之后再升级（SMTP 和 IMAP 的 STARTTLS、PostgreSQL 的 SSLRequest），这时在原来的连接上调用 `startTLS` 即可：

```asuka check
import std/net

async fn main() throws {
  let c = try await net.connect("mail.example.com", 587)
  let _ = try await c.readLine()   // 220 mail.example.com ESMTP
  try await c.write("STARTTLS\r\n")
  let _ = try await c.readLine()   // 220 Ready to start TLS
  let state = try await c.startTLS("mail.example.com")
  print("{state.version} {state.cipherSuite}")
  try await c.write("EHLO me.example.com\r\n")
}
```

- 服务器的证书会按系统信任的证书颁发机构来检查。在 macOS、iOS 和 Windows 上由系统自己检查，和 Safari、Edge 一样：会遵循用户和管理员信任或不信任的设置，自动获取缺失的中间证书，并检查吊销状态。在 Linux 上，会像 Go 一样读取系统的 CA 证书包：优先用 `SSL_CERT_FILE`，否则依次查找 `/etc/ssl/certs/ca-certificates.crt`（Debian、Ubuntu）、`/etc/pki/tls/certs/ca-bundle.crt`（Fedora、RHEL）、`/etc/ssl/ca-bundle.pem`（openSUSE）、`/etc/ssl/cert.pem`（Alpine）等位置，然后再读 `SSL_CERT_DIR` 里的文件。静态构建读的也是这些位置，所以容器镜像里要装上 `ca-certificates` 包。在 Android 上，用的是系统的证书存储。
- `net.clientTLS(ca:, certificate:, key:, alpn:, minVersion:, maxVersion:, insecureSkipVerify:, sessionCache:)` 创建客户端配置，`connectTLS` 和 `startTLS` 通过 `tls:` 接收它。`ca`（PEM）会代替系统的证书颁发机构被信任，比如私有 CA，或者自签名的服务器。`certificate` 和 `key` 是本端自己的证书和私钥，用于要求客户端出示证书的服务器（双向 TLS）。`alpn` 是本端提供的应用层协议（`["h2", "http/1.1"]`）。`insecureSkipVerify` 什么都不检查，谁都能冒充服务器，所以只适合测试。
- `net.serverTLS(certificate:, key:, others:, alpn:, clientCA:, requireClientCertificate:, sessionTickets:)` 创建服务器配置：`net.listen`、`net.serve` 和 `http.serve` 通过 `tls:` 接收它，`conn.startServerTLS(tls)` 则在原来的连接上升级。`others` 是更多的证书（`KeyPair`）：客户端请求的名字（SNI）和哪张证书的名字匹配，就出示哪张。`clientCA` 要求每个客户端出示由其中某个机构签发的证书。
- `key` 是 PEM 格式：PKCS#8、PKCS#1 或 SEC 1。加密过的私钥或 OpenSSH 格式的私钥交给 std/crypto 来读，再把它的 `pem()` 交给 TLS：`key: (try await crypto.decryptPrivateKey(text, password: password)).pem()`。（这样，两者都不读的 TLS 应用就不会带上它们的代码。）
- 客户端会像浏览器一样恢复会话：配置会按服务器的名字和端口记下每次握手协商出的结果（TLS 1.3 的 ticket、TLS 1.2 的会话），下次再连到那里，握手就更短，服务器也不用再发一遍证书（之前检查过的那张证书仍会再检查名字和有效期）。`sessionCache:` 是最多为多少个服务器保留会话（默认 64；最久没用的先被丢掉；`0` 表示一个都不保留）。服务器会给客户端发 ticket，用它在内存里随机生成的密钥加密；这些密钥从不写到任何地方，并且每天换新。`sessionTickets: false` 表示不发 ticket。不提前发送数据（TLS 1.3 的 0-RTT）：握手结束前发出的数据，任何看到它的人都能重放。
- `conn.handshake()` 返回握手协商出的结果，即一个 `TLSState`：`version`、`cipherSuite`、ALPN 选定的 `protocol`、`serverName`、是否恢复了会话 `resumed`，以及对端的证书 `peerCertificates`（DER）。TLS 服务器上的连接会在第一次读或写需要等待时完成握手；客户端握手失败时，这次读或写会抛出错误。
- 证书不受信任时抛出 `NetError.untrustedCertificate(problem, reason)`：`problem` 是 `expired`、`notYetValid`、`wrongHost`、`unknownIssuer`、`revoked`、`missing`、`invalid` 之一，`reason` 说明具体发现了什么（"example.com's certificate expired on 2025-03-01"）。其他失败都是 `NetError.tls(reason)`：双方没有共同支持的版本、密码套件或 ALPN 协议，对端发来了警报（alert），或者服务器根本不说 TLS。

TLS 在所有平台上都是同一份实现（Mbed TLS），从不调用它的应用不会包含它。网页没有可供它使用的 socket：在网页上，`std/http` 和 `std/websocket` 照样能使用 HTTPS 和 `wss:`，其他平台也一样，都是通过各平台自己的客户端。

### 私有证书颁发机构与客户端证书

`std/http` 的请求和 `std/websocket` 的连接都通过各平台自己的客户端发出，像系统一样检查服务器的证书。如果系统不信任这台服务器（比如证书由公司内部 CA 签发的内部服务，或者测试用的服务器），或者服务器要求客户端出示证书，就给请求单独配上 TLS 设置：`http.TLS`，通过 `tls:` 传给 `http.get`、`post`、`request`、`download`、`stream`、`events` 和 `websocket.connect`。

```asuka check
import std/fs
import std/http

async fn main() throws {
  let tls = http.TLS(ca: try fs.readText("ca.pem"), certificate: try fs.readText("client.pem"), key: try fs.readText("client.key"))
  let r = try await http.get("https://orders.internal/health", tls: tls)
  print(r.status)
}
```

- `ca`（PEM）会代替系统的证书颁发机构被信任；加上 `trustSystem: true` 则两者都信任。无论哪种情况，都会检查服务器的名字。
- `certificate`（PEM：客户端自己的证书，后面跟着签发它的各级机构的证书）和 `key`（PEM：PKCS#8、RSA 或 EC 格式，不能加密）会出示给要求客户端证书的服务器（双向 TLS）。
- `insecureSkipVerify` 完全不检查证书，谁都能冒充服务器，所以只适合测试你自己的服务器。
- 设置相同的请求共用连接；设置不同或没有设置的请求，绝不会用到这些连接。
- 读不了的设置会抛出 `HTTPError.tls(reason)`。设置不信任的服务器会让请求失败，并说明原因（"orders.internal's certificate isn't trusted: ..."）。
- 在 iOS 和 macOS 上，即使 `ca` 信任了签发机构，服务器证书也必须满足 Apple 的要求：有效期不超过 825 天，服务器的名字写在 subjectAltName 里，用 SHA-256 或更强的算法签名，RSA 密钥至少 2048 位；如果有 extendedKeyUsage，必须包含服务器用途。
- 在 Windows 上，客户端证书的私钥在程序运行期间存放在系统的密钥存储里，程序退出时删除；证书和根证书之间的各级机构证书会加入当前用户的中间证书颁发机构存储，因为 Windows 要从那里找出它发送的证书链。
- 网页做不到：信任谁、出示哪张证书都由浏览器决定，所以在网页上带 `tls:` 的请求会抛出错误。

## 密钥、密码与签名：std/crypto

std/crypto 在所有平台上都是同一份 C 代码，Web 也一样：一个平台加密、哈希或签名的数据，其他任何平台都能解开和验证。服务验证 webhook 的 HMAC 时用 `crypto.equal` 比较，它的耗时与字节在哪里不同无关；保存密码用 `hashPassword`（argon2id，输出其他系统也能读的 PHC 格式），它会在另一个线程上运行：

```asuka check
import std/crypto
import std/encoding
import std/http
import std/os

struct Login {
  name: string
  password: string
}

async fn main() throws {
  let secret = (os.env("WEBHOOK_SECRET") ?? "").bytes()
  var users: [string: string] = [:]  // 真实的服务里放在数据库中
  let api = http.Router()
    .post("/hooks/github", req => {
      // GitHub 会对请求体签名：X-Hub-Signature-256 是 sha256=<十六进制的 HMAC>。
      let sent = (req.header("X-Hub-Signature-256") ?? "").trimPrefix("sha256=")
      let mac = encoding.hexEncode(crypto.hmacSHA256(secret, req.body))
      if !crypto.equal(mac.bytes(), sent.bytes()) {
        return .text("bad signature", status: 401)
      }
      return .text("ok")
    })
    .post("/signup", req => {
      let login = try req.json<Login>()
      users[login.name] = try await crypto.hashPassword(login.password)
      return .text("welcome", status: 201)
    })
    .post("/login", req => {
      let login = try req.json<Login>()
      let stored = users[login.name] ?? ""
      if stored != "" && (try await crypto.verifyPassword(login.password, hash: stored)) {
        return .text("hello, {login.name}")
      }
      return .text("wrong name or password", status: 401)
    })
  let server = try await http.serve(8080, api.handler())
  for await _ in os.signals() {
    break
  }
  await server.close(grace: 10s)
}
```

用 RS256、PS256、ES256 或 EdDSA 签名的 JWT，用签发方的公钥验证，公钥可以来自 PEM 或它的 JWK Set。能用哪些签名方案验证由密钥的类型决定，所以令牌里的 `alg` 无法换成更弱的方案：

```asuka check
import std/crypto
import std/encoding
import std/http
import std/json

struct Header {
  alg: string
  kid: string = ""
}

/// token 这个 JWT 是否由签发方的某个密钥签名。
async fn verified(token: string, jwksURL: string) throws -> bool {
  let keys = try crypto.publicKeys((try await http.get(jwksURL)).text())
  let parts = token.split(".")
  if parts.count() != 3 {
    return false
  }
  let header = try json.decode<Header>((try encoding.base64Decode(parts[0])).string())
  if let key = keys[header.kid], let scheme = crypto.SignatureScheme.jws(header.alg) {
    return try crypto.verify(key, "{parts[0]}.{parts[1]}".bytes(), try encoding.base64Decode(parts[2]), scheme: scheme)
  }
  return false
}
```

- `crypto.seal(key, plaintext, additionalData:)` 用 XChaCha20-Poly1305 和一个随机 nonce 加密并认证，nonce 放在结果最前面；`open` 还原明文，数据有任何改动时抛出 `CryptoError.notAuthentic`。需要和其他系统互通时用 `cipher: .aes256GCM`（或 `.aes128GCM`、`.chaCha20Poly1305`）；协议自己规定 nonce 时用 `sealWithNonce` 和 `openWithNonce`。
- `hkdf` 从密钥材料派生密钥，`pbkdf2` 和 `argon2id` 从密码派生；`hmac`、`hmacSHA256` 和 `hmacSHA512` 用共享密钥签名。
- `hashPasswordBcrypt` 和 `verifyPassword` 也能读 bcrypt 的 `$2b$`、`$2a$` 和 `$2y$`，适合由其他系统填好的数据库。
- `newPrivateKey()` 生成 ECDSA P-256 密钥（`kind: .ed25519`、P-384、P-521 也可以）；`sign` 和 `verify` 接受从 PEM（`publicKey`、`privateKey`）、DER 或 JWK 读入的 RSA（PKCS#1 v1.5 和 PSS）、ECDSA 和 Ed25519 密钥，并能用 `pem()` 和 `jwk()` 再导出。
- `privateKey` 也能读 OpenSSH 的私钥（`ssh-keygen` 生成的）。加密的私钥用 `decryptPrivateKey(pem, password:)` 读：OpenSSL 的 `ENCRYPTED PRIVATE KEY`（PBES2，配合 PBKDF2 或 scrypt，以及 AES 或 3DES；还有旧版 OpenSSL 和 Java 用的 PKCS#12 3DES）、OpenSSL 旧式的 `Proc-Type: 4,ENCRYPTED` 私钥，以及 OpenSSH 加密的私钥（bcrypt 配合 AES 或 ChaCha20-Poly1305）。密码解不开私钥时抛出 `CryptoError.wrongPassword`；单 DES、RC2 和 RC4 太弱，不予读取。`key.encryptedPEM(password:)` 按 OpenSSL 3 的方式写出加密的私钥（PBKDF2-HMAC-SHA256 迭代 600,000 次，AES-256-CBC）。这两者都有意放慢，这样猜密码也同样慢，它们都在另一个线程上运行。不读加密私钥的应用不会带上这些加密算法的代码。

```asuka check
import std/crypto
import std/fs
import std/os

async fn main() throws {
  let text = try fs.readText("deploy_key")  // ssh-keygen -t ed25519 -f deploy_key
  let key = try await crypto.decryptPrivateKey(text, password: os.env("KEY_PASSWORD") ?? "")
  let signature = try crypto.sign(key, "release 1.4.0".bytes(), scheme: .ed25519)
  print(signature.count())  // 64
  // -----BEGIN ENCRYPTED PRIVATE KEY-----，OpenSSL 等工具都能读
  try fs.writeText("deploy_key.pem", await key.encryptedPEM(password: os.env("KEY_PASSWORD") ?? ""))
}
```
- `md5` 只用于协议要求的校验和，不用于安全；CRC 在 std/compress 里：`crc32` 和 `crc32c`。

## 运行

`asuka run` 直接运行程序的 `main`，不需要等待构建：`--` 之后的参数都交给程序，退出时带上程序的退出码。

```sh
asuka run -- --jobs 8 a.txt b.txt
asuka run --bin notes -- add "Buy milk"   # asuka.toml 里 [bin] 中的一个程序
```

`asuka dev` 运行程序，并在你每次保存后重新运行（就像 `cargo watch -x run`）：保存时如果上一次还没结束，会先停掉它。对于没有 `fn main` 的文件或包，`asuka run` 还是和以前一样，运行它的测试块并显示它的应用画面。

## 构建

`asuka build` 把程序编译成本机的可执行文件，放在 `dist/` 里：

```sh
asuka build                          # dist/greet
asuka build --target linux-amd64     # dist/linux-amd64/greet
asuka build --target windows-arm64   # dist/windows-arm64/greet.exe
```

`--target` 由 `darwin`、`linux` 或 `windows` 加上 `amd64` 或 `arm64` 组成；本机以外的系统和桌面应用一样，用 Zig 构建。可执行文件在系统允许的范围内是独立的：在 Linux 上它是静态链接的（musl，需要 Zig），除非它用到的东西要加载系统的库（std/http 的 libsoup）；在 Windows 上只依赖系统自带的 DLL；在 macOS 上只依赖系统的库。

带平台代码的包在每个桌面系统上的程序里都能用：`std/http` 在 macOS 上用 URLSession、在 Windows 上用 WinHTTP、在 Linux 上用 libsoup 发请求，和在应用里一样。只是没有窗口。SQLite（`std/sqlite`）和在应用里一样编译进可执行文件。

### 应用自带的程序

应用可以自带程序，就像 VS Code 自带 `code` 命令：在终端里操作应用数据的命令。一个包不能同时有 `app { }` 和 `fn main()`，所以每个程序放在自己的目录里，并在 asuka.toml 中声明：

```toml
[bin.notes]
path = "cli"
```

程序和其他包一样，用相对路径导入项目里的包（`import ../model`）。`asuka run --bin notes` 运行它（只有一个程序时可以省略 `--bin`），`asuka test` 会把各个程序的测试和应用的测试一起运行，`asuka build --bin notes` 单独构建它。应用自带的程序把数据保存在应用保存的地方（std/fs 的 `appDir`，以及其中的 SQLite 数据库），所以两者读写的是同一份数据；开发时，`asuka run --bin` 使用 `asuka dev` 的那份。

桌面应用的构建会把程序放进应用里：

| 构建 | 位置 | 加到用户的 PATH |
|---|---|---|
| macOS | `Notes.app/Contents/Helpers/notes`，与应用的架构相同，和应用一起签名 | 由用户自己（或应用提供选项）创建链接：`ln -s /Applications/Notes.app/Contents/Helpers/notes /usr/local/bin/notes` |
| Linux | 安装前缀的 `bin/notes`，和应用的可执行文件在一起，tar 包和 Flatpak 里都有 | 随应用安装到 `~/.local/bin` 或 `/usr/bin`；Flatpak 中用 `flatpak run --command=notes <id>` 运行 |
| Windows | 应用 `.exe` 旁边的 `notes.exe` | 由安装程序把应用的文件夹加入 `PATH` |

手机和 Web 会忽略 `[bin]`。[examples/notes](../../examples/notes) 就是一个自带 `notes` 命令的应用，这个命令可以列出、添加和勾选应用里的笔记。

### 由其他工具链构建的程序

由其他工具链构建的程序（用 Go 写的服务、用 Rust 写的工具，由应用启动）也以同样的方式放进应用。它的 `[bin]` 写的不是目录，而是构建它的命令，应用为哪个目标构建，这个命令就针对那个目标运行。占位符用各个工具链自己的名字表示目标：Go 用 `{goos}` 和 `{goarch}`，Rust 用目标三元组 `{target}`；`{os}` 和 `{arch}` 是 Asuka 自己的名字（`macos`、`x64`），供你自己的脚本使用。不能写到 `{out}` 的构建（比如 cargo）用 `output` 说明程序留在哪里（完整列表见 [asuka.toml](asuka-toml.md#bin)）：

```toml
[bin.notes-server]
build = ["go", "build", "-o", "{out}", "./cmd/server"]
env = { GOOS = "{goos}", GOARCH = "{goarch}", CGO_ENABLED = "0" }

[bin.notes-index]
build = ["cargo", "build", "--release", "--manifest-path", "index/Cargo.toml", "--target", "{target}"]
output = "index/target/{target}/release/notes-index{exe}"
```

在目标系统上构建时，`{target}` 是该系统自己的三元组（Windows 上是 `x86_64-pc-windows-msvc`）；交叉构建时，是不需要那个系统的 SDK 就能构建的三元组（`x86_64-pc-windows-gnu`，即 MinGW；`x86_64-unknown-linux-musl`，静态链接）。Rust 的目标用 `rustup target add` 安装，交叉构建还需要对应的链接器。

如果占位符给出的参数不是你想要的，`build` 可以运行你自己的脚本，由脚本在运行时决定参数：目标在它的环境变量里（`ASUKA_OS`、`ASUKA_ARCH`、`ASUKA_EXE`、`ASUKA_TARGET`、`ASUKA_GOOS`、`ASUKA_GOARCH`），程序写到 `ASUKA_OUT`。下面这个脚本在任何地方都构建静态链接的 Linux 程序，并为 Mac 开启一个 feature：

```toml
[bin.notes-index]
build = ["sh", "index/build.sh"]
```

```sh
#!/bin/sh
# index/build.sh: 在项目目录中运行，每个目标一次。
set -e
target=$ASUKA_TARGET
features=
case $ASUKA_OS in
  linux)
    case $ASUKA_ARCH in
      x64) target=x86_64-unknown-linux-musl ;;
      arm64) target=aarch64-unknown-linux-musl ;;
    esac ;;
  macos) features="--features metal" ;;
esac
cargo build --release --manifest-path index/Cargo.toml --target "$target" $features
cp "index/target/$target/release/notes-index$ASUKA_EXE" "$ASUKA_OUT"
```

脚本在构建应用的机器上运行，而不是在应用运行的机器上：在 Mac 上构建 Windows 版时，用的是 Mac 的 `sh`。要在 Windows 上构建，就写 Windows 能运行的东西：`["pwsh", "-File", "index/build.ps1"]`（输出路径是 `$env:ASUKA_OUT`），或者任何语言写的程序，比如 `["go", "run", "./tools/build-index"]`。

应用用 `command.bundled(name)` 找到自己的程序：构建里的路径，没有时（手机、Web、测试）为 none。`asuka dev` 启动时会构建它们，应用也用同样的方式找到：

```asuka
import std/command

async fn startServer() throws -> command.Process {
  let server = command.bundled("notes-server") ?? "notes-server" // 否则用 PATH 上的
  return try command.start(server, ["serve"])
}
```

## 测试

和应用一样，程序的测试块用 `asuka test` 运行。`main` 是一个函数：测试里用 `os.fake` 像真正的进程那样运行它，`os.fake` 提供参数、环境变量和 stdin，并保存它写出的内容：

```asuka
import std/os

fn main() {
  let name = os.args().first() ?? "world"
  print("Hello, {name}!")
}

test "greets whom it's given" {
  let o = os.fake(args: ["Ada"], env: ["LANG": "en"], stdin: "")
  expect(await o.run(main) == 0)
  expect(o.stdout() == "Hello, Ada!\n")
}
```

`o.run(main)` 返回程序本该结束时的退出码：0；抛出错误时为 1（消息在 `o.stderr()` 里）；或者 `os.exit` 给出的值，这时 `os.exit` 只结束测试里的 `main`，而不是整个进程。`o.signal(.interrupt)` 像按下 Ctrl-C 那样，给它正在运行的 `main` 发送信号：如果有 `os.signals` 流在读，流会收到它；否则 `main` 会被取消，`run` 返回 130。没有 fake 时，测试里的 `os.args()` 和 stdin 都是空的。`command.fake()` 替其他程序作答，这样它们就不会真的运行：

```asuka
import std/command

test "reads git's branch" {
  let git = command.fake()
  git.answer("git", ["branch", "--show-current"], stdout: "main\n")
  let out = try await command.run("git", ["branch", "--show-current"])
  expect(out.text() == "main\n")
  expect(git.calls == [["git", "branch", "--show-current"]])
}
```

属性测试、快照、基准测试和覆盖率在命令行程序里的用法和应用一样：`asuka test --bench` 测量的是 `asuka build` 发布出去的、编译成 C 的代码，`asuka test --cover` 统计的是程序自己的代码（见[测试](testing.md#属性测试)）。

## 各平台支持情况

| | macOS | Windows | Linux | 手机、Web |
|---|---|---|---|---|
| 程序（`asuka build`） | 支持 | 支持 | 支持 | 不支持 |
| `os.args`、`os.exit`、stdin | 支持 | 支持 | 支持 | 应用里为空，`exit` 会抛出错误 |
| `std/command` | 支持 | 支持 | 支持 | `unsupported` |
| `std/flag` | 支持 | 支持 | 支持 | 支持（它能解析任何列表） |
| `std/term` | 支持 | 支持 | 支持 | 没有终端：没有样式，`prompt` 返回 `none` |
| `std/log` | stderr（应用：统一日志系统） | stderr（应用：调试器的输出） | stderr | 统一日志系统、logcat、浏览器控制台 |
| `http.serve` | 支持 | 支持 | 支持 | 手机应用：运行期间可用；Web：`unsupported` |
| `std/net` | 支持 | 支持 | 支持 | 手机应用：运行期间可用；Web：`unsupported` |
