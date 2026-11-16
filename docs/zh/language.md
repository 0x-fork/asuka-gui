# Asuka 语言

Asuka 应用用 Asuka 语言编写。这是一门小巧的语言，专为使用各平台原生控件的界面而设计。它是静态类型的：发布构建时会预先编译成 C；开发时则运行在虚拟机上，所以保存后几毫秒内就能看到改动，所有的值和原生视图都会保留下来。

```asuka
component Counter {
  state count = 0

  SafeArea {
    justify: .center; align: .center; gap: 16; padding: 24

    Text("Count: {count}") { font: .largeTitle.bold() }
    Row {
      gap: 12
      Button("Reset") { variant: .gray; on press { count = 0 } }
      Button("Add") { variant: .filled; on press { count += 1 } }
    }
    if count >= 10 {
      Text("That's a lot") { color: .secondaryLabel }
    }
  }
}

app { Counter() }
```

本页介绍这门语言的全部内容。元素本身（`Text`、`Button`、`List`、`Stack` ……）另有专门的页面：[组件](components.md)。

## 文件、包与导入

源文件以 `.asuka` 结尾。一个目录里的源文件构成一个**包**：同一个包的文件不需要导入，就能看到彼此的声明。加上 `pub` 的声明才对其他包可见。

```asuka
import asuka/http                 // 标准库
import @acme/charts               // 一个依赖（asuka add @acme/charts）
import ./components               // 与当前包并列的另一个包
import ./model.{Message, Folder}  // 直接使用这些名字，不用加包名
import ./theme as t               // 起一个更短的名字
```

包里的名字要通过包名来使用：`http.get(url)`、`components.Card("Hi")`。包名取自路径的最后一段；最后一段不能用作名字的包（比如 `@acme/line-charts`），导入时要用 `as` 给它起一个名字。预置模块（prelude，即内置的类型、元素和函数）不需要导入。`asuka/...` 这些包都列在[标准库](stdlib.md)里。

语句和声明在行尾结束；想在一行里写多条，用 `;` 隔开。如果一行以运算符、逗号或左括号结尾，就会接着下一行继续。

注释写作 `// …` 和 `/* … */`。`///` 注释用来给它下面的声明写文档，编辑器会显示出来。

## 值与类型

| 类型 | 值 |
|---|---|
| `int` | 64 位整数：`42`、`1_000`、`0xff` |
| `float` | 64 位浮点数：`3.14`、`1e-3` |
| `bool` | `true`、`false` |
| `string` | UTF-8 文本：`"Hello, {name}"` |
| `bytes` | 原始字节 |
| `[T]` | 数组：`[1, 2, 3]`、`[]` |
| `[K: V]` | map，按插入顺序保存：`["a": 1]`、`[:]` |
| `(A, B)` | 元组：`(1, "one")` |
| `T?` | 可选值：一个 `T`，或者 `none` |
| `Duration` | `300ms`、`5s`、`2min`、`1h`（还有 `us`、`ns`） |
| `Color` | `#7c3aed`、`#7c3aed80`，以及语义色：`.label`、`.accent` …… |
| `Length` | `100`（点）、`50%`、`.auto` |
| `fn(A) -> B` | 函数和闭包 |

字符串的花括号里可以放任意表达式：`"{count} items"`、`"Total: {price * qty}"`。`\{` 表示一个花括号字符；`r"…"` 是原始字符串，没有转义也没有插值；`"""…"""` 可以跨多行（第一行为空时不算在内，结束引号所在那一行的缩进也会去掉）。不是数字、布尔值或字符串的值，显示为文本时是这样的：枚举成员显示为它的名字，可选值显示为 `none` 或其中的值，错误显示为它的消息，其他值显示为 JSON。

map 的键可以是整数、字符串、布尔值，或者成员不携带值的枚举。

### 内置方法

字符串、bytes、数组、map 和可选值都有方法。其中会修改值本身的方法（`push`、`insert`、`removeAt`、`removeWhere`、`pop`、`clear`、`sort`、`remove`）只能用在 `var` 或状态上：

