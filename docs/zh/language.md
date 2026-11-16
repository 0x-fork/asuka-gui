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
| `int` | 64 位整数：`42`、`1_000`、`0xff`、`0b1010`、`0o17` |
| `float` | 64 位浮点数：`3.14`、`1e-3` |
| `bool` | `true`、`false` |
| `string` | UTF-8 文本：`"Hello, {name}"` |
| `bytes` | 原始字节 |
| `[T]` | 数组：`[1, 2, 3]`、`[]` |
| `[K: V]` | map，按插入顺序保存：`["a": 1]`、`[:]` |
| `Set<T>` | 集合：一组互不相同的值，按插入顺序保存；在需要集合的地方写 `["a", "b"]` |
| `(A, B)` | 元组：`(1, "one")` |
| `T?` | 可选值：一个 `T`，或者 `none` |
| `Duration` | `300ms`、`5s`、`2min`、`1h`（还有 `us`、`ns`） |
| `Color` | `#7c3aed`、`#7c3aed80`，以及语义色：`.label`、`.accent` …… |
| `Length` | `100`（点）、`50%`、`.auto` |
| `fn(A) -> B` | 函数和闭包 |

字符串的花括号里可以放任意表达式：`"{count} items"`、`"Total: {price * qty}"`。`\{` 表示一个花括号字符；`r"…"` 是原始字符串，没有转义也没有插值；`"""…"""` 可以跨多行（第一行为空时不算在内，结束引号所在那一行的缩进也会去掉）。不是数字、布尔值或字符串的值，显示为文本时是这样的：枚举成员显示为它的名字，可选值显示为 `none` 或其中的值，错误显示为它的消息，其他值显示为 JSON。

map 的键和集合的元素可以是整数、字符串、布尔值，或者成员不携带值的枚举（也就是满足 `Hashable` 的类型）。

### 运算符

下表按结合从紧到松排列；同一行的运算符优先级相同，从左往右结合（`??` 从右往左）：

| 运算符 | 作用 |
|---|---|
| `!x`、`-x`、`~x` | 取反、取负、把整数的每一位取反 |
| `<<`、`>>` | 把整数的位左移、右移（右移保留符号） |
| `*`、`/`、`%`、`&` | 乘、除、取余、按位与 |
| `+`、`-`、<code>&#124;</code>、`^` | 加（也用于拼接字符串和数组）、减、按位或、按位异或 |
| `..`、`..<` | 范围：`1..3`、`0..<n` |
| `==`、`!=`、`<`、`<=`、`>`、`>=` | 比较（不能连写） |
| `&&` | 与 |
| <code>&#124;&#124;</code> | 或 |
| `??` | 可选值里的值，为 none 时取默认值 |

按位运算符只接受整数。移位 64 位或更多时，所有位都会被移出（负数右移得到 -1）；移位数为负数则是一个错误。有了这些运算符和 `b[i]`（以整数形式读出一个字节），程序就能读写二进制数据，比如蓝牙特征值或 NFC 记录：

```asuka
let raw = b[1] | b[2] << 8                         // 小端序的 uint16
let celsius = if raw & 0x8000 != 0 { raw - 0x10000 } else { raw }
let out = bytes([flags, value & 0xff, value >> 8])
```

`int` 的运算会回绕：`+`、`-`、`*` 的结果超过最大的 `int`（2⁶³ − 1）时，会按 64 位二进制补码的规则从最小值接着算下去（`9223372036854775807 + 1` 等于 `-9223372036854775808`）。哈希、校验和以及二进制协议需要的正是这种行为，而且这样每次运算都不必额外检查溢出。除以零（`/`、`%`）会抛出错误；对超出范围的浮点数调用 `int(x)`，得到离它最近的 `int`（NaN 得到 `0`）。

### 内置方法

字符串、bytes、数组、集合、map 和可选值都有方法。其中会修改值本身的方法（`push`、`insert`、`removeAt`、`removeWhere`、`pop`、`clear`、`sort`、`remove`）只能用在 `var` 或状态上：

