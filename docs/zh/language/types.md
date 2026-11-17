# 值与类型

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

字符串的花括号里可以放任意表达式：`"{count} items"`、`"Total: {price * qty}"`。`\{` 表示一个花括号字符；`r"…"` 是原始字符串，没有转义也没有插值；`"""…"""` 可以跨多行（第一行为空时不算在内，结束引号所在那一行的缩进也会去掉）。不是数字、布尔值或字符串的值，显示为文本时是这样的：枚举成员显示为它的名字，可选值显示为 `none` 或其中的值，错误显示为它的消息，颜色按写法显示（`#0a84ff`、`.accent`），时长以毫秒显示（`1500ms`），其他值显示为 JSON（JSON 表示不了的值，比如元组或函数，显示为它的类型：`<(int, string)>`）。

要读取元组里的元素，需要把它拆开：`let (count, label) = pair`、`for (i, x) in xs.enumerated()`，或者在 `match` 里用元组模式（见[控制流](control-flow.md#控制流)）。

map 的键和集合的元素可以是整数、字符串、布尔值、时长、颜色，或者成员不携带值的枚举（也就是满足 `Hashable` 的类型）。

## 运算符

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

## 内置方法

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

`min`、`max`、`abs`、`float(n)` 和 `int(x)` 是函数；其余的（`math.sqrt`、`math.pow`、`math.pi` ……）在 `std/math` 里。

`toInt()` 和 `toFloat()` 读的是整个字符串，读不出来就是 none：可选的正负号，接着是数字，前后不能有别的东西（`" 12"` 是 none，先 `trim()`）。`toInt()` 只认整数（`"+7"`、`"-42"`）；`toFloat()` 还认小数和指数（`"2.5"`、`".5"`、`"1e-3"`），以及浮点数打印出来的样子（`"Infinity"`、`"NaN"`）。无论用户的语言是什么，小数点都是 `.`。

## 值语义

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