| 类型 | 方法 |
|---|---|
| `[T]` | `count()`、`count(where)`、`isEmpty()`、`first()`、`last()`、`get(i)`（返回 `T?`）、`push(x)`、`insert(x, at: i)`、`removeAt(i)`、`removeWhere(p)`、`pop()`、`clear()`、`map(f)`、`filter(p)`、`any(p)`、`all(p)`、`index(p)`（返回 `int?`）、`find(p)`、`contains(x)`、`sum()`、`reversed()`、`slice(from, to)`、`sorted()`、`sorted(by)`、`sort()`、`sort(by)`、`enumerated()`、`joined(separator:)`（用于字符串数组）、`+` |
| `[K: V]` | `m[k]`（返回 `V?`）、`m[k] = v`、`count()`、`isEmpty()`、`has(k)`、`get(k)`、`remove(k)`、`keys()`、`values()`（按插入顺序） |
| `string` | `count()`（字符数）、`byteCount()`、`isEmpty()`、`upper()`、`lower()`、`trim()`、`trimStart()`、`trimEnd()`、`trimPrefix(s)`、`trimSuffix(s)`、`contains(s)`、`hasPrefix(s)`、`hasSuffix(s)`、`replace(old, new, count:)`（默认替换全部，count 指定次数）、`split(sep, count:)`、`fields()`（按空白分出的词）、`lines()`、`find(s)`、`findLast(s)`、`findAny(chars)`（字节偏移，找不到时为 none）、`slice(from, to)`（按字符）、`byteSlice(from, to)`（按字节：即 find 给出的偏移）、`padStart(width, with:)`、`padEnd(width, with:)`、`toInt()`、`toFloat()`、`repeat(n)`、`chars()`、`bytes()`、`+` |
| `bytes` | `count()`、`isEmpty()`、`get(i)`（返回 `int?`）、`slice(from, to)`、`concat(b)`、`string()`（按 UTF-8 解码，无效的部分变成 U+FFFD）、`hex()` 和 `hex(separator)`（`"04 a1"`）；`bytes()` 是空的字节序列，`bytes([0x04, 0xa1])` 是这些字节，`s.bytes()` 是字符串的 UTF-8 字节 |
| `float` | `fixed(digits)`、`rounded()`、`floor()`、`ceil()` |
| `Color` | `opacity(a)`：以原本不透明度的 a 倍绘制（语义颜色仍然是语义颜色） |
| `Duration` | `nanoseconds()`、`milliseconds()`（整数）、`seconds()`、`minutes()`、`hours()`（浮点数）、`rounded(to:)`、`truncated(to:)`、`+`、`-`、`* n`、`/ n` |
| `T?` | `isNone()`、`isSome()` |

`min`、`max`、`abs`、`float(n)` 和 `int(x)` 是函数；其余的（`math.sqrt`、`math.pow`、`math.pi` ……）在 `asuka/math` 里。

### 值语义

字符串、数组、map、结构体和枚举都是**值**：赋值或传参时，从你能观察到的行为来看，得到的都是一份副本。副本的开销很小：多个副本共享同一块内存，直到其中一个被修改；如果一个值只有一处持有，修改时会直接原地修改。

```asuka
var a = [1, 2, 3]
var b = a
b.push(4)        // a 仍然是 [1, 2, 3]
```

store、闭包、task 和视图是**引用**：复制出来的仍是同一个东西。

语言里没有 null。可能缺失的值用可选值表示：

```asuka
let user: User? = none
if let u = user { Text(u.name) }
let name = user?.name ?? "Guest"
let first = items.first()!        // 为 none 时会崩溃：只在确定不可能为 none 时这样写
```

`?.` 之后，链条剩下的部分遇到 none 也会停下：`user?.boss.name` 的类型是 `string?`。

### 声明

```asuka
let pi = 3.14159             // 常量
var total = 0                // 变量（在函数和处理函数中）
state count = 0              // 状态：界面会跟随变化的值
```

类型会自动推断；在有助于阅读的地方可以写出来：`let ids: [int] = []`。

### 结构体

```asuka
struct Message {
  id: string
  subject: string
  read: bool = false

  fn preview() -> string = subject.slice(0, 40)
}

let m = Message(id: "1", subject: "Hello")
let n = Message(id: m.id, subject: m.subject, read: true)
```

