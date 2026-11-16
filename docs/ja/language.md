# Asuka 言語

Asuka のアプリは、Asuka という言語で書きます。各プラットフォーム純正のウィジェットを使う UI のために作られた、小さな言語です。静的型付けで、リリースビルドでは事前にコンパイルされます。開発中はその場でリロードされるので、保存した変更は数ミリ秒で画面に反映され、値もネイティブのビューもすべてそのまま残ります。

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

このページでは言語のすべてを説明します。要素そのもの（`Text`、`Button`、`List`、`Stack` など）については、専用のページ[コンポーネント](components.md)を参照してください。

## ファイル、パッケージ、インポート

ソースファイルの拡張子は `.asuka` です。ソースファイルを入れたディレクトリが一つの**パッケージ**になります。同じパッケージのファイルどうしは、何もインポートしなくても互いの宣言が見えます。ほかのパッケージから見えるのは、`pub` を付けた宣言だけです。

```asuka
import std/http                 // 標準ライブラリ
import platform/camera          // プラットフォーム SDK
import @acme/charts               // 依存パッケージ（asuka add @acme/charts）
import ./components               // このパッケージと同じ階層にあるパッケージ
import ./model.{Message, Folder}  // パッケージ名を付けずに使う名前
import ./theme as t               // 短い別名
import ../model                   // 1 つ上のディレクトリにあるパッケージ
```

パッケージの名前は、パッケージ名を通して使います（`http.get(url)`、`components.Card("Hi")`）。パッケージ名はパスの最後の部分です。最後の部分が名前として使えないパッケージ（`@acme/line-charts`）は、`as` で名前を付けてインポートします。プレリュード（組み込みの型、要素、関数）はインポートする必要がありません。`std/...` と `platform/...` のパッケージは CLI に付属していて、一覧は[標準ライブラリとプラットフォーム SDK](stdlib.md)にあります。ほかのパッケージには `std/` や `platform/` で始まる名前を付けられません。

文と宣言は行末で終わります。一行に複数書くときは `;` で区切ります。演算子、カンマ、開き括弧で終わる行は、次の行に続きます。

コメントは `// …` と `/* … */` です。`///` のコメントはその下にある宣言のドキュメントになり、エディターに表示されます。

### アプリ、プログラム、ライブラリ