| 类型 | 方法 |
|---|---|
| `[T]` | `count()`、`count(where)`、`isEmpty()`、`first()`、`last()`、`get(i)`（返回 `T?`）、`push(x)`、`insert(x, at: i)`、`removeAt(i)`、`removeWhere(p)`、`pop()`、`clear()`、`map(f)`、`filter(p)`、`any(p)`、`all(p)`、`index(p)`（返回 `int?`）、`find(p)`、`contains(x)`、`sum()`、`reversed()`、`slice(from, to)`（省略 `to` 时到末尾）、`prefix(n)`、`suffix(n)`、`sorted()`、`sorted(by)`、`sort()`、`sort(by)`、`enumerated()`、`joined(separator:)`（用于字符串数组）、`+` |
| `Set<T>` | `count()`、`isEmpty()`、`contains(x)`、`insert(x)`（返回是否新加入）、`remove(x)`（返回原本是否存在）、`union(s)`、`intersection(s)`、`subtracting(s)`、`isSubset(of: s)`、`isSuperset(of: s)`；`Set(xs)` 是数组中互不相同的元素，`Set<T>()` 是空集合 |
| `[K: V]` | `m[k]`（返回 `V?`）、`m[k] = v`、`count()`、`isEmpty()`、`has(k)`、`get(k)`、`remove(k)`、`keys()`、`values()`（按插入顺序） |
| `string` | `count()`（字符数）、`byteCount()`、`isEmpty()`、`upper()`、`lower()`、`trim()`、`trimStart()`、`trimEnd()`、`trimPrefix(s)`、`trimSuffix(s)`、`contains(s)`、`hasPrefix(s)`、`hasSuffix(s)`、`replace(old, new, count:)`（默认替换全部，count 指定次数）、`split(sep, count:)`、`fields()`（按空白分出的词）、`lines()`、`find(s)`、`findLast(s)`、`findAny(chars)`（字节偏移，找不到时为 none）、`slice(from, to)`（按字符）、`byteSlice(from, to)`（按字节：即 find 给出的偏移）、`padStart(width, with:)`、`padEnd(width, with:)`、`toInt()`、`toFloat()`、`repeat(n)`、`chars()`、`bytes()`、`+` |
| `bytes` | `b[i]`（返回 `int`，取值 0 到 255；越界是一个错误）、`count()`、`isEmpty()`、`get(i)`（返回 `int?`）、`slice(from, to)`、`concat(b)`、`string()`（按 UTF-8 解码，无效的部分变成 U+FFFD）、`hex()` 和 `hex(separator)`（`"04 a1"`）；`bytes()` 是空的字节序列，`bytes([0x04, 0xa1])` 是这些字节，`s.bytes()` 是字符串的 UTF-8 字节 |
| `int` | `float()`、`abs()`、`padded(width)`（`7.padded(2)` 是 `"07"`） |
| `float` | `fixed(digits)`、`rounded()`、`floor()`、`ceil()`、`int()`（向零取整）、`abs()` |
| `Color` | `opacity(a)`：以原本不透明度的 a 倍绘制（语义颜色仍然是语义颜色）；`hex()`（`"#0a84ff"`；语义颜色则是它的名字：`".accent"`） |
| `Duration` | `nanoseconds()`、`milliseconds()`（整数）、`seconds()`、`minutes()`、`hours()`（浮点数）、`rounded(to:)`、`truncated(to:)`、`+`、`-`、`* n`（整数或浮点数：`1.5 * 1h`）、`/ n`、`/ d`（另一个 Duration：它能容纳几个，一个浮点数） |
| `Length` | `* n`（`width: 50% * 2`：缩放一个长度） |
| `T?` | `isNone()`、`isSome()` |

`min`、`max`、`abs`、`float(n)` 和 `int(x)` 是函数；其余的（`math.sqrt`、`math.pow`、`math.pi` ……）在 `asuka/math` 里。

### 值语义