创建结构体的方式是调用它，按名字或按顺序传入各个字段；有默认值的字段可以省略。结构体的方法直接用名字访问它的字段，但不能修改结构体（它是一个值）：需要修改时，就返回一个新的结构体。结构体主体里的 `let` 是这个类型的常量（`Font.title`），在任何需要 `Font` 的地方都可以简写成 `.title`。

### 枚举

```asuka
enum Filter {
  all
  unread
  from(Person)

  fn matches(m: Message) -> bool = match self {
    .all => true
    .unread => !m.read
    .from(p) => m.from == p
  }
}

let f: Filter = .unread
```

在类型已知的地方，枚举的成员写作 `.name`。`match` 必须覆盖所有成员（或者以 `_ =>` 结尾）；模式可以绑定成员携带的值（`.from(p)`），可以匹配字面量（`0`、`"x"`），可以同时匹配多个成员（`.a | .b`），还可以带守卫条件（`.from(p) if p.vip =>`）。成员带标签的值如果有零值（`false`、`0`、`""`、空数组或空映射、`none`、枚举的第一个成员），构造时可以省略：`.button(title: "Copy", run: copy)` 就省略了 `Action.button` 的 `icon`、`role`、`disabled` 和 `checked`。

### 函数与闭包

```asuka
fn area(width: float, height: float = 1) -> float {
  return width * height
}

fn double(x: int) -> int = x * 2      // 只有一个表达式

area(3, height: 4)
let squares = [1, 2, 3].map(x => x * x)
let sorted = people.sorted((a, b) => a.age < b.age)
```

参数先按位置传，再按名字传。闭包写作 `x => expr`、`(a, b) => expr` 或 `x => { statements }`；它按值捕获普通的值，按引用捕获状态。

### 接口与泛型

接口是一组方法和字段；一个类型只要具备这些方法和字段，就实现了这个接口（不需要额外声明）。

```asuka
interface Shape { fn area() -> float }

fn total<T>(items: [T], size: fn(T) -> float) -> float {
  var sum = 0.0
  for i in items { sum += size(i) }
  return sum
}
```

泛型函数和泛型类型可以接收类型参数；可用的约束有 `Equatable`、`Comparable` 和 `Hashable`，当结构体和枚举的字段都满足某个约束时，它们自身也就满足这个约束。

## 组件

组件是界面中的一块，参数在头部声明，其余所有内容都写在主体里：

```asuka
component MessageRow(message: Message) {
  prop compact = false            // 一个具名 prop，带默认值
  prop accessory: View? = none    // 一个插槽：由调用方传入的视图
  event archive                   // 它对外报告的事件
  state expanded = false          // 它自己的状态

  Row {
    gap: 12; padding: [12, 16]
    Avatar(message.from)
    Column {
      grow: 1
      Text(message.subject) { font: .headline; lines: 1 }
      if !compact { Text(message.preview()) { color: .secondaryLabel } }
    }
    accessory
  }
  contextMenu {
    Button("Archive") { role: .destructive; on press { emit archive } }
  }
}
```

使用组件的方式和使用内置元素完全一样。头部参数放在圆括号里，其余一切都放在代码块里：

- prop：`name: value`
- 处理函数：`on name { … }`
- 双向绑定：`bind name: target`
- 样式：`style name`
- 子元素

```asuka
MessageRow(m) {
  compact: true
  accessory: Badge(3)
  on archive { mailbox.archive(m.id) }
}
```

每样东西都只有一个位置：头部参数不能在代码块里设置，prop 也不能放在圆括号里传。

接收子元素的组件要声明 `prop children: View`，调用方代码块里的内容就会放到这里。具名插槽在代码块里写作 `name { … }`（`toolbar { … }`）。

`emit name(args)` 触发一个事件，由调用方的 `on name(args) { }` 来处理。没有人处理的事件什么也不做；把事件名当作值读取，得到的是调用方有没有处理它（`if click { … }`：比如托盘图标，调用方不接管点击时，点击就打开它的菜单）。

只显示一个元素的组件（主体里的视图就是那一个元素，或另一个同样如此的组件）也接受样式属性：调用方给的样式落在那个元素上，并覆盖组件自己在那里设置的值。

```asuka
component Card(title: string) {
  Column {
    padding: 16; radius: 12; background: .secondaryBackground
    Text(title)
  }
}

Card("Plan") { padding: 8; width: 240 }   // padding 为 8，radius 为 12
```

