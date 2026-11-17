# 声明

```asuka
let pi = 3.14159             // 常量
var total = 0                // 变量（在函数和处理函数中）
state count = 0              // 状态：界面会跟随变化的值
state shown = count          // 在被写入之前跟随 count：跟随的状态
```

类型会自动推断；在有助于阅读的地方可以写出来：`let ids: [int] = []`。

`type` 在文件顶层给类型起一个名字：`type Pair<A, B> = (A, B)`、`type Check = fn(string) -> bool`。它只是同一个类型的别名：`Pair<int, string>` 就是 `(int, string)`。

## 结构体

```asuka check
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

```asuka check
struct Money {
  cents: int

  static fn of(dollars: int, cents: int = 0) -> Money = Money(cents: dollars * 100 + cents)
}

let price: Money = .of(dollars: 4, cents: 50)
```

结构体可以带类型参数，创建时会自动推断：

```asuka check
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

结构体和枚举可以从它们的方法获得运算符：`fn add(o: T) -> R` 就是 `a + b`，`fn sub(o: T) -> R`（或 `minus`）就是 `a - b`，`fn compare(o: T) -> int`（负数、零或正数）就是 `<`、`<=`、`>` 和 `>=`，也让 `sorted()` 和 `T: Comparable` 可用（见[函数](functions.md#接口与泛型)）。只要 `add` 返回的还是 `a` 的类型，`a += b` 就是借助这些方法的 `a = a + b`，可以用在 var、状态、字段和元素上（状态只改变一次）。`std/time` 的 `Time` 就是这样加上一个 `Duration`、减去另一个 `Time` 的：

```asuka check
struct Money {
  cents: int

  fn add(o: Money) -> Money = Money(cents: cents + o.cents)
  fn compare(o: Money) -> int = cents - o.cents
}

let total = Money(cents: 250) + Money(cents: 99)
let over = total > Money(cents: 300)

fn withTip(bill: Money) -> Money {
  var paid = bill
  paid += Money(cents: 100)
  return paid
}
```

结构体不会逐个字段地排序：它的顺序是什么意思，由它的 `compare` 来决定。

## 枚举

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

```asuka check
enum Expr {
  num(int)
  add(Expr, Expr)

  fn eval() -> int = match self {
    .num(n) => n
    .add(a, b) => a.eval() + b.eval()
  }
}
```