字符串、数组、集合、map、结构体和枚举都是**值**：赋值或传参时，从你能观察到的行为来看，得到的都是一份副本。副本的开销很小：多个副本共享同一块内存，直到其中一个被修改；如果一个值只有一处持有，修改时会直接原地修改。

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

`type` 在文件顶层给类型起一个名字：`type Pair<A, B> = (A, B)`、`type Check = fn(string) -> bool`。它只是同一个类型的别名：`Pair<int, string>` 就是 `(int, string)`。

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

结构体或枚举主体里的 `static fn` 是这个类型自己的函数，没有 `self`：在类型上调用，和普通函数一样可以用具名参数和默认值（`Money.of(dollars: 2)`），在任何需要这个类型的地方也可以写成 `.of(…)`。

```asuka
struct Money {
  cents: int

  static fn of(dollars: int, cents: int = 0) -> Money = Money(cents: dollars * 100 + cents)
}

let price: Money = .of(dollars: 4, cents: 50)
```

结构体可以带类型参数，创建时会自动推断：

```asuka
struct Page<T> {
  items: [T]
  next: string? = none
}

let page = Page(items: [1, 2, 3])     // 一个 Page<int>
```

在 `match` 和 `if let` 里，结构体模式按字段名匹配结构体的字段：单写一个 `y` 会绑定字段 `y`，`y: py` 把它绑定为 `py`，`x: 0` 匹配一个值，没有写出的字段匹配任何值。模式可以嵌套：

```asuka
match line {
  Line(from: Point(x: 0, y: 0), dashed: true) => "dashed, from the origin"
  Line(from: Point(x: 0, y), to) => "from {y} on the y axis to {to.x}"
  Line(dashed) => "a line"
}
```

结构体可以从它的方法获得运算符：`fn add(o: T) -> R` 就是 `a + b`，`fn sub(o: T) -> R`（或 `minus`）就是 `a - b`，`fn compare(o: T) -> int`（负数、零或正数）就是 `<`、`<=`、`>` 和 `>=`。`asuka/time` 的 `Time` 就是这样加上一个 `Duration`、减去另一个 `Time` 的：

```asuka
struct Money {
  cents: int

  fn add(o: Money) -> Money = Money(cents: cents + o.cents)
  fn compare(o: Money) -> int = cents - o.cents
}

let total = Money(cents: 250) + Money(cents: 99)
let over = total > Money(cents: 300)
```

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

在类型已知的地方，枚举的成员写作 `.name`。`match` 必须覆盖所有成员（或者以 `_ =>` 结尾）；模式可以绑定成员携带的值（`.from(p)`，也可以按标签绑定：`.server(code: c)`），可以匹配字面量（`0`、`"x"`），可以同时匹配多个成员（`.a | .b`），可以嵌套（`.from(Person(vip: true))`），还可以带守卫条件（`.from(p) if p.vip =>`）。编译器检查 `match` 是否覆盖了所有情况时会深入嵌套的模式，并把缺少的情况写成模式告诉你：`match doesn't cover .key(_, false)`。成员带标签的值如果有零值（`false`、`0`、`""`、空数组或空映射、`none`、枚举的第一个成员），构造时可以省略：`.button(title: "Copy", run: copy)` 就省略了 `Action.button` 的 `icon`、`role`、`disabled` 和 `checked`。

成员携带的值可以是这个枚举本身，因此枚举可以表示一棵树：

```asuka
enum Expr {
  num(int)
  add(Expr, Expr)

  fn eval() -> int = match self {
    .num(n) => n
    .add(a, b) => a.eval() + b.eval()
  }
}
```

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

参数先按位置传，再按名字传。闭包写作 `x => expr`、`(a, b) => expr` 或 `x => { statements }`。闭包和周围的代码共享它捕获的变量：一方修改了，另一方也能看到。

```asuka
var n = 0
let inc = () => { n += 1 }
inc()
inc()                // n 现在是 2
```