### 组件的主体是声明

组件的主体**不是**运行一次就被抛到脑后的代码。主体里的每一行都声明了一件始终成立的事：

- `state x = …` 是组件自己拥有的状态。
- `let y = …` 由它读取的内容派生而来，并会一直保持最新（只在被读取时才计算，而且只有当它读过的内容变化后才会再次计算）。
- 每个元素的每个 prop 值都以同样的方式保持最新：`count` 变化时，`Text("Count: {count}")` 跟着变化，其他东西都不会变。
- `if`、`for` 和 `match` 会按照条件显示对应的内容，现在如此，之后也一样。

`count` 变化时，重新运行的只有读取了它的部分：那个 `Text` 的文字，以及那个 `if`。没有任何组件会重新运行，平台的视图也只在确实有变化的地方才会被改动。

### 处理函数与语句

代码在这些地方运行：处理函数（`on press { … }`）、函数、`task` 和 `effect` 代码块，以及测试。在这些地方读取状态，得到的是它的当前值；写入状态，读取它的地方就会更新：

```asuka
on press {
  count += 1
  todos.push(Todo(title: draft))
  todos[0].done = true      // 写入 todos：只有读取了 todos[0] 的行会更新
  draft = ""
}
```

一个处理函数里的多次写入，会在它结束时（或者执行到 `await` 时）一起显示出来。

语句有：`let`、`var`、赋值（`= += -= *= /= %= ??=`）、`if`、`for x in xs`、`for (i, x) in xs.enumerated()`、`while`、`match`、`return`、`break`、`continue`、`throw`、`do { } catch { }`、`defer { }`、`emit`。

### 双向绑定

控件既要显示一个值，也要修改它。`bind` 把状态交给控件，两件事就都能做：

```asuka
state query = ""
state notify = true

TextField { bind text: query; placeholder: "Search" }
Toggle { bind on: notify; title: "Notifications" }
```

不用 `bind` 时，控件只负责报告变化：`TextField { text: name; on change(t) { name = t.trim() } }`。

### 视图列表

```asuka
for todo in todos key todo.id {
  TodoRow(todo) { on delete { todos.removeWhere(t => t.id == todo.id) } }
}
```

`for` 为每个键保留一行。某一项变化时，对应的行只更新它自己的绑定。新增一项会创建一行，删除一项会销毁它的那一行。移动了位置的行只会被移动，不会重建。直接放在 `List` 下面时，行只在显示在屏幕上时才存在，所以一个有 100,000 项的列表，开销也只是屏幕上可见的那些。

### Store

store 是状态加上修改它的函数，由使用它的视图共享：

```asuka
pub store Mailbox {
  state messages: [Message] = []
  let unread = messages.count(m => !m.read)

  pub fn markRead(id: string) {
    if let i = messages.index(m => m.id == id) { messages[i].read = true }
  }

  pub async fn refresh() throws {
    messages = try await http.get("https://example.com/inbox").json()
  }
}
```

store 的创建方式和结构体一样（`Mailbox()`），它属于创建它的那个组件或 store。`provide` 让它对下面的所有内容可用，`use` 用来取到它：

```asuka
app {
  provide Mailbox()
  Stack { Inbox() }
}

screen Inbox {
  use mailbox: Mailbox
  …
}
```

写在文件顶层的 `state` 属于整个应用：全应用只有一份。

store 和结构体一样可以带类型参数，由参数或期望的类型推断出来：

```asuka
store Kept<T>(key: string, initial: T) {
  pub state value: T = initial
}

let size = Kept("size", 17.0)                 // 一个 Kept<float>
let tags: Kept<[string]> = Kept("tags", [])
```

在 store 的 fn 里，`self` 就是这个 store 本身，可以把它交给它创建的东西。

### Effect 与 task

```asuka
effect { storage.set("draft", draft) }        // 立即运行，之后 draft 每次变化时再运行
task { try? await mailbox.refresh() }         // 组件显示期间运行一次
task(query) { results = await search(query) } // query 变化时再运行一次
```

effect 会立即运行，之后每当它读取过的内容变化时再次运行。task 是异步的（见下文）：组件出现时启动；圆括号里的内容变化时重新启动（并取消上一次运行）；组件消失时被取消。

