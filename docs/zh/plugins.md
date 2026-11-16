# 插件

插件是一个自带原生库的包。这些库用 C、C++、Zig 或 Rust 编写，为每个平台预先构建好并发布一次，凡是用到这个包的应用都会把它们一起打包。包的 `asuka.toml` 旁边有一份清单 `asuka-plugin.toml`，写明每个库在每个平台上对应的文件及其 SHA-256。`std/sqlite` 就是一个插件（见[数据](data.md#记录stdsqlite)）：SQLite 用同一套编译选项为所有平台构建一次。

## 插件、原生包与 C

要引入不是用 Asuka 写的代码，Asuka 有三种方式。一个包可以同时用上其中几种。

| | 是什么 | 何时构建 | 适合用来 |
|---|---|---|---|
| [用 `extern` 调用 C](c.md) | `asuka.toml` 的 `[c]` 列出的 C 源码，Asuka 通过 `extern fn` 调用 | 和应用一起，由命令行工具的编译器为每个目标构建 | 你自己的 C 代码、小型库的源码、希望直接编进应用的 C |
| [原生包](native.md) | `native interface` 或 `native component`，用 Swift、Kotlin、JavaScript 和 C 实现 | 和应用一起，从各平台自己的工程构建（Swift 包、Gradle 模块、`web/`、`desktop/`） | 平台自己的 API 和视图 |
| 插件 | 按平台预先构建的库，列在 `asuka-plugin.toml` 里 | 由插件自己的构建流程构建一次，然后发布 | 大型库、需要自己一套工具链的库（Rust、需要 configure 的项目）、必须在所有平台上是同一份构建的库 |

插件的库不会编译进应用：每个库都是独立的库文件（`.dylib`、`.so`、`.dll`、iOS framework、WebAssembly 模块），在运行时由包的代码加载。构建应用时从不构建它们，所以一个用了 SQLite 的应用并不需要编译 SQLite。

## 使用插件

导入它的包：

```asuka
import std/sqlite

fn openNotes() throws -> sqlite.DB = try sqlite.open("notes.db")
```

这样就够了。`asuka build` 会在应用用到的包里（应用自己的目录、它的依赖、它导入的标准库和平台 SDK 的包）找出插件，为构建的目标平台打包每个库的文件。`asuka dev`、`asuka test` 和 `asuka run` 会优先使用插件自己的构建产物（插件是一份带构建产物的检出时），否则使用发布的文件：第一次用到时下载到用户的缓存里（系统缓存目录下的 `asuka/plugins`），之后直接从那里使用。每个下载的文件在使用前都会和清单里写的 SHA-256 核对。从包仓库下载的包本身不带这些库，它的清单写明了去哪里取。

### 库放在哪里

| 平台 | 位置 | |
|---|---|---|
| macOS | `Contents/Frameworks` | 与应用一起签名 |
| Windows | `.exe` 旁边 | 与它一起签名（`ASUKA_WINDOWS_CERT`） |
| Linux | 安装前缀的 `lib/<app>` | 包含在 tar 包和 Flatpak 里 |
| iOS | `Frameworks/<library>.framework` | 带有自己的 Info.plist 和隐私清单，与应用一起签名 |
| Android | 每个 ABI 里 `libasuka.so` 的旁边 | |
| Web | 宿主文件的 `lib/` | 页面的 `libs` 选项给出每个文件的 URL |

一个应用所有插件的库都放在同一个地方，所以对同一个平台，任意两个插件都不能有同名的文件；出现这种情况时，构建会指出是哪两个插件。

开发时，`asuka dev` 会把它们放到开发用的应用找得到的地方：在桌面上，它让核心直接使用每个文件（`ASUKA_LIB_<NAME>`，即库名的大写形式，如 `ASUKA_LIB_ASQLITE`）；在 Android 上，它把这些库推送到开发用应用的核心旁边；在 iOS 上，它们是开发用应用里的 framework；在 Web 上，它们由页面的 `asuka/lib/` 提供。`asuka test` 和 `asuka run` 同样会设置 `ASUKA_LIB_<NAME>`。

嵌入到另一个应用里的应用（`asuka build --platform ios --xcframework`）会为每个库附带一个 XCFramework，宿主应用也要一并嵌入。

## 编写插件

插件就是一个包（见[包](packages.md)），它的 `asuka.toml` 旁边有一份 `asuka-plugin.toml`，还有一套构建它的库的流程，以及加载这些库的代码。

### 清单

```toml
name = "sqlite"
sources = ["csrc"]

[[libraries]]
name = "asqlite"
version = "3.54.1"
release = "https://github.com/you/sqlite-plugin/releases/download/plugin-sqlite"

[libraries.files]
darwin = {name = "libasqlite.dylib"}
ios = {name = "asqlite"}
ios-simulator = {name = "asqlite"}
android-arm64 = {name = "libasqlite.so"}
android-arm = {name = "libasqlite.so"}
android-amd64 = {name = "libasqlite.so"}
linux-amd64 = {name = "libasqlite.so"}
linux-arm64 = {name = "libasqlite.so"}
windows-amd64 = {name = "asqlite.dll"}
windows-arm64 = {name = "asqlite.dll"}
wasm = {name = "asqlite.wasm"}
```

- `name` 是插件的名字，命令行工具的提示信息和它的 release 都用这个名字。它只能包含字母、数字、`-`、`_` 和 `.`，且不能以后三者开头。
- `sources` 是构建这些库所用的源码，路径相对于清单（目录表示其中的全部文件）。如果某个库是用旧版本的源码构建的，它就不会被发布。不写时，包里除 `build/`、清单、`testdata` 和以点开头的文件之外的所有文件都算作源码。
- 每个库的 `name` 是代码请求它时用的名字（`ASUKA_LIB_ASQLITE`），也是它在 iOS 上的 framework 名。`version` 是库的版本，写进那个 framework 的 Info.plist。`release` 是这些文件要发布到的 GitHub release：`https://github.com/<owner>/<repo>/releases/download/<tag>`。
- `files` 写明它在每个平台上的文件：`darwin`（一个通用文件，同时适用于 arm64 和 x86_64 的 Mac）、`ios` 和 `ios-simulator`、`android-arm64`、`android-arm`、`android-amd64`、`linux-amd64`、`linux-arm64`、`windows-amd64`、`windows-arm64` 以及 `wasm`。为一个库没有对应文件的平台构建时会失败，并说明原因。Mac 上的文件是 `.dylib`；iOS 上的文件以库名命名（它是 framework 的二进制文件，install name 为 `@rpath/<library>.framework/<library>`）。

`asuka plugin publish` 会把发布的信息（`asset`、`size`、`sha256`、`rawSize`、`rawSHA256`、`source`）补进每个文件的条目，不要自己填写这些字段。

### 构建

插件用自己的工具来构建它的库，怎么构建由库的需要决定：sqlite 插件用 Xcode 为 Mac 和 iOS、用 Android NDK 为 Android、用 Zig 为 Windows、Linux 和 Web 编译 SQLite 及其入口。一个 shell 脚本、一个 Makefile 或一个程序都可以。对每个目标，它把库写到清单旁边的 `build/<target>/<file>`，并在同一位置写一个 `<file>.source`，里面是构建所用源码的哈希：按路径排序（路径相对于清单，用 `/` 分隔）依次取 `sources` 里的每个文件，先写入“路径、一个空格、以字节计的大小、一个换行”，再写入文件内容，对全部内容求 SHA-256。

`build/` 不提交进仓库，包的 tar 包也从不包含它。在检出的仓库里，`asuka dev`、`asuka test` 和 `asuka run` 会先用那里的构建产物，再考虑发布的文件，所以插件自己的测试和它旁边的应用运行的都是你刚构建出来的库。

### 加载库

包的代码用平台的加载器（`dlopen`、`LoadLibrary`、WebAssembly 的实例化）加载库，再查找其中的函数，所以应用在链接时不依赖它。查找顺序如下：

1. `ASUKA_LIB_<NAME>` 给出的文件路径，`asuka dev`、`asuka test` 和 `asuka run` 会设置它。
2. `asuka build` 放置它的位置，从可执行文件所在目录算起：Mac 上是 `../Frameworks/<file>`（从 `Contents/MacOS` 算起），Windows 上是 `.exe` 所在目录，Linux 上是 `../lib/<exe>/<file>`（从 `bin` 算起），iOS 上是 `Frameworks/<library>.framework/<library>`（从应用扩展里则是 `../../Frameworks/…`）。
3. 按名字交给系统的加载器去找：Android 上是 `lib<library>.so`（APK 的 `lib/<abi>`），iOS 上是 `@rpath/<library>.framework/<library>`。

在 Web 上，页面的 `libs` 选项把每个文件名对应到它的 URL。

`std/sqlite` 在第一次打开数据库时加载 `asqlite`，并在主线程之外运行它；在 Web 上，由 SQLite 的 worker 实例化 `asqlite.wasm`。你自己的包则在它的[原生代码](native.md)里加载自己的库：桌面上的 C、Swift 和 Kotlin，以及 Web 上的 JavaScript。

库导出的符号要尽量少，只导出它自己的，不导出它链接进来的东西：进程里可能还有另一份同源的代码（比如 GTK 加载的系统 SQLite）。调用时一次完成一整个操作，而不是每个值调用一个函数：sqlite 插件只有一个入口 `asq_call`，它执行一整条语句，把结果行放在一个缓冲区里返回；正因如此，Web 上才能把它放到 worker 里运行。

### 发布

```sh
asuka plugin publish path/to/plugin     # 包含 asuka-plugin.toml 的目录（默认：.）
```

`asuka plugin publish` 会把每个库的构建产物用 gzip 压缩，并以其哈希命名（`asqlite-darwin-1982f596dd7c-libasqlite.dylib.gz`）。它用 `gh` 上传 release 里还没有的文件，release 不存在时会先创建。之后它把每个文件下载回来核对，再记录到清单里——你要提交的就是这份清单。它会重写整份清单，并在开头加一段注释说明这一点，所以你在清单里写的注释不会保留。release 里已有的文件永远不会被替换，因此任何一次提交里的清单都始终可用。如果构建产物比插件的源码旧，发布会中止：先重新构建。

然后像发布其他包一样发布这个包（`asuka pkg publish`，见[包](packages.md)）：它的 tar 包里带着清单，依赖它的应用会从 release 下载自己要构建的平台所需的库。