状态也以同样的方式共享。只有 `spawn` 会复制它用到的东西（只能是普通的值，取它开始运行那一刻的值），因为它在另一个线程上运行。

函数、处理函数、闭包和方法的主体里也可以声明 `fn`。它像闭包一样能看到周围的东西，可以调用自己，也可以调用同一个代码块里紧挨着它声明的其他 `fn`（它们也能调用它）：

```asuka
fn total(root: Node) -> int {
  var sum = 0
  fn walk(n: Node) {
    sum += n.size
    for c in n.children { walk(c) }
  }
  walk(root)
  return sum
}
```

它和其他函数一样是一个值（`items.map(walk)`）。它的参数按顺序传入，不能带标签或默认值，它也不能是泛型的。

函数要调用自己，就这样写。如果把闭包存进一个变量，再通过这个变量调用自己（`var walk = …; walk = n => { … walk(c) … }`），闭包就持有这个变量，而变量又持有闭包：两者都永远不会被释放，因为没有什么会回收这样的循环引用，编译器也会对此给出警告。局部 `fn` 调用自己时不会持有自己。

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

接口本身也是一种类型：接口类型的值可以是任何具备这些方法的值，调用方法时调用的就是这个值自己的方法。

```asuka
let shapes: [Shape] = [Circle(radius: 1), Square(side: 2)]
let areas = shapes.map(s => s.area())
```

泛型函数和泛型类型可以接收类型参数。类型参数的约束可以是程序自己的接口，也可以是 `Equatable`、`Comparable` 和 `Hashable`（当结构体和枚举的字段都满足后三者之一时，它们自身也就满足），多个约束用 `&` 连接。类型实参必须具备的东西，泛型代码里它的值也都能用：接口的方法和字段会针对每一个编译出的具体类型直接调用。

```asuka
fn sortByArea<T: Shape>(xs: [T]) -> [T] = xs.sorted((a, b) => a.area() < b.area())

fn largestOnce<T: Shape & Hashable>(xs: [T]) -> [T] { … }

struct Labeled<T: Shape> {
  item: T
  fn label() -> string = "{item.area()}"
}
```

只要具备接口的方法和字段，类型实参就满足这个接口：可以是结构体或枚举（成员不携带值的枚举也可以），可以是要求了这些方法和字段的接口，也可以是带有相应约束的另一个类型参数。`T: Error` 可以读取错误的 `message`。

### 控制流

`if` 和 `match` 也可以作为值使用。作为值时，`if` 必须带 `else`，代码块的值就是它的最后一个表达式：

```asuka
let label = if unread > 0 { "{unread} new" } else { "Inbox" }
let icon = match (online, unread) {
  (false, _) => "wifi.slash"
  (true, 0) => "tray"
  _ => "tray.full"
}
```

元组模式（`(true, 0)`）用各自的模式分别匹配元组的每个元素。

`for` 可以遍历数组、map 的 `(key, value)` 对，或者一个区间：`a..<b` 是从 `a` 到 `b` 但不包括 `b` 的整数，`a..b` 则包括 `b`（`0..<3` 就是 `[0, 1, 2]`；`for` 遍历区间时不会真的创建数组）。`while cond { … }` 在 cond 成立时一直循环；`break` 跳出循环，`continue` 直接进入下一轮。

`if let` 和 `while let` 取出可选值里的值，或者匹配一个模式，只有取得到时才执行代码块：`if let u = user { … }`、`while let job = queue.pop() { … }`、`if let .ready(v) = state { … }`。它们的条件可以是一串用逗号分隔的子句，`let` 和布尔条件可以任意交替。每个子句都能用到前面子句绑定的名字；所有子句都成立时才执行代码块（子句按顺序求值，遇到第一个不成立的就停下）：

```asuka
if let user = session.user, user.isAdmin, let team = teams[user.teamId] {
  Text("{team.name}'s admin")
} else {
  Text("Not an admin")
}
```