## 异步

需要等待的工作（网络、文件、定时器）用 `async` 表示，等待它则用 `await`：

```asuka
async fn loadThread(id: string) throws -> Thread {
  let (messages, people) = try await all(api.messages(id), api.people(id))
  let html = await spawn { renderMarkdown(messages) }
  return Thread(messages, people, html)
}
```

- 调用异步函数需要 `await`。处理函数、task 和测试里可以使用 `await`；组件的主体及其中的 `let` 不行。
- **主线程。** 所有代码都在主线程上运行；`await` 让你的代码在等待时，其他工作可以先运行。状态可以在主线程上修改，而且只能在主线程上修改。
- `all(a, b)` 同时发起多个调用，并等待全部结果；`race(a, b)` 取第一个完成的结果，并取消其余的；此外还有 `sleep(300ms)` 和 `withTimeout(5s) { … }`。它们是结构化的：被它们取消的工作会先停下、执行完自己的 `defer`，它们才返回或抛出错误。`await never()` 什么都不等，只等任务结束：适合在组件显示期间一直持有某样东西的 task（`defer { release() }; await never()`），或只在读者离开时才结束的流。
- `spawn { … }` 在另一个线程上运行一段代码，用于会拖住主线程的工作（解析、图片处理）。这段代码只能使用值，不能使用状态、store 或视图：如果用了，编译器会报错。在 Web 上，它运行在页面的一个 Web Worker 里。
- `for await message in socket.messages { … }` 读取一个流，也就是 `Stream<T>`：随时间陆续到来的值。`Stream(send => { … })` 创建一个流：流第一次被读取时，这个代码块作为 task 开始运行，每次 `send(x)` 就是一个值；代码块返回时流结束，代码块抛出错误时流也以这个错误失败。离开循环（`break`、出错、owner 被销毁）会取消这个代码块。一个流只能有一个读取者。

  ```asuka
  fn levels() -> Stream<float> = Stream(send => {
    try await battery.watch(changed: level => send(level))
  })
  ```
- `live(initial, source)` 是一个由流保持最新的值，以读取它的函数的形式出现：在绑定中读取时，从第一次这样的读取开始跟随 `source()` 的流，直到最后一个读取者离开（随后流被取消：不会在没人需要时还监听平台）；在别处读取时，得到的是最后已知的值。包就是这样提供平台会变化的东西的，比如网络状态：

  ```asuka
  let level = live(1.0, levels)
  pub fn batteryLevel() -> float = level()
  ```
- **取消。** task 属于启动它的那个东西。当那个东西消失时（比如用户离开了页面），task 就会被取消：它正在等待的那个 `await` 会结束它，它的 `defer` 会运行，并且不会报告任何错误。

### 需要加载的数据

`load` 把一个异步结果保存在组件里：

```asuka
screen Profile(id: string) {
  let user = load api.user(id)   // id 变化时重新加载

  match user {
    .pending => Spinner()
    .failed(e) => ErrorView(e) { on retry { user.reload() } }
    .ready(u) => ProfileCard(u)
  }
}
```

`Loading<T>` 的值是 `.pending`、`.ready(value)` 或 `.failed(error)` 之一；`.value` 是最近一次得到的值（重新加载期间也会保留，这样页面不会闪烁），`.reload()` 会再运行一次。当它的参数所读取的内容变化时，它会重新运行，并取消那次即将过时的运行。

## 错误

可能失败的函数要标注 `throws`，调用它时需要 `try`：

```asuka
enum MailError: Error { offline, server(code: int) }

async fn send(m: Message) throws {
  if !network.online { throw MailError.offline }
  …
}

on press {
  do {
    try await send(draft)
    dismiss()
  } catch MailError.offline {
    showOffline = true
  } catch e {
    alert("Couldn't send", message: e.message)
  }
}

let n = try? parse(text)   // 抛出错误时为 none
```

错误的 `message` 是它的 `message` 字段、它的 `fn message() -> string`，或者它的类型名和成员名。`Error("…")` 用一条消息创建一个错误。`catch` 永远不会捕获 task 的取消：取消会一直走到 task 的结尾，并在途中执行各个 `defer`。

