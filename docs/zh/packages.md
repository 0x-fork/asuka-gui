# 包

包用来在应用之间共享 Asuka 代码：组件、store、原生包（连同它们的 Swift、Kotlin 和 JavaScript 代码）以及 C 代码（源码，或者厂商预编译好的库）。包发布在 **npm registry** 上，但安装它们的不是 npm，而是 `asuka` 命令行工具，不需要 Node。

## 使用包

```sh
asuka add @acme/charts            # 最新版本
asuka add @acme/charts@^1.2       # 一个版本范围
asuka remove @acme/charts
```

`asuka add` 会把依赖写进 `asuka.toml`，解析出具体版本，并记录到 `asuka.lock` 中：

```toml
[dependencies]
"@acme/charts" = "^1.2.0"
```

然后按名字导入它：

```asuka
import @acme/charts

charts.LineChart(points) { height: 200 }
```

`asuka dev`、`asuka build` 和 `asuka test` 第一次运行时会下载 `asuka.lock` 中列出的包，之后就一直使用下载好的副本。请把 `asuka.lock` 提交到版本库，这样所有人都用同样的版本构建。`asuka install` 会提前下载全部依赖（适合 CI）；`asuka install -update` 会按 `asuka.toml` 中的版本范围重新解析。

### 包从哪里来

| 在 asuka.toml 中 | 含义 |
|---|---|
| `"@acme/charts" = "^1.2.0"` | registry 上的一个版本范围 |
| `charts2 = "npm:@acme/charts@^2.0.0"` | 另一个包，以 `charts2` 的名字导入：用于同时使用第二个主版本 |
| `charts = "github:acme/charts#v1.2.0"` | GitHub 仓库的某个 tag、分支或提交 |
| `charts = "file:../charts"` | 项目旁边的一个目录，适合同时开发两者时使用 |

私有包来自 `.npmrc` 中指定的 registry，使用其中配置的令牌，读取方式与 npm 相同：可以是公司为某个 scope 配置的 registry（`@acme:registry=https://npm.acme.dev/`），也可以是 GitHub Packages 或 Verdaccio。`NPM_TOKEN` 是默认 registry 的令牌。

### 选用哪个版本

在一个应用中，每个包只有**一个版本**：能满足所有对它提出的版本范围的最低版本（最小版本选择，与 Go modules 的做法相同）。因此，别人发布了新版本不会让你的构建发生变化；两个人只要 `asuka.lock` 相同，构建出来的东西就相同。

同一个包的两个主版本就是两个不同的包。如果某个依赖需要 `@acme/charts` 1.x，而你需要 2.x，就用另一个名字依赖 2.x（`charts2 = "npm:@acme/charts@^2.0.0"`），并用这个名字导入。遇到这种情况时，`asuka add` 会提示你。

### 校验

每个 tarball 在使用前都会用 `asuka.lock` 中记录的哈希（SHA-512，也就是 registry 提供的 integrity）进行校验，然后按这个哈希存放在所有项目共用的缓存中（Linux 上是 `~/.cache/asuka/pkg`，其他系统上是系统的缓存目录）。安装时不会运行包里的任何东西：包没有安装脚本。

## 编写包

包就是一个库：一个包含 `asuka.toml` 和 `.asuka` 文件的目录。`asuka new --lib ./charts` 会创建一个，里面有一个函数和它的测试块（只用 `asuka new` 创建的是应用）：

```toml
name = "@acme/charts"
version = "1.2.0"
description = "Charts drawn with Asuka's Canvas"
license = "MIT"
repository = "github:acme/charts"

[dependencies]
"@acme/colors" = "^0.3.0"
```

包里的 `pub` 声明就是应用可以导入的内容。包还可以包含原生代码（[原生代码](native.md)）和 C 代码，可以是自己的源码，也可以是厂商预编译好的库（[在 Asuka 中调用 C](c.md)），它们会随包一起发布。

### 编写文档

包的文档写在源码里。第一个声明上方写一段 `// Package <name> …` 注释，说明这个包是做什么的；每个 `pub` 声明正上方的 `///` 注释说明它的作用，结构体的字段、枚举的分支和方法上方的 `///` 注释则说明它们各是什么。比周围多缩进两个空格的行是示例，会作为 Asuka 代码显示：

```asuka
// Package @acme/charts 用 Asuka 的 Canvas 画图表：柱状图、折线图
// 和饼图，数据变化时带动画。
//
//   Bars(data: sales) { color: .blue }

/// 每个值画一根柱子，按最大值缩放。
pub component Bars(data: [float]) {
  …
}
```

`asuka doc` 把包的文档输出为 Markdown：先是包的注释，然后是每个 `pub` 声明的签名和注释，按函数、组件、store 和类型分组。它接受一个导入路径、一个目录，或者什么都不写（当前目录的包），再加一个名字就只输出那一项：

```sh
asuka doc                         # 当前目录的包
asuka doc @acme/colors            # 一个依赖
asuka doc std/time Time.add       # 一个声明
asuka doc ./charts -o API.md      # 写进文件
```

本站上标准库和平台 SDK 的页面也是这样从源码生成的。编辑器在悬停时会显示 `///` 注释（见[编辑器](development.md#编辑器)）。

### 发布

```sh
asuka pkg publish                 # 使用你的 npm 登录信息或 NPM_TOKEN
asuka pkg publish -otp 123456     # 开启了双重验证的账号
asuka pkg publish -tag next       # 预发布版本
```

`asuka pkg publish` 会把目录打包（不包括 `.git`、`node_modules`、`dist` 和 `asuka.lock`），而且每次的打包方式完全相同，所以同样的源文件总会得到同样的 tarball；接着写出 registry 读取的 `package.json`，再上传。用 `npm login` 登录一次，或者设置 `NPM_TOKEN` 即可。在 CI 上用 npm 发布（在 `asuka pkg pack` 写出的目录中运行 `npm publish --provenance`），还会附上一份来源证明（provenance statement）。

版本号遵循 semver。上传之前，`asuka pkg publish` 会把包的 `pub` API 与上一个版本进行比较：删除某个声明，或者以会破坏使用者代码的方式修改它，都需要发布新的主版本。

## 标准库与平台 SDK

`std/http`、`std/json`、`std/storage` 等标准库，以及 `platform/camera`、`platform/notify` 等平台 SDK 的包，都随命令行工具一起提供，版本也跟着命令行工具走：不需要 `asuka add`。包不能以 `std/` 或 `platform/` 命名（`asuka pkg publish` 和 `asuka add` 都会拒绝）。每个包的 API 都在参考里：[标准库](std.md)和[平台 SDK](platform.md)（在终端里用 `asuka doc std/time`）。