パッケージはエントリーポイントになるか、ライブラリになるかのどちらかです。`app { }` ブロックを持つパッケージはアプリで、このブロックが表示する内容になります（[ナビゲーション](#ナビゲーション)を参照）。ファイルで `fn main()` を宣言しているパッケージは**プログラム**、つまりコマンドラインツールです。`main` は引数を受け取らず（引数は `os.args()` で読みます）、値も返さず（`os.exit(code)` で終了コードを指定して終了します）、`async` にすることもエラーを投げることもできます。`main` が終わるとプログラムも終わります。どちらも持たないパッケージはライブラリで、ほかのパッケージからインポートされます。1 つのパッケージがアプリとプログラムの両方になることはできず、プログラムはコンポーネントを宣言できません。ビューを表示しないからです。エントリーポイントを持つのは、アプリやプログラムを構成するパッケージだけです。インポートしたパッケージにある `main` は、ほかの関数と変わりません。詳しくは[プログラム](programs.md)を参照してください。

```asuka
import std/os

fn main() {
  print("Hello, {os.args().first() ?? "world"}!")
}
```

## 値と型

| 型 | 値 |
|---|---|
| `int` | 64 ビット整数：`42`、`1_000`、`0xff`、`0b1010`、`0o17` |
| `float` | 64 ビット浮動小数点数：`3.14`、`1e-3` |
| `bool` | `true`、`false` |
| `string` | UTF-8 のテキスト：`"Hello, {name}"` |
| `bytes` | 生のバイト列 |
| `[T]` | 配列：`[1, 2, 3]`、`[]` |
| `[K: V]` | マップ（挿入した順に保持）：`["a": 1]`、`[:]` |
| `Set<T>` | セット（重複しない値の集まり、挿入した順に保持）：セットが求められる場所では `["a", "b"]` |
| `(A, B)` | タプル：`(1, "one")` |
| `T?` | オプショナル：`T` か `none` |
| `Duration` | `300ms`、`5s`、`2min`、`1h`（ほかに `us`、`ns`） |
| `Color` | `#7c3aed`、`#7c3aed80`、セマンティックカラー（`.label`、`.accent` など） |
| `Length` | `100`（ポイント）、`50%`、`.auto` |
| `fn(A) -> B` | 関数とクロージャ |

文字列の波括弧の中には、任意の式を書けます（`"{count} items"`、`"Total: {price * qty}"`）。`\{` は波括弧そのものを表します。`r"…"` はエスケープも埋め込みもない raw 文字列で、`"""…"""` は複数行にまたがる文字列です（1 行目が空ならその行は含まれず、閉じ引用符の行のインデントも取り除かれます）。数値、ブール値、文字列以外の値は、テキストとして表示すると次のようになります。列挙型のケースはその名前、オプショナルは `none` か中の値、エラーはそのメッセージ、それ以外は JSON です。

マップのキーとセットの要素には、整数、文字列、ブール値、値を持たないケースだけの列挙型が使えます（つまり `Hashable` な型です）。

### 演算子

結合の強いものから順に並べています。同じ行の演算子は優先順位が同じで、左から順に結合します（`??` だけは右から）。

| 演算子 | 意味 |
|---|---|
| `!x`、`-x`、`~x` | 否定、符号反転、整数のビット反転 |
| `<<`、`>>` | 整数のビットを左へ、右へシフト（右シフトは符号を保つ） |
| `*`、`/`、`%`、`&` | 乗算、除算、剰余、ビット AND |
| `+`、`-`、<code>&#124;</code>、`^` | 加算（文字列や配列の連結にも使う）、減算、ビット OR、ビット XOR |
| `..`、`..<` | 範囲：`1..3`、`0..<n` |
| `==`、`!=`、`<`、`<=`、`>`、`>=` | 比較（連ねて書くことはできません） |
| `&&` | かつ |
| <code>&#124;&#124;</code> | または |
| `??` | オプショナルの中の値。`none` ならデフォルト値 |

ビット演算子が取るのは整数だけです。64 ビット以上シフトするとすべてのビットが押し出されます（負の数の `>>` は -1 になります）。負の数でシフトするとエラーになります。これらの演算子と `b[i]`（バイトを整数として読む）を使えば、Bluetooth のキャラクタリスティックや NFC のレコードといったバイナリデータを読み書きできます。

```asuka
let raw = b[1] | b[2] << 8                         // リトルエンディアンの uint16
let celsius = if raw & 0x8000 != 0 { raw - 0x10000 } else { raw }
let out = bytes([flags, value & 0xff, value >> 8])
```

`int` の演算はラップアラウンドします。`+`、`-`、`*` の結果が最大の `int`（2⁶³ − 1）を超えると、64 ビットの 2 の補数どおりに最小値の側から続きます（`9223372036854775807 + 1` は `-9223372036854775808`）。ハッシュ、チェックサム、バイナリプロトコルが求めるのはまさにこの動作で、しかも演算のたびにオーバーフローを検査するコストがかかりません。ゼロ除算（`/`、`%`）はエラーを投げます。範囲外の浮動小数点数に `int(x)` を使うと、いちばん近い `int` になります（NaN は `0`）。

### 組み込みメソッド

文字列、バイト列、配列、セット、マップ、オプショナルにはメソッドがあります。値そのものを変更するメソッド（`push`、`insert`、`removeAt`、`removeWhere`、`pop`、`clear`、`sort`、`remove`）は、`var` か状態に対してだけ呼べます。

| 型 | メソッド |
|---|---|
| `[T]` | `count()`、`count(where)`、`isEmpty()`、`first()`、`last()`、`get(i)`（`T?` を返す）、`push(x)`、`insert(x, at: i)`、`removeAt(i)`、`removeWhere(p)`、`pop()`、`clear()`、`map(f)`、`filter(p)`、`any(p)`、`all(p)`、`index(p)`（`int?` を返す）、`find(p)`、`contains(x)`、`sum()`、`reversed()`、`slice(from, to)`（`to` を省くと末尾まで）、`prefix(n)`、`suffix(n)`、`sorted()`、`sorted(by)`、`sort()`、`sort(by)`、`enumerated()`、`joined(separator:)`（文字列の配列）、`+` |
| `Set<T>` | `count()`、`isEmpty()`、`contains(x)`、`insert(x)`（新しく加わったかを返す）、`remove(x)`（あったかを返す）、`union(s)`、`intersection(s)`、`subtracting(s)`、`isSubset(of: s)`、`isSuperset(of: s)`。`Set(xs)` は配列の重複しない要素、`Set<T>()` は空のセット |
| `[K: V]` | `m[k]`（`V?` を返す）、`m[k] = v`、`count()`、`isEmpty()`、`has(k)`、`get(k)`、`remove(k)`、`keys()`、`values()`（挿入した順） |
| `string` | `count()`（文字数）、`byteCount()`、`isEmpty()`、`upper()`、`lower()`、`trim()`、`trimStart()`、`trimEnd()`、`trimPrefix(s)`、`trimSuffix(s)`、`contains(s)`、`hasPrefix(s)`、`hasSuffix(s)`、`replace(old, new, count:)`（count を指定しなければすべて置き換え）、`split(sep, count:)`、`fields()`（空白で区切った単語）、`lines()`、`find(s)`、`findLast(s)`、`findAny(chars)`（バイト単位の位置。見つからなければ none）、`slice(from, to)`（文字単位）、`byteSlice(from, to)`（バイト単位：find が返す位置）、`padStart(width, with:)`、`padEnd(width, with:)`、`toInt()`、`toFloat()`、`repeat(n)`、`chars()`、`bytes()`、`+` |
| `bytes` | `b[i]`（0 から 255 の `int`。範囲外はエラー）、`count()`、`isEmpty()`、`get(i)`（`int?` を返す）、`slice(from, to)`、`concat(b)`、`string()`（UTF-8 として読み、不正な部分は U+FFFD になる）、`hex()` と `hex(separator)`（`"04 a1"`）。`bytes()` は空のバイト列、`bytes([0x04, 0xa1])` はそのバイト列、`s.bytes()` は文字列の UTF-8 |
| `int` | `float()`、`abs()`、`padded(width)`（`7.padded(2)` は `"07"`） |
| `float` | `fixed(digits)`、`rounded()`、`floor()`、`ceil()`、`int()`（0 の方向へ切り捨て）、`abs()` |
| `Color` | `opacity(a)`：元の不透明度の a 倍で描く（セマンティックカラーはセマンティックカラーのまま）。`hex()`（`"#0a84ff"`。セマンティックカラーはその名前：`".accent"`） |
| `Duration` | `nanoseconds()`、`milliseconds()`（整数）、`seconds()`、`minutes()`、`hours()`（浮動小数点数）、`rounded(to:)`、`truncated(to:)`、`+`、`-`、`* n`（整数でも浮動小数点数でも：`1.5 * 1h`）、`/ n`、`/ d`（別の Duration：何個分か。浮動小数点数） |
| `Length` | `* n`（`width: 50% * 2`：長さを拡大縮小） |
| `T?` | `isNone()`、`isSome()` |

`min`、`max`、`abs`、`float(n)`、`int(x)` は関数です。それ以外（`math.sqrt`、`math.pow`、`math.pi` など）は `std/math` にあります。

### 値のセマンティクス

文字列、配列、セット、マップ、構造体、列挙型は**値**です。代入したり引数に渡したりすると、見かけ上はコピーが渡されます。コピーのコストはわずかです。コピーどうしは、どれかが変更されるまで同じメモリを共有します。また、一か所からしか持たれていない値は、その場で変更されます。

```asuka
var a = [1, 2, 3]
var b = a
b.push(4)        // a は [1, 2, 3] のまま
```

ストア、クロージャ、タスク、ビューは**参照**です。コピーしても同じものを指します。

null はありません。ないかもしれない値はオプショナルで表します。

```asuka
let user: User? = none
if let u = user { Text(u.name) }
let name = user?.name ?? "Guest"
let first = items.first()!        // none ならクラッシュする。none になりえないときだけ書く
```

`?.` のあとは、チェーンの残りの部分も none で止まります。`user?.boss.name` は `string?` です。

### 宣言

```asuka
let pi = 3.14159             // 定数
var total = 0                // 変数（関数とハンドラーの中で）
state count = 0              // 状態：UI が追従する値
```

型は推論されます。書いたほうがわかりやすいところでは明示します（`let ids: [int] = []`）。

`type` は、ファイルのトップレベルで型に名前を付けます（`type Pair<A, B> = (A, B)`、`type Check = fn(string) -> bool`）。これは同じ型の別名（エイリアス）なので、`Pair<int, string>` は `(int, string)` そのものです。

### 構造体

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

構造体は、フィールドを名前付きか順番どおりに渡して呼び出すことで作ります。デフォルト値のあるフィールドは省略できます。メソッドからはフィールドを名前でそのまま参照できます。ただし構造体は値なので、メソッドで変更することはできません。変更するメソッドは、新しい構造体を返します。構造体の本体に書いた `let` はその型の定数（`Font.title`）になり、`Font` が求められる場所ならどこでも `.title` と書けます。

構造体や列挙型の本体に書いた `static fn` は、`self` を持たない、その型の関数です。ほかの関数と同じく名前付き引数とデフォルト値を使って型に対して呼び出せ（`Money.of(dollars: 2)`）、その型が求められる場所では `.of(…)` とも書けます。

```asuka
struct Money {
  cents: int

  static fn of(dollars: int, cents: int = 0) -> Money = Money(cents: dollars * 100 + cents)
}

let price: Money = .of(dollars: 4, cents: 50)
```

構造体は型パラメーターを取れます。型引数は、作るときに推論されます。

```asuka
struct Page<T> {
  items: [T]
  next: string? = none
}

let page = Page(items: [1, 2, 3])     // Page<int>
```

`match` と `if let` では、構造体パターンで構造体のフィールドを名前でマッチさせられます。`y` とだけ書くとフィールド `y` を束縛し、`y: py` なら `py` という名前で束縛し、`x: 0` は値にマッチします。書かなかったフィールドは何にでもマッチします。パターンは入れ子にできます。

```asuka
match line {
  Line(from: Point(x: 0, y: 0), dashed: true) => "dashed, from the origin"
  Line(from: Point(x: 0, y), to) => "from {y} on the y axis to {to.x}"
  Line(dashed) => "a line"
}
```

構造体は、メソッドから演算子を得ます。`fn add(o: T) -> R` は `a + b` に、`fn sub(o: T) -> R`（または `minus`）は `a - b` に、`fn compare(o: T) -> int`（負、ゼロ、正）は `<`、`<=`、`>`、`>=` になります。`std/time` の `Time` が `Duration` を足し、別の `Time` を引けるのはこのためです。

```asuka
struct Money {
  cents: int

  fn add(o: Money) -> Money = Money(cents: cents + o.cents)
  fn compare(o: Money) -> int = cents - o.cents
}

let total = Money(cents: 250) + Money(cents: 99)
let over = total > Money(cents: 300)
```

### 列挙型

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

型がわかっている場所では、ケースを `.name` と書きます。`match` はすべてのケースを網羅しなければなりません（または `_ =>` で終えます）。パターンでは、ケースの値を束縛したり（`.from(p)`。ラベルでも書けます：`.server(code: c)`）、リテラルにマッチさせたり（`0`、`"x"`）、複数のケースをまとめたり（`.a | .b`）、入れ子にしたり（`.from(Person(vip: true))`）、ガードを付けたり（`.from(p) if p.vip =>`）できます。`match` がすべての値を網羅しているかを調べるとき、コンパイラーは入れ子のパターンの中までたどり、足りないものをパターンの形で示します（`match doesn't cover .key(_, false)`）。ケースのラベル付きの値のうち、ゼロ値（`false`、`0`、`""`、空の配列やマップ、`none`、列挙型の最初のケース）があるものは、作るときに省略できます。`.button(title: "Copy", run: copy)` は `Action.button` の `icon`、`role`、`disabled`、`checked` を省略しています。

ケースの値には、その列挙型自身も持たせられます。そのため、列挙型で木構造を表せます。

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

### 関数とクロージャ

```asuka
fn area(width: float, height: float = 1) -> float {
  return width * height
}

fn double(x: int) -> int = x * 2      // 式が一つだけの関数

area(3, height: 4)
let squares = [1, 2, 3].map(x => x * x)
let sorted = people.sorted((a, b) => a.age < b.age)
```

引数は位置で渡すものが先、名前で渡すものが後です。クロージャは `x => expr`、`(a, b) => expr`、`x => { statements }` と書きます。クロージャは、キャプチャした変数を周りのコードと共有します。どちらかが変更すれば、もう一方にもその変更が見えます。

```asuka
var n = 0
let inc = () => { n += 1 }
inc()
inc()                // n は 2
```

状態も同じように共有されます。使うものをコピーするのは `spawn` だけです（値だけを、実行を始めた時点のまま受け取ります）。別のスレッドで動くためです。

`fn` は、関数、ハンドラー、クロージャ、メソッドの本体の中でも宣言できます。クロージャと同じように周りのものを参照でき、自分自身や、同じブロックで隣に宣言した `fn` を呼び出せます（隣の `fn` からも呼び出せます）。

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

ほかの関数と同じく値としても使えます（`items.map(walk)`）。引数は順番どおりに渡し、ラベルやデフォルト値は使えません。ジェネリックにもできません。

決して戻らない関数（必ずエラーを投げる、panic する、プログラムを終了する）は、`os.exit` のように `-> never` でそれを示します。その呼び出しは `return` と同じようにブロックを終わらせ、値が来る場所ならどこにでも書けます：`let port = text.toInt() ?? fail("not a port")`。

関数が自分自身を呼び出すときは、こう書きます。クロージャを変数に入れ、その変数を通して自分を呼び出す（`var walk = …; walk = n => { … walk(c) … }`）と、クロージャは変数を持ち、変数はクロージャを持つことになります。このような循環参照を回収する仕組みはないので、どちらも解放されません。コンパイラーはこれを警告します。ローカルな `fn` なら、自分自身を持たずに自分を呼び出せます。

### インターフェースとジェネリクス

インターフェースはメソッドとフィールドの集まりです。それらを持っている型は、そのインターフェースを満たします（何も宣言する必要はありません）。

```asuka
interface Shape { fn area() -> float }

fn total<T>(items: [T], size: fn(T) -> float) -> float {
  var sum = 0.0
  for i in items { sum += size(i) }
  return sum
}
```

インターフェースはそれ自体が型でもあります。インターフェース型の値には、そのメソッドを持つどんな値でも入れられ、メソッドを呼ぶとその値のメソッドが呼ばれます。

```asuka
let shapes: [Shape] = [Circle(radius: 1), Square(side: 2)]
let areas = shapes.map(s => s.area())
```

ジェネリックな関数と型は型パラメーターを取ります。型パラメーターの制約には、プログラム自身のインターフェースと、`Equatable`、`Comparable`、`Hashable` が使えます（構造体と列挙型は、フィールドが後の三つを満たしていれば自身も満たします）。複数の制約は `&` でつなぎます。型引数が持っていなければならないものは、ジェネリックなコードの中でその値から使えます。インターフェースのメソッドとフィールドは、コンパイルされる具体的な型ごとに直接呼び出されます。

```asuka
fn sortByArea<T: Shape>(xs: [T]) -> [T] = xs.sorted((a, b) => a.area() < b.area())

fn largestOnce<T: Shape & Hashable>(xs: [T]) -> [T] { … }

struct Labeled<T: Shape> {
  item: T
  fn label() -> string = "{item.area()}"
}
```

ジェネリック関数の型引数は、引数と、戻り値に期待される型から推論されます。推論できないときは `f<T>(…)` で明示します：`json.decode<[Note]>(text)`、`flag.parse<Options>(args)`。

インターフェースのメソッドとフィールドを持っていれば、型引数はそのインターフェースを満たします。構造体や列挙型（値を持たないケースだけの列挙型も含む）、それらを求めるインターフェース、同じように制約された別の型パラメーターがそうです。`T: Error` ならエラーの `message` を読めます。

### 制御フロー

`if` と `match` は値としても使えます。値として使う `if` には `else` が必要で、ブロックの値はその最後の式です。

```asuka
let label = if unread > 0 { "{unread} new" } else { "Inbox" }
let icon = match (online, unread) {
  (false, _) => "wifi.slash"
  (true, 0) => "tray"
  _ => "tray.full"
}
```

タプルパターン（`(true, 0)`）は、タプルの要素ごとにそれぞれのパターンでマッチさせます。

`for` で回せるのは、配列、マップの `(key, value)` の組、範囲です。`a..<b` は `a` から `b` の手前までの整数、`a..b` は `b` を含む整数です（`0..<3` は `[0, 1, 2]`。範囲を `for` で回しても配列は作られません）。`while cond { … }` は cond が成り立つあいだ繰り返します。`break` はループを抜け、`continue` は次の周回に進みます。

`if let` と `while let` は、オプショナルの中の値を取り出すか、パターンにマッチさせ、取り出せたときだけブロックを実行します（`if let u = user { … }`、`while let job = queue.pop() { … }`、`if let .ready(v) = state { … }`）。条件は、カンマで区切った節の並びにもできます。`let` とブール値の条件は、どんな順番で並べてもかまいません。各節では、それより前の節で束縛した名前を使えます。ブロックが実行されるのはすべての節が成り立ったときです（節は順に評価され、成り立たない節が出たところで止まります）。

```asuka
if let user = session.user, user.isAdmin, let team = teams[user.teamId] {
  Text("{team.name}'s admin")
} else {
  Text("Not an admin")
}
```

`let … else` は、値を束縛するか、その場を離れます。オプショナルが none のとき（またはパターンにマッチしないとき）は `else` のブロックが実行され、このブロックは必ず離れなければなりません。離れ方は `return`、`throw`、`break`、`continue`、`panic(…)` のいずれかです（先へ進めてしまう場合はコンパイラーがエラーにします）。この文より後では、名前はオプショナルではなく中の値を指します。

```asuka
fn summary(id: string) -> string {
  let message = cache[id] else { return "(deleted)" }
  let .ready(body) = message.body else { return message.subject }
  return "{message.subject}: {body}"
}
```

`throw`、`return`、`break`、`continue` は、値を書く場所にも置けます。値を返す代わりにその場を離れます（型は `never` で、どんな型の場所にも当てはまります）。`??` の後ろに書けば、none のときにどうするかを表せます。

```asuka
let user = cache[id] ?? throw MailError.missing(id)
for line in lines {
  let n = line.toInt() ?? continue
  total += n
}
```

`if` の分岐、`match` のアーム、クロージャの本体にも同じように書けます（`x => x ?? throw Error("empty")`）。

## コンポーネント

コンポーネントは UI の部品です。パラメーターはヘッダーで宣言し、それ以外はすべて本体に書きます。

```asuka
component MessageRow(message: Message) {
  prop compact = false            // デフォルト値付きの名前付きプロパティ
  prop accessory: View? = none    // スロット：呼び出し側が渡すビュー
  event archive                   // コンポーネントが知らせるイベント
  state expanded = false          // コンポーネント自身の状態

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

使い方は組み込みの要素と同じです。ヘッダーのパラメーターは丸括弧の中に、それ以外はすべてブロックの中に書きます。

- プロパティ：`name: value`
- ハンドラー：`on name { … }`
- 双方向バインディング：`bind name: target`
- スタイル：`style name`
- 子

```asuka
MessageRow(m) {
  compact: true
  accessory: Badge(3)
  on archive { mailbox.archive(m.id) }
}
```

どれも書く場所は一つだけです。ヘッダーのパラメーターをブロックの中で設定することも、プロパティを丸括弧の中で渡すこともできません。

子を受け取るコンポーネントは `prop children: View` を宣言します。呼び出し側のブロックの中身がそこに入ります。名前付きのスロットは、ブロックの中に `name { … }` と書きます（`toolbar { … }`）。

`emit name(args)` はイベントを発生させ、呼び出し側の `on name(args) { }` がそれを処理します。誰も処理しないイベントは何もしません。イベント名を値として読むと、呼び出し側がそれを処理しているかどうかになります（`if click { … }`：たとえばトレイアイコンは、呼び出し側がクリックを引き受けなければクリックでメニューを開きます）。

ひとつの要素だけを表示するコンポーネント（本体のビューがその要素ひとつ、またはそうなっている別のコンポーネント）は、スタイルのプロパティも受け取ります。呼び出し側が渡したスタイルはその要素に付き、コンポーネント自身がそこに設定した値より優先されます。

```asuka
component Card(title: string) {
  Column {
    padding: 16; radius: 12; background: .secondaryBackground
    Text(title)
  }
}

Card("Plan") { padding: 8; width: 240 }   // padding は 8、radius は 12
```

### コンポーネントの本体は宣言

コンポーネントの本体は、一度実行したら忘れられるコードでは**ありません**。一行一行が、ずっと成り立ち続けることを宣言しています。

- `state x = …` は、コンポーネントが持つ状態です。
- `let y = …` は、読んだものから導かれる値で、常に最新に保たれます（読まれたときにだけ計算され、読んだものが変わったときにだけ計算し直されます）。
- すべての要素のすべてのプロパティの値も、同じように最新に保たれます。`Text("Count: {count}")` は `count` が変わると変わり、ほかのものは何も変わりません。
- `if`、`for`、`match` は、条件が示すものを表示します。今も、これから先も。

`count` が変わったときに再び実行されるのは、それを読んでいるもの、つまりその `Text` のテキストと `if` だけです。コンポーネントが再実行されることはなく、プラットフォームのビューも、何かが変わった箇所にしか手が入りません。

### ハンドラーと文

コードが実行されるのは、ハンドラー（`on press { … }`）、関数、`task` と `effect` のブロック、そしてテストの中です。そこで状態を読むと現在の値が得られ、書き込むとそれを読んでいるものが更新されます。

```asuka
on press {
  count += 1
  todos.push(Todo(title: draft))
  todos[0].done = true      // todos への書き込み：todos[0] を読んでいる行だけが更新される
  draft = ""
}
```

ハンドラーの中での書き込みは、ハンドラーが終わったとき（または `await` に達したとき）にまとめて画面に反映されます。

文には次のものがあります：`let`、`var`、`let … else`、代入（`=`、`+= -= *= /= %=`、`&= |= ^= <<= >>=`、`??=`）、`if` と `if let`、`for x in xs`（配列かセット）、`for (k, v) in m`、`for (i, x) in xs.enumerated()`、`while` と `while let`、`match`、`return`、`break`、`continue`、`throw`、`do { } catch { }`、`defer { }`、`emit`（[制御フロー](#制御フロー)を参照）、`fn` の宣言（[関数とクロージャ](#関数とクロージャ)を参照）。

### 双方向バインディング

コントロールは値を表示し、その値を変更します。`bind` で状態を渡すと、その両方を行えます。

```asuka
state query = ""
state notify = true

TextField { bind text: query; placeholder: "Search" }
Toggle { bind on: notify; title: "Notifications" }
```

`bind` を使わない場合、コントロールは変更を知らせるだけです（`TextField { text: name; on change(t) { name = t.trim() } }`）。

コンポーネントのプロパティも、コンポーネントが `event <prop>Change` でそれを知らせるなら、同じようにバインドできます。`prop value` と `event valueChange(value: T)` を持つコンポーネントに `bind value: when` と書くと、状態を渡し、コンポーネントが変更を発行したときに書き戻します。

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

### ビューのリスト

```asuka
for todo in todos key todo.id {
  TodoRow(todo) { on delete { todos.removeWhere(t => t.id == todo.id) } }
}
```

`for` はキーごとに行を一つ保ちます。項目が変わった行は、自分のバインディングだけを更新します。新しい項目が加わると行が作られ、項目が削除されるとその行は破棄されます。移動した行は作り直されず、移動されるだけです。`List` の直下では、行は画面に表示されている間しか存在しません。そのため 100,000 件のリストでも、コストは見えている分だけです。

### ストア

ストアは、状態とそれを変更する関数をまとめたもので、それを使うビューのあいだで共有されます。

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

ストアは構造体と同じように作り（`Mailbox()`）、作ったコンポーネントまたはストアのものになります。`provide` でその下にあるすべてから使えるようにし、`use` で取り出します。

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

ファイルの最上位に書いた `state` はアプリのもので、アプリ全体で一つだけです。

ストアは構造体と同じように型パラメータを取れます。型は引数か、期待される型から推論されます。

```asuka
store Kept<T>(key: string, initial: T) {
  pub state value: T = initial
}

let size = Kept("size", 17.0)                 // Kept<float>
let tags: Kept<[string]> = Kept("tags", [])
```

ストアの fn の中では、`self` はそのストア自身です。ストアが作るものに渡せます。

### エフェクトとタスク

```asuka
effect { storage.set("draft", draft) }        // すぐに実行し、draft が変わるたびにまた実行する
task { try? await mailbox.refresh() }         // コンポーネントが表示されている間に一度だけ
task(query) { results = await search(query) } // query が変わるたびにもう一度
```

エフェクトはすぐに実行され、読んだものが変わるたびに再び実行されます。タスクは非同期です（後述）。コンポーネントが現れると始まり、丸括弧の中身が変わると前回の実行をキャンセルしてやり直し、コンポーネントがなくなるとキャンセルされます。`app` ブロックにも書けます。アプリ自身のもので、アプリが動いている間ずっと有効です（更新のループや、アプリの状態に追従する設定：`effect { setColorScheme(scheme) }`）。

## 非同期処理

待つ必要のある処理（ネットワーク、ファイル、タイマー）は `async` で、それを待つのは `await` です。

```asuka
async fn loadThread(id: string) throws -> Thread {
  let (messages, people) = try await all(api.messages(id), api.people(id))
  let html = await spawn { renderMarkdown(messages) }
  return Thread(messages, people, html)
}
```

- 非同期関数を呼ぶには `await` が必要です。ハンドラー、タスク、テストでは `await` できますが、コンポーネントの本体とその中の `let` ではできません。
- **メインスレッド。** すべてのコードはメインスレッドで動きます。`await` で待っている間は、ほかの処理が動けます。状態を変更できるのはメインスレッドだけです。
- `all(a, b)` は複数の呼び出しを同時に実行し、すべての結果を待ちます。`race(a, b)` は最初に終わった結果を取り、残りをキャンセルします。ほかに `sleep(300ms)` と `withTimeout(5s) { … }` があります。`withTimeout` の値はブロックの値で（`let page = try await withTimeout(5s) { return try await fetch(url) }`）、時間切れになるとエラーを投げます。これらは構造化されていて、キャンセルした処理が止まり、その `defer` が実行されてから戻るかエラーを投げます。`await never()` はタスクが終わるまで待つだけです。コンポーネントが表示されている間、何かを持ち続けるタスク（`defer { release() }; await never()`）や、読み手がいなくなったときにだけ終わるストリームに使います。
- `spawn { … }` はブロックを別のスレッドで実行します。メインスレッドを止めてしまうような処理（パース、画像処理）に使います。中で使えるのは値だけで、状態、ストア、ビュー、ネイティブのオブジェクト、C のポインター、そしてそれらを持つファイルの `let`（データベースなど）は使えません。使っていればコンパイラーが教えてくれます。Web では、ページの Web Worker の一つで実行されます。Worker はファイルの値だけを自分で作り、それ以外は作りません。
- `for await message in socket.messages { … }` はストリーム、つまり `Stream<T>` を読みます。時間とともに届く値です。`Stream(send => { … })` で作ります。ストリームが最初に読まれたときにブロックがタスクとして動き出し、`send(x)` のたびに値が一つ届きます。ブロックが戻るとストリームは終わり、ブロックが投げたエラーでストリームも失敗します。ループを抜けると（`break`、エラー、オーナーがなくなる）ブロックはキャンセルされます。ストリームの読み手は一つだけです。

  ```asuka
  fn levels() -> Stream<float> = Stream(send => {
    try await battery.watch(changed: level => send(level))
  })
  ```
- `live(initial, source)` は、ストリームが最新に保つ値で、それを読む関数として使います。バインディングの中で読むと、最初にそう読まれたときから最後の読み手がいなくなるまで `source()` のストリームに従います（そのあとストリームはキャンセルされ、誰も見ていないのにプラットフォームを監視し続けることはありません）。それ以外の場所で読むと、最後に分かっている値になります。ネットワークの状態のように、プラットフォームが変えるものをパッケージが提供するときに使います。

  ```asuka
  let level = live(1.0, levels)
  pub fn batteryLevel() -> float = level()
  ```
- **キャンセル。** タスクは、それを始めたものに属します。それがなくなると（ユーザーが画面を離れたときなど）、タスクはキャンセルされます。そのとき待っている `await` でタスクは終わり、`defer` が実行され、エラーは報告されません。

### 読み込むデータ

`load` は非同期の結果をコンポーネントの中に保持します。

```asuka
screen Profile(id: string) {
  let user = load api.user(id)   // id が変わると読み込み直す

  match user {
    .pending => Spinner()
    .failed(e) => ErrorView(e) { on retry { user.reload() } }
    .ready(u) => ProfileCard(u)
  }
}
```

`Loading<T>` は `.pending`、`.ready(value)`、`.failed(error)` のいずれかです。`.value` は最新の値で（読み込み直している間も残るので、画面がちらつきません）、`.reload()` はもう一度実行します。引数が読んでいるものが変わると再び実行され、古くなりかけている実行はキャンセルされます。

## エラー

失敗する可能性のある関数には `throws` を付け、呼び出すときには `try` が必要です。

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

let n = try? parse(text)   // エラーが投げられたら none
```

式の後ろに `catch` を書くと、代わりの値を用意できます。普段は式の値になり、式の中でエラーが投げられたときは `catch` のブロックの値（`if` と同じく、最後の式）になります。ブロックがその場を離れる場合は別です。

```asuka
let port = try parsePort(text) catch e {
  log(e.message)
  80
}
let size = try await fileSize(path) catch { return }
try save(draft) catch { failed = true }   // 文として使う：値は要らない
```

節は `do` のものと同じです（`catch`、`catch e`、`catch e: ParseError`、`catch MailError.server(code)`）。複数を続けて書くことができ、エラーを受け取った最初の節が実行されます。どの節も受け取らなかったエラーはそのまま先へ投げられるので、`try` と同じく、囲んでいる関数に `throws` が必要です。すべてのエラーを受け取る節があれば、式全体がエラーを投げなくなるので、`try` を置けない場所でも使えます。たとえばコンポーネントの `let`（`let port = try parsePort(text) catch { 80 }` は `text` が変わるたびに更新されます）や、エラーを投げてはいけないクロージャの中です。`try await` とも、値としての `throw` とも組み合わせられます（`let user = cache[id] ?? throw MailError.missing(id)`）。

エラーの `message` は、`message` フィールド、`fn message() -> string`、または型とケースの名前です。`Error("…")` はメッセージからエラーを作ります。タスクのキャンセルは `catch` では捕まりません。キャンセルはタスクの最後まで進み、その途中で `defer` を実行していきます。

`Error` は、`catch` と同じように、エラー型のケースでも照合できます：`match e { MailError.offline => …; MailError.status(code) => …; _ => e.message }`（`Error` はどんなエラーでもありうるので、`_` で終えます）。

ハンドラーやタスクで捕まえなかったエラーは、そのコンポーネントを囲むいちばん近い `Boundary` に届きます。`Boundary` がなければアプリのもの（開発中は画面に表示し、リリースビルドではログに記録します）に届きます。

```asuka
Boundary {
  Feed()
  on error(e) { Text("Something went wrong: {e.message}") }
}
```

## スタイル

すべての要素がスタイルのプロパティを受け取ります。まずレイアウト（`direction`、`justify`、`align`、`gap`、`padding`、`margin`、`width`、`height`、`grow`、`shrink`、`absolute`、`top`、`grid` など）、次に見た目（`background`、`radius`、`border`、`opacity`、`shadow`、`font`、`color`、`lines` など）です。詳しくは[レイアウトとスタイル](layout.md)を参照してください。

```asuka
style card {
  padding: 16; radius: 12; gap: 8
  background: .secondaryBackground
}

View { style card; background: .accent }   // 後に書いたプロパティが優先される
```

`pub style` は別のパッケージからも使えます。`style theme.card` と書き、その値は宣言したパッケージの中で評価されます。

スタイルを何も指定しない要素は、プラットフォーム純正のものと同じ見た目になります。

コンテナ（`View`、`Row`、`Column`、`SafeArea`）には**バリアント**を書けます。インタラクションの状態の名前を付けたスタイルのプロパティのまとまりで、コンテナがその状態にある間、元のスタイルの上に重ねられます。CSS の `:hover` や `:active` にあたるものです。状態は `hovered`、`pressed`、`dragging`（ドラッグされている間）、`dragOver`（受け付けるドラッグが上にある間）の 4 つで、この順に重なるため、`pressed` は `hovered` より優先されます。バリアントが変えるのは書いたプロパティだけで、その値はほかのプロパティと同じくバインディングです。

```asuka
View {
  padding: 16; radius: 12; background: .secondaryBackground
  hovered { borderColor: .accent }
  pressed { background: .separator; opacity: 0.8 }
  on press { open() }
}
```

ビューが状態を報告するのは、どこかでその状態が使われたときだけです。そのため `pressed` バリアントを持たないビューは、プラットフォーム自身の押下時の見た目をそのまま保ちます。バリアントの中でも `style` を使えます（`pressed { style raised }`）。ほかのビューは名前を通してビューの状態に追従できます（[ビューに名前を付ける](#ビューに名前を付ける)を参照）。`View as card { … }` と書き、任意のバインディングで `card.hovered`（あるいは `card.pressed`、`card.dragging`、`card.dragOver`）を読みます。子ビュー（グループ全体のホバー）でも兄弟のビューでも使えます。スタイル以外の反応にはハンドラーを使います：`on pressedChange(p)`、`on hoverChange(h)`。

## ビューに名前を付ける

ビューには、値を設定するのではなく、ビューに対して何かをしたいこともあります。動画を再生する、ページでスクリプトを実行する、テキストフィールドに文字を入れる、リストをスクロールする、といった操作です。`Ref` はビューのハンドルです。任意の要素に `ref: r` と書くとそのビューが `r` に入り、`Element as name` と書くと、そのブロックの残りの部分で使えるハンドルが宣言されます。

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

Ref がビューを持つのは、ビューが表示されている間だけです。その前後は空なので、コマンドは何もせず、状態はすべて false になります。状態は追跡されるので、それを読むバインディングは状態の変化に追従します。

| 対象 | Ref のメンバー |
|---|---|
| すべてのビュー | `hovered`、`pressed`、`dragging`、`dragOver`（bool、追跡される）、`focus()`、`blur()` |
| `Scroll`、`List` | `scrollTo(x, y, animated: true)` |
| `Video` | `play()`、`pause()`、`seek(seconds)` |
| `WebView` | `try await eval(script)`（スクリプトの値を JSON で）、`run(script)`（完了を待たない）、`goBack()`、`goForward()`、`reload()`、`stopLoading()` |
| `TextField` | `insert(text)`、`format(.bold, on)`、`toggleFormat(.italic)`、`setLink(url)`（リッチなフィールドの選択範囲に対して） |

Ref はほかへ渡せます（`shareFiles(paths, from: button)`、`a11yFocus(field)`）。ファイルの `let` に置けば、複数のコンポーネントのビューで使えます。

## アニメーション

`animate { … }` はブロックをすぐに実行し、その中でフレームや描画される内容（不透明度、背景、角の半径、シェイプのトリム）に加えた変更をネイティブにアニメーションさせます。`animate(.spring) { … }` は `Animation` を受け取ります。長さ、カーブ、遅延、繰り返し回数、スプリングの減衰と初速です。`await` すると、アニメーションが完了してから戻ります。`await` しなくてもかまいません。

```asuka
on press {
  animate { expanded = !expanded }
}
on press {
  await animate(Animation(duration: 500ms, curve: .spring)) { offset = 0 }
  done = true
}
```

`animate(() => …)` と `animate(anim, () => …)` は、ブロックの代わりにクロージャを渡す同じ書き方です。`repeat: Animation.forever` は、プロパティが再び変わるまで繰り返します。`animating()` は、いま加えている変更がアニメーションするかどうかを返します。`onFrame(t => …)` は、ディスプレイがフレームを描くたびにそのフレームの時刻を渡してクロージャを呼びます。戻り値のクロージャを呼ぶか、コンポーネントがなくなると止まります：`let stop = onFrame(t => tick(t))`。`frames()` は同じフレームを `Stream<Duration>` として返し、読まれている間だけフレームを描かせます。タスクの中で `for await t in frames() { … }` と書けば、ループが終わるかタスクがなくなったときに止まります。

## 描画

`Canvas(g => …)` は `Graphics` を使って、左上を原点にポイント単位で描画します。レイアウトが済むと描画し、サイズや読み取った内容が変わると描き直します。`Shape(fn(width, height) -> Path)` は、プラットフォームが塗りと線を描く輪郭です（`fill`、`stroke`、`lineWidth`、`trimEnd`、`dash` など）。

```asuka
Canvas(g => {
  let ring = g.path().arc(g.width / 2, g.height / 2, 40, 0, 6.28318)
  g.stroke(ring, .accent, StrokeStyle(width: 8, cap: .round))
  g.text("{percent}%", g.width / 2, g.height / 2, CanvasText(size: 20, align: .center))
}) { height: 100 }
```

`Path` はメソッドで組み立てます。どのメソッドもパス自身を返します：`Path().move(x, y)`、`line`、`quad`、`cubic`、`rect`、`roundedRect`、`ellipse`、`arc(cx, cy, radius, start, end, clockwise: true)`（ラジアン、x 軸の正の向きから）、`close()`。`g.path()` はキャンバス自身が持つ空のパスで、描画のたびに使い回されます。`Graphics` が有効なのは描画関数の実行中だけです：`width`、`height`、`fill(path, color)`（`LinearGradient` や `RadialGradient` も渡せます）、`stroke(path, color またはグラデーション, style)`、`save()`、`restore()`、`translate`、`scale`、`rotate(degrees)`、`opacity`、`clip(path)`、`transform(a, b, c, d, tx, ty)`、`evenOdd(on)`、`text(s, x, y, style)`。

`Shader(shader("""…"""))` は GPU のプログラムが描く箱です。プログラムは Kage で書き、アプリと一緒にコンパイルされます（プログラムの誤りはコンパイラーのエラーになります）。uniform は名前で設定します（`uniforms: [Uniform(name: "Level", value: [0.5]), colorUniform("Tint", .accent)]`）。

## システムの機能

| 関数 | 動作 |
|---|---|
| `share(text)` | テキストやリンクの共有シートを表示する |
| `try await shareFiles(paths, title:, from: ref)` | ファイルの共有シートを、あるビューから（ポップオーバーが指す位置に）表示する。シートが閉じると戻る |
| `haptic(.success)` | プラットフォームの触覚フィードバック（`Haptic`） |
| `copy(text)` | テキストをクリップボードに入れる |
| `openURL(url)` | url を扱えるアプリで開く |
| `announce(text)` | スクリーンリーダーにテキストを読み上げさせる |
| `a11yFocus(ref)` | スクリーンリーダーのフォーカスをそのビューに移す |
| `try await webCookies(url)` | Web ビューが url について保持している Cookie |
| `useKeys([Shortcut(…)])` | コンポーネントがある間の、ウィンドウのキー操作 |
| `onOpenURL(u => …)`、`onOpenFiles(paths => …)`、`onShare(items => …)` | コンポーネントがある間に、アプリが開かれたときに渡されたもの、またはほかのアプリから共有されたもの |
| `quit()`、`relaunch()` | ユーザーが「終了」を選んだときと同じようにアプリを終了し（再び起動し）ます。先にアプリに確認します。デスクトップのみ |
| `closeWindow()` | コードがあるウィンドウを確認せずに閉じます |
| `onCloseRequest(fn() -> bool)`、`onQuit(e => …)`、`onSecondInstance((args, dir) => …)` | コンポーネントがあるあいだ：そのウィンドウが閉じる前の確認、アプリの終了の直前（`e.cancel()`）、アプリの再起動を受け取ります |
| `onBack { … }` | コンポーネントがある画面、シート、ポップオーバーからユーザーが戻ろうとしたときに問い合わせられます。`true` なら引き受け（画面は残る）、`false` なら戻らせます |
| `onLifecycle(l => …)` | コンポーネントがある間、アプリがフォアグラウンドやバックグラウンドに移ったこと、メモリ警告（`Lifecycle`）を受け取ります |
| `alert(title, message:, actions:)` | その場でアラートを表示します。`actions` は `.button` の配列です（なければ OK だけ）。`Alert` 要素は、`open` が true の間アラートを表示します |
| `snackbar(message, action:, duration:, onDismiss:)` | ウィンドウの下部に短いメッセージを、アクション（元に戻す）付きで表示します。閉じるためのクロージャーを返します |
| `setColorScheme(.dark)` | アプリの外観：`.light`、`.dark`、`.system`（プラットフォームのもの） |
| `environment()`、`screenEnvironment()` | サイズ、セーフエリア、文字サイズ、プラットフォームなど（追跡されます）。画面やシート自身のものと、画面全体のもの |
| `mainWindow()` | メインウィンドウの `Ref`。どこからでも `show()`、`hide()`、`focus()` などができます |

`useKeys` とこれらの `on…` による登録は、コンポーネントの本体（またはアプリのブロック）に書きます。アプリのブロックでは、これらをイベントとしても扱えます：`on openURL(url) { … }`、`on files(paths) { … }`、`on share(items) { … }`、`on quit(e) { … }`、`on secondInstance(args, dir) { … }`、`on foreground { … }`、`on background { … }`。ブロックの最初の `Window` がメインウィンドウです（[ナビゲーション](navigation.md#ウィンドウ)）。

`std/embed` は、アプリのコンパイル時にパッケージのファイルを読んでアプリに組み込みます。どのプラットフォームでも、実行時に読み込むことなくそのファイルを使えます。

```asuka
import std/embed

struct Config { api: string; retries: int }

let welcome = embed.text("welcome.md")         // string
let logo = embed.bytes("art/logo.png")         // bytes
let config: Config = embed.json("config.json") // コンパイル時に検査される
let posts = embed.texts("posts/*.md")          // [パス: テキスト]
let icons = embed.files("icons/**/*.svg")      // [パス: バイト列]
```

パスは文字列リテラルで、パッケージのディレクトリ（`asuka.toml` のある場所）からの相対パスです。その外は指せません。グロブには `*`、`?`、`[abc]`、`**`（任意の階層のディレクトリ）が使え、名前がドットで始まるファイルには一致しません。`embed.json` は `json.decode` と同じようにファイルを読みますが、それをコンパイル時に行います。型に合わないファイルは、行と列を示すコンパイルエラーになり、実行時のエラーにはなりません。ファイルがない場合や、グロブに一致するファイルがない場合もコンパイルエラーです。`asuka dev` では、組み込んだファイルを変更するとコードを変更したときと同じようにアプリがその場で再読み込みされます。実行時にプラットフォームが読むファイル（画像、フォント）は `assets/` に置きます（[std/embed](std/embed.md)）。

## ナビゲーション

```asuka
app {
  Tabs {
    Tab("Inbox") { icon: "tray"; Stack { Inbox() } }
    Tab("Settings") { icon: "gear"; Stack { Settings() } }
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

`screen` は、ルートがスタックの画面になるコンポーネントです。ブロックには画面のプロパティ（`title`、`largeTitle` など）とスロット（`toolbar`、`bottomToolbar`、`primary`）を書きます。`push(…)` と `pop()` はいちばん近いスタックに、`present(…)` と `dismiss()` はシートに対して働きます。`Split` は、`Destination`（`Sidebar` のセクションに入れられます）を並べたサイドバーと、その横の選んだ行き先のスタックです。

`Router` を使うと、どの画面にもアドレスが付きます。パターン付きで宣言した画面はルートに載せられ、パラメーターには型があります。その呼び出しがアドレスになり、コンパイル時に検査されます。

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

詳しくは[ナビゲーション](navigation.md)を参照してください。

## ほかの言語のテキスト

`tr.inboxTitle(unread: n)` はアプリのカタログ（`locales/en.json`、`de.json` など）のメッセージを返します。カタログはアプリのコンパイル時にチェックされ、キーや引数が足りなかったり型が違ったりするとエラーになります。バインディングの中で使ったメッセージはアプリの言語に従うので、言語を切り替えると（std/i18n の `i18n.setLocale("de")`）表示も変わります。右から左に書く言語では、アプリ全体が右から並びます。詳しくは[多言語対応](i18n.md)を参照してください。

## ネイティブコード

プラットフォームにしかできないことは、ネイティブパッケージに入れます。Asuka で宣言し、各プラットフォームの言語で実装します。

```asuka
pub native interface Haptics {
  fn impact(style: Feedback)
}

pub native component MapView(region: Region) {
  prop pins: [Pin] = []
  event regionChanged(region: Region)
}
```

モジュールを作ることも呼び出すことも失敗する可能性がある（そのプラットフォームにコードがないかもしれない）ので、どちらにも `try` を付けます（`let h = try Haptics()`、`try h.impact(.light)`）。`asuka native generate` は、各プラットフォームが実装する Swift、Kotlin、JavaScript、C のコードを生成します。詳しくは[ネイティブコード](native.md)を参照してください。

C は直接呼び出せます。`extern` 宣言では、C の関数が何を受け取り何を返すかを C の型で書きます。Asuka からは、それぞれが Asuka 自身の型に見えます（`Int32` は `int`、`CString` は `string`）。

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

`extern type` は C が渡すポインターで、最後の参照がなくなると release で解放されます。`extern struct` は値渡しで受け渡しされ、C に渡した関数はコールバックになります。アプリがコンパイルしてリンクする C は、`asuka.toml` の `[c]` に書きます。詳しくは [Asuka から C を呼び出す](c.md)を参照してください。

## テスト

```asuka
test "archiving removes the row" {
  let app = mount(Inbox(), provide: Mailbox.fake([sample]))
  await app.idle()
  app.tap("Archive")
  app.expectNo("Hello")
}
```

`expect(cond)` は cond が false ならテストを失敗させ、何がどこで失敗したかを示します（比較なら両辺の値も示します）。JSON は `std/json` で扱います。`let notes: [Note] = try json.decode(text)` は求められている型として読み込み（`json.decode<[Note]>(text)` とも書けます）、`json.encode(notes)` は JSON に書き出します。ジェネリック関数も自分の `T` を JSON として読み書きできます。その場合、呼び出しごとに `T` は JSON で表せる型でなければならず、コンパイラーが呼び出し箇所でそれをチェックします。

`asuka test` は、パッケージのテストをデバイスなしで実行します。プラットフォームと同じようにビューをレイアウトし、テキストは 1 文字あたり固定の幅で測ります。また、時間はほかに実行できるものが何もないときにしか進みません（テストの中の `sleep(5s)` には時間がかかりません）。`mount` はアプリを返すので、それを操作して確かめます（`tap`、`longPress`、`type(_, into:)`、`submit`、`toggle`、`slide`、`select`、`swipe(_, action:)`、`refresh`、`scrollTo`、`back`、`dismiss`、`pickDate`、`pickColor`、`expect`、`expectNo`、`texts()`、`disabled(text)`、`until`、`waitFor`、`idle`）。`in(.navigationBar)`、`in(.sidebar)`、`nextTo(label)` で、テキストが指すものを絞り込めます。ルーターがあれば、`mount(…, at: "/inbox/1")`、`href`、`history()`、`goBack()`、`goForward()` も使えます。詳しくは[テスト](testing.md)を参照してください。

## メモリとパフォーマンス

ガベージコレクターがないので、停止もありません。値は参照カウントで管理され、書き込むときにコピーされます。状態、ストア、エフェクト、タスクは、それを作ったコンポーネントに属し、コンポーネントと一緒になくなります。

- 変更があっても、実行されるのは変わったものを読んでいるバインディングだけで、プラットフォームに送られるのも変わったプロパティだけで、送るのはフレームごとに一回です。
- リリースビルドは事前にコンパイルされるので、配布するアプリにインタープリターは含まれません。
- リストは画面に見えている分だけを組み立て、セルを再利用します。

## キーワード

`app as async await bind break catch component continue defer do effect
else emit enum event extern false fn for if import in interface key let load
match native none on prop provide pub return screen self spawn state
store struct style task test throw throws true try type use var while`