处理函数或 task 没有捕获的错误，会交给包住这个组件的最近的 `Boundary`；如果没有，就交给应用自己的处理（开发时显示出来，发布版本中记录到日志）：

```asuka
Boundary {
  Feed()
  on error(e) { Text("Something went wrong: {e.message}") }
}
```

## 样式

每个元素都接收样式 prop：先是布局（`direction`、`justify`、`align`、`gap`、`padding`、`margin`、`width`、`height`、`grow`、`shrink`、`absolute`、`top`、`grid` ……），然后是外观（`background`、`radius`、`border`、`opacity`、`shadow`、`font`、`color`、`lines` ……）。详见[布局与样式](layout.md)。

```asuka
style card {
  padding: 16; radius: 12; gap: 8
  background: .secondaryBackground
}

View { style card; background: .accent }   // 后写的 prop 优先
```

`pub style` 也能在别的包里使用：写作 `style theme.card`，其中的值在声明它的包里求值。

不设置任何样式的元素，看起来就和平台自己的一样。

容器（`View`、`Row`、`Column`、`SafeArea`）可以写**变体**：以交互状态命名的一组样式 prop，在容器处于该状态时叠加到它的样式上，相当于 CSS 的 `:hover` 和 `:active`。状态有四种：`hovered`、`pressed`、`dragging`（正被拖动时）和 `dragOver`（它接受的拖动正停在它上方时），按这个顺序叠加，所以 `pressed` 会盖过 `hovered`。变体只覆盖它写出的 prop，它的值和普通 prop 一样是绑定：

```asuka
View {
  padding: 16; radius: 12; background: .secondaryBackground
  hovered { borderColor: .accent }
  pressed { background: .separator; opacity: 0.8 }
  on press { open() }
}
```