`let … else` 要么绑定一个值，要么离开。可选值为 none（或者模式不匹配）时执行 `else` 代码块，而这个代码块必须离开：`return`、`throw`、`break`、`continue` 或 `panic(…)`（如果它可能继续往下执行，编译器会报错）。在它之后，这个名字就是其中的值，而不再是可选值：

```asuka
fn summary(id: string) -> string {
  let message = cache[id] else { return "(deleted)" }
  let .ready(body) = message.body else { return message.subject }
  return "{message.subject}: {body}"
}
```

`throw`、`return`、`break` 和 `continue` 也可以出现在需要值的地方：它们不提供值，而是直接离开（它们的类型是 `never`，可以放在任何类型的位置上）。写在 `??` 后面，就表示遇到 none 时该怎么办：

```asuka
let user = cache[id] ?? throw MailError.missing(id)
for line in lines {
  let n = line.toInt() ?? continue
  total += n
}
```

它们同样可以用作 `if` 的分支、`match` 的分支，或者闭包的主体：`x => x ?? throw Error("empty")`。

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

语句有：`let`、`var`、`let … else`、赋值（`=`、`+= -= *= /= %=`、`&= |= ^= <<= >>=`、`??=`）、`if` 和 `if let`、`for x in xs`（数组或集合）、`for (k, v) in m`、`for (i, x) in xs.enumerated()`、`while` 和 `while let`、`match`、`return`、`break`、`continue`、`throw`、`do { } catch { }`、`defer { }`、`emit`（见[控制流](#控制流)），以及 `fn` 声明（见[函数与闭包](#函数与闭包)）。

### 双向绑定

控件既要显示一个值，也要修改它。`bind` 把状态交给控件，两件事就都能做：

```asuka
state query = ""
state notify = true

TextField { bind text: query; placeholder: "Search" }
Toggle { bind on: notify; title: "Notifications" }
```

不用 `bind` 时，控件只负责报告变化：`TextField { text: name; on change(t) { name = t.trim() } }`。

组件的 prop 也可以这样绑定，只要组件用 `event <prop>Change` 报告它：一个有 `prop value` 和 `event valueChange(value: T)` 的组件，写 `bind value: when` 就会把状态传进去，并在组件发出变化时写回：

```asuka
component Rating {
  prop value = 0
  event valueChange(value: int)

  Stepper {
    value: float(value)
    on change(v) { emit valueChange(int(v)) }
  }
}

component Review {
  state stars = 3

  Rating { bind value: stars }
}
```

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

effect 会立即运行，之后每当它读取过的内容变化时再次运行。task 是异步的（见下文）：组件出现时启动；圆括号里的内容变化时重新启动（并取消上一次运行）；组件消失时被取消。`app` 代码块里也可以写它们：它们属于应用本身，在应用运行期间一直有效（比如一个刷新循环，或者一个跟随应用状态的设置：`effect { setColorScheme(scheme) }`）。

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
- `all(a, b)` 同时发起多个调用，并等待全部结果；`race(a, b)` 取第一个完成的结果，并取消其余的；此外还有 `sleep(300ms)` 和 `withTimeout(5s) { … }`，后者的值就是代码块的值（`let page = try await withTimeout(5s) { return try await fetch(url) }`），超时就抛出错误。它们是结构化的：被它们取消的工作会先停下、执行完自己的 `defer`，它们才返回或抛出错误。`await never()` 什么都不等，只等任务结束：适合在组件显示期间一直持有某样东西的 task（`defer { release() }; await never()`），或只在读者离开时才结束的流。
- `spawn { … }` 在另一个线程上运行一段代码，用于会拖住主线程的工作（解析、图片处理）。这段代码只能使用值，不能使用状态、store、视图、原生对象或 C 指针，也不能使用保存着它们的文件级 `let`（比如一个数据库）：如果用了，编译器会报错。在 Web 上，它运行在页面的一个 Web Worker 里；Worker 只会自己生成文件里的值，不会生成其他东西。
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

在表达式后面写 `catch`，可以给它一个备用值：正常时得到表达式的值；表达式里有地方抛出错误时，得到 `catch` 代码块的值（和 `if` 一样，是它的最后一个表达式），除非这个代码块直接离开：

```asuka
let port = try parsePort(text) catch e {
  log(e.message)
  80
}
let size = try await fileSize(path) catch { return }
try save(draft) catch { failed = true }   // 作为语句：不需要提供值
```

它的子句和 `do` 的一样：`catch`、`catch e`、`catch e: ParseError`、`catch MailError.server(code)`，可以连写多个，由第一个接住错误的子句处理。没有子句接住的错误会继续往外抛，所以所在的函数必须标注 `throws`，这和 `try` 的规则相同。如果子句能接住所有错误，整个表达式就不会抛出错误，因此可以用在 `try` 不能出现的地方：组件的 `let` 里（`let port = try parsePort(text) catch { 80 }`，会随着 `text` 的变化保持更新），或者不允许抛出错误的闭包里。它也可以配合 `try await` 使用，以及配合作为值的 `throw`：`let user = cache[id] ?? throw MailError.missing(id)`。

错误的 `message` 是它的 `message` 字段、它的 `fn message() -> string`，或者它的类型名和成员名。`Error("…")` 用一条消息创建一个错误。`catch` 永远不会捕获 task 的取消：取消会一直走到 task 的结尾，并在途中执行各个 `defer`。

`Error` 也可以像 `catch` 那样按错误类型的成员来匹配：`match e { MailError.offline => …; MailError.status(code) => …; _ => e.message }`（`Error` 可能是任何错误，所以匹配要以 `_` 结尾）。

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

`animate(() => …)` 和 `animate(anim, () => …)` 是同样的写法，只是用闭包代替代码块。`repeat: Animation.forever` 会一直重复，直到属性再次改变。`animating()` 表示当前所做的改动是否处在动画中。`onFrame(t => …)` 在显示器绘制的每一帧调用闭包，传入这一帧的时间，直到调用它返回的闭包，或组件消失为止：`let stop = onFrame(t => tick(t))`。`frames()` 把同样的帧作为 `Stream<Duration>` 提供，只在被读取时才会绘制帧：在任务里写 `for await t in frames() { … }`，循环结束或任务消失时就会停止。

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
| `onBack { … }` | 用户从组件所在的页面、弹出面板或气泡弹窗返回时询问：`true` 表示接管（页面留下），`false` 表示放行 |
| `onLifecycle(l => …)` | 组件存在期间：接收应用进入前台、后台，以及内存警告（一个 `Lifecycle`） |
| `alert(title, message:, actions:)` | 立即显示一个警告框；`actions` 是一组 `.button`（没有时只有一个 OK）。`Alert` 元素则在它的 `open` 为 true 时显示一个 |
| `snackbar(message, action:, duration:, onDismiss:)` | 在窗口底部显示一条简短消息，可以带一个操作（撤销）；返回一个用来关闭它的闭包 |
| `setColorScheme(.dark)` | 应用的外观：`.light`、`.dark`，或 `.system`（跟随平台） |
| `environment()`、`screenEnvironment()` | 尺寸、安全区域、文字大小、平台等（会被跟踪）：页面或弹出面板自己的，以及整个屏幕的 |
| `mainWindow()` | 主窗口的 `Ref`：在任何地方都可以 `show()`、`hide()`、`focus()` …… |

`useKeys` 和这些 `on…` 注册写在组件的主体里（或 app 代码块里）。app 代码块还可以把它们当作事件来处理：`on openURL(url) { … }`、`on files(paths) { … }`、`on share(items) { … }`、`on quit(e) { … }`、`on secondInstance(args, dir) { … }`、`on foreground { … }`、`on background { … }`。它的第一个 `Window` 是主窗口（见[导航](navigation.md#窗口)）。

`asuka/assets` 在应用编译时读取包的 `assets/` 目录里的文件，就像 Go 的 `go:embed`：`assets.text("welcome.md")` 是它的文本，`assets.bytes("logo.png")` 是它的字节。文件不存在就是编译错误。

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

`screen` 是一种组件，它的根是导航栈中的一个页面：它的代码块接收页面的 prop（`title`、`largeTitle` ……）和插槽（`toolbar`、`bottomToolbar`、`primary`）。`push(…)` 和 `pop()` 作用于最近的导航栈，`present(…)` 和 `dismiss()` 作用于弹出面板（sheet）。`Split` 是一个由 `Destination` 组成的侧边栏（可以放在 `Sidebar` 分区里），旁边是选中目的地的导航栈。

有了 `Router`，每个页面都有一个地址。用模式声明的页面是可路由的，参数有类型；调用它就得到它的地址，并在编译时检查：

```asuka
app {
  Router(initial: "/inbox") {
    Route("/inbox", layout: .stack, title: "Inbox") {
      Inbox
      Message
    }
  }
}

screen Inbox at "" {
  title: "Inbox"
  Button("The first") { href: Message(id: 1) } // /inbox/1
}

screen Message(id: int) at ":id" {
  title: "Message {id}"
  Button("Back to the inbox") { on press { navigate("/inbox") } }
}
```

详见[导航](navigation.md)。

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

创建模块和调用它都可能失败（当前平台可能没有它的代码），所以两者都要加 `try`：`let h = try Haptics()`、`try h.impact(.light)`。`asuka native generate` 会生成各平台需要实现的 Swift、Kotlin、JavaScript 和 C 代码。详见[原生代码](native.md)。

C 则可以直接调用。`extern` 声明用 C 的类型写明一个 C 函数接收什么、返回什么，Asuka 把每个声明都当作自己的函数来看待（`Int32` 就是 `int`，`CString` 就是 `string`）：

```asuka
extern type Db with release db_close
extern fn db_open(path: CString) -> Db?
extern fn db_close(db: Db)
extern fn db_each(db: Db, sql: CString, row: fn(CString?) -> bool) -> Int32

extern struct Point {
  x: Float32
  y: Float32
}
```

`extern type` 是 C 给出的一个指针，最后一个引用消失时由它的 release 函数释放；`extern struct` 按值传递；传给 C 的函数就是回调。`asuka.toml` 的 `[c]` 写明应用要编译和链接哪些 C 代码。详见[在 Asuka 中调用 C](c.md)。

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

`asuka test` 在假宿主上运行包里的测试：它会像各平台那样对视图进行布局，按每个字符固定宽度来测量文字，而且只有在没有其他东西可以运行时，时间才会前进（测试里的 `sleep(5s)` 不花任何时间）。`mount` 返回应用，供你驱动和检查：`tap`、`longPress`、`type(_, into:)`、`submit`、`toggle`、`slide`、`select`、`swipe(_, action:)`、`refresh`、`scrollTo`、`back`、`dismiss`、`pickDate`、`pickColor`、`expect`、`expectNo`、`texts()`、`disabled(text)`、`until`、`waitFor`、`idle`；`in(.navigationBar)`、`in(.sidebar)` 和 `nextTo(label)` 缩小文字所指的范围；有路由器时，还有 `mount(…, at: "/inbox/1")`、`href`、`history()`、`goBack()`、`goForward()`。详见[测试](testing.md)。

## 内存与性能

没有垃圾回收器，所以也就没有停顿。值采用引用计数，并在写入时复制。状态、store、effect 和 task 都属于创建它们的组件，并随它一起销毁。

- 一次变化只会运行读取了变化内容的那些绑定，也只把变化了的 prop 发给平台。这些 prop 打成一个批次发送，然后运行布局，再把各视图的位置和尺寸（frame）打成另一个批次发送。
- 发布构建会和核心一起编译成 C，所以你发布的应用里没有解释器。
- 列表只构建屏幕上可见的部分，并复用单元格。

## 关键字

`app as async await bind break catch component continue defer do effect
else emit enum event extern false fn for if import in interface key let load
match native none on prop provide pub return screen self spawn state
store struct style task test throw throws true try type use var while`