只有当某处用到了某个状态，视图才会上报它。因此没有写 `pressed` 变体的视图，仍然保留平台自己的按下效果。变体里也可以用 `style`（`pressed { style raised }`）。其他视图可以通过名字跟随一个视图的状态（见[给视图命名](#给视图命名)）：写成 `View as card { … }`，然后在任意绑定里读 `card.hovered`（或 `card.pressed`、`card.dragging`、`card.dragOver`），子视图可以这样做（整组悬停），兄弟视图也可以。与样式无关的反应则交给处理器：`on pressedChange(p)`、`on hoverChange(h)`。

## 给视图命名

有些事不是给视图设置一个值，而是要对视图做点什么：播放视频、在网页里运行脚本、往输入框里插入文本、滚动列表。`Ref` 就是视图的句柄：在任意元素上写 `ref: r`，它的视图就放进 `r`；写成 `Element as name`，则会为所在代码块的剩余部分声明一个这样的句柄。

```asuka
component Player {
  let video = Ref()

  Video("intro.mp4") { ref: video }
  Button("Start over") {
    on press {
      video.seek(0)
      video.play()
    }
  }
}
```

只有视图显示期间，Ref 才持有它；在那之前和之后都是空的，这时命令什么也不做，状态都是 false。Ref 的状态会被追踪，读取它的绑定会跟着它变化：

| 适用于 | Ref 的成员 |
|---|---|
| 任意视图 | `hovered`、`pressed`、`dragging`、`dragOver`（bool，会被追踪）；`focus()`、`blur()` |
| `Scroll`、`List` | `scrollTo(x, y, animated: true)` |
| `Video` | `play()`、`pause()`、`seek(seconds)` |
| `WebView` | `try await eval(script)`（脚本的值，以 JSON 表示）、`run(script)`（不等待结果）、`goBack()`、`goForward()`、`reload()`、`stopLoading()` |
| `TextField` | `insert(text)`、`format(.bold, on)`、`toggleFormat(.italic)`、`setLink(url)`（作用于富文本输入框的选区） |

Ref 可以传来传去（`shareFiles(paths, from: button)`、`a11yFocus(field)`），也可以放在文件顶层的 `let` 里，供多个组件的视图使用。

## 动画

`animate { … }` 会立即运行代码块，块中对 frame 和绘制内容（不透明度、背景、圆角半径、形状的裁剪）所做的改动，都会以原生动画呈现。`animate(.spring) { … }` 接收一个 `Animation`：时长、曲线、延迟、重复次数，以及弹簧动画的阻尼和初速度。`await` 它时，会等到动画完成才返回；不 `await` 也可以。

```asuka
on press {
  animate { expanded = !expanded }
}
on press {
  await animate(Animation(duration: 500ms, curve: .spring)) { offset = 0 }
  done = true
}
```

`animate(() => …)` 和 `animate(anim, () => …)` 是同样的写法，只是用闭包代替代码块。`animating()` 表示当前所做的改动是否处在动画中。`onFrame(t => …)` 在显示器绘制的每一帧调用闭包，传入这一帧的时间，直到调用它返回的闭包，或组件消失为止：`let stop = onFrame(t => tick(t))`。`frames()` 把同样的帧作为 `Stream<Duration>` 提供，只在被读取时才会绘制帧：在任务里写 `for await t in frames() { … }`，循环结束或任务消失时就会停止。

## 绘制

`Canvas(g => …)` 用一个 `Graphics` 来绘制，坐标以点为单位、从左上角算起。它在布局完成后绘制一次，尺寸或读取的内容变化时再重新绘制。`Shape(fn(width, height) -> Path)` 是一个由平台负责填充和描边的轮廓（`fill`、`stroke`、`lineWidth`、`trimEnd`、`dash` ……）。

```asuka
Canvas(g => {
  let ring = g.path().arc(g.width / 2, g.height / 2, 40, 0, 6.28318)
  g.stroke(ring, .accent, StrokeStyle(width: 8, cap: .round))
  g.text("{percent}%", g.width / 2, g.height / 2, CanvasText(size: 20, align: .center))
}) { height: 100 }
```

`Path` 通过它的方法构建，每个方法都返回它自己：`Path().move(x, y)`、`line`、`quad`、`cubic`、`rect`、`roundedRect`、`ellipse`、`arc(cx, cy, radius, start, end, clockwise: true)`（弧度，从 x 轴正方向起算）、`close()`；`g.path()` 是画布自己的一个空路径，每次绘制都会复用。`Graphics` 只在绘制函数运行期间有效：`width`、`height`、`fill(path, color)`（也可以传 `LinearGradient` 或 `RadialGradient`）、`stroke(path, color 或渐变, style)`、`save()`、`restore()`、`translate`、`scale`、`rotate(degrees)`、`opacity`、`clip(path)`、`transform(a, b, c, d, tx, ty)`、`evenOdd(on)`、`text(s, x, y, style)`。

`Shader(shader("""…"""))` 是一个由 GPU 程序绘制的方框。程序用 Kage 编写，随应用一起编译（程序里有错误时，由编译器报告），uniform 按名字设置（`uniforms: [Uniform(name: "Level", value: [0.5]), colorUniform("Tint", .accent)]`）。

## 系统功能

| 函数 | 作用 |
|---|---|
| `share(text)` | 为一段文本或一个链接弹出分享面板 |
| `try await shareFiles(paths, title:, from: ref)` | 为文件弹出分享面板，从某个视图弹出（弹出框指向的位置）；面板关闭后返回 |
| `haptic(.success)` | 平台的触感反馈（一个 `Haptic`） |
| `copy(text)` | 把文本放到剪贴板 |
| `openURL(url)` | 用能处理 url 的应用打开它 |
| `announce(text)` | 让屏幕阅读器读出文本 |
| `a11yFocus(ref)` | 把屏幕阅读器的焦点移到该视图 |
| `try await webCookies(url)` | Web 视图为 url 保存的 cookie |
| `useKeys([Shortcut(…)])` | 组件存在期间，窗口的快捷键 |
| `onOpenURL(u => …)`、`onOpenFiles(paths => …)`、`onShare(items => …)` | 组件存在期间，应用被打开时带来的内容，或其他应用分享给它的内容 |
| `quit()`、`relaunch()` | 像用户选择“退出”那样退出应用（并重新启动它），会先询问应用；仅桌面端 |
| `closeWindow()` | 不询问，直接关闭代码所在的窗口 |
| `onCloseRequest(fn() -> bool)`、`onQuit(e => …)`、`onSecondInstance((args, dir) => …)` | 组件存在期间：组件所在窗口关闭前的询问；应用即将退出（`e.cancel()`）；应用再次被启动 |

`useKeys` 和这些 `on…` 注册写在组件的主体里（或 app 代码块里）。app 代码块还可以把它们当作事件来处理：`on openURL(url) { … }`、`on files(paths) { … }`、`on share(items) { … }`、`on quit(e) { … }`、`on secondInstance(args, dir) { … }`。它的第一个 `Window` 是主窗口（见[导航](navigation.md#窗口)）。

## 导航

```asuka
app {
  Tabs {
    Tab("Inbox", icon: "tray") { Stack { Inbox() } }
    Tab("Settings", icon: "gear") { Stack { Settings() } }
  }
}

screen Inbox {
  title: "Inbox"
  List {
    for m in messages key m.id {
      MessageRow(m) { on press { push(Thread(m.id)) } }
    }
  }
  toolbar {
    Button(icon: "square.and.pencil") { on press { present(Compose()) } }
  }
}
```

`screen` 是一种组件，它的根是导航栈中的一个页面：它的代码块接收页面的 prop（`title`、`largeTitle` ……）和插槽（`toolbar`）。`push(…)` 和 `pop()` 作用于最近的导航栈，`present(…)` 和 `dismiss()` 作用于弹出面板（sheet）。详见[导航](navigation.md)。

## 多语言文本

`tr.inboxTitle(unread: n)` 说出应用目录文件（`locales/en.json`、`de.json`……）里的一条文案，并在应用编译时检查：缺少键或参数，或者参数类型不对，都会报错。在绑定里说出的文案会跟随应用的语言，所以切换语言（`i18n.setLocale("de")`，来自 asuka/i18n）时，显示的内容也会随之改变；从右往左书写的语言还会让整个应用从右侧开始排版。详见[多语言](i18n.md)。

## 原生代码

只有平台才能做的事，放在原生包里：用 Asuka 声明，再用各平台自己的语言实现：

```asuka
pub native interface Haptics {
  fn impact(style: Feedback)
}

pub native component MapView(region: Region) {
  prop pins: [Pin] = []
  event regionChanged(region: Region)
}
```

创建模块和调用它都可能失败（当前平台可能没有它的代码），所以两者都要加 `try`：`let h = try Haptics()`、`try h.impact(.light)`。`asuka native generate` 会生成各平台需要实现的 Swift、Kotlin、JavaScript 和 Go 代码。详见[原生代码](native.md)。

## 测试

```asuka
test "archiving removes the row" {
  let app = mount(Inbox(), provide: Mailbox.fake([sample]))
  await app.idle()
  app.tap("Archive")
  app.expectNo("Hello")
}
```

`expect(cond)` 在 cond 为 false 时让测试失败，并指出失败的是什么、在哪里（如果是比较，还会给出两边的值）。JSON 由 `asuka/json` 处理：`let notes: [Note] = try json.decode(text)` 按期望的类型读取 JSON（也可以写成 `json.decode<[Note]>(text)`），`json.encode(notes)` 把值写成 JSON。泛型函数也可以把它的 `T` 当作 JSON 读写；这样每次调用时，`T` 都必须是 JSON 能表示的类型，编译器会在调用处检查这一点。

`asuka test` 在假宿主上运行包里的测试：它会像各平台那样对视图进行布局，按每个字符固定宽度来测量文字，而且只有在没有其他东西可以运行时，时间才会前进（测试里的 `sleep(5s)` 不花任何时间）。`mount` 返回应用，供你驱动和检查：`tap`、`type(_, into:)`、`scroll`、`back`、`expect`、`expectNo`、`idle`。详见[测试](testing.md)。

## 内存与性能

没有垃圾回收器，所以也就没有停顿。值采用引用计数，并在写入时复制。状态、store、effect 和 task 都属于创建它们的组件，并随它一起销毁。

- 一次变化只会运行读取了变化内容的那些绑定，也只把变化了的 prop 发给平台。这些 prop 打成一个批次发送，然后运行布局，再把各视图的位置和尺寸（frame）打成另一个批次发送。
- 发布构建会和核心一起编译成 C，所以你发布的应用里没有解释器。
- 列表只构建屏幕上可见的部分，并复用单元格。

## 关键字

`app as async await bind break catch component continue defer do effect
else emit enum event false fn for if import in interface key let load
match native none on prop provide pub return screen self spawn state
store struct style task test throw throws true try type use var while`
