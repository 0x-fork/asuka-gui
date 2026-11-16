# Asuka 言語

Asuka のアプリは、Asuka という言語で書きます。各プラットフォーム純正のウィジェットを使う UI のために作られた、小さな言語です。静的型付けで、リリースビルドでは事前に C へコンパイルされます。開発中は VM の上で動くので、保存した変更は数ミリ秒で画面に反映され、値もネイティブのビューもすべてそのまま残ります。

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
import asuka/http                 // 標準ライブラリ
import @acme/charts               // 依存パッケージ（asuka add @acme/charts）
import ./components               // このパッケージと同じ階層にあるパッケージ
import ./model.{Message, Folder}  // パッケージ名を付けずに使う名前
import ./theme as t               // 短い別名
```

パッケージの名前は、パッケージ名を通して使います（`http.get(url)`、`components.Card("Hi")`）。パッケージ名はパスの最後の部分です。最後の部分が名前として使えないパッケージ（`@acme/line-charts`）は、`as` で名前を付けてインポートします。プレリュード（組み込みの型、要素、関数）はインポートする必要がありません。`asuka/...` のパッケージの一覧は[標準ライブラリ](stdlib.md)にあります。

文と宣言は行末で終わります。一行に複数書くときは `;` で区切ります。演算子、カンマ、開き括弧で終わる行は、次の行に続きます。

コメントは `// …` と `/* … */` です。`///` のコメントはその下にある宣言のドキュメントになり、エディターに表示されます。

## 値と型

| 型 | 値 |
|---|---|
| `int` | 64 ビット整数：`42`、`1_000`、`0xff` |
| `float` | 64 ビット浮動小数点数：`3.14`、`1e-3` |
| `bool` | `true`、`false` |
| `string` | UTF-8 のテキスト：`"Hello, {name}"` |
| `bytes` | 生のバイト列 |
| `[T]` | 配列：`[1, 2, 3]`、`[]` |
| `[K: V]` | マップ（挿入した順に保持）：`["a": 1]`、`[:]` |
| `(A, B)` | タプル：`(1, "one")` |
| `T?` | オプショナル：`T` か `none` |
| `Duration` | `300ms`、`5s`、`2min`、`1h`（ほかに `us`、`ns`） |
| `Color` | `#7c3aed`、`#7c3aed80`、セマンティックカラー（`.label`、`.accent` など） |
| `Length` | `100`（ポイント）、`50%`、`.auto` |
| `fn(A) -> B` | 関数とクロージャ |

文字列の波括弧の中には、任意の式を書けます（`"{count} items"`、`"Total: {price * qty}"`）。`\{` は波括弧そのものを表します。`r"…"` はエスケープも埋め込みもない raw 文字列で、`"""…"""` は複数行にまたがる文字列です（1 行目が空ならその行は含まれず、閉じ引用符の行のインデントも取り除かれます）。数値、ブール値、文字列以外の値は、テキストとして表示すると次のようになります。列挙型のケースはその名前、オプショナルは `none` か中の値、エラーはそのメッセージ、それ以外は JSON です。

マップのキーには、整数、文字列、ブール値、値を持たないケースだけの列挙型が使えます。

### 組み込みメソッド

文字列、バイト列、配列、マップ、オプショナルにはメソッドがあります。値そのものを変更するメソッド（`push`、`insert`、`removeAt`、`removeWhere`、`pop`、`clear`、`sort`、`remove`）は、`var` か状態に対してだけ呼べます。

| 型 | メソッド |
|---|---|
| `[T]` | `count()`、`count(where)`、`isEmpty()`、`first()`、`last()`、`get(i)`（`T?` を返す）、`push(x)`、`insert(x, at: i)`、`removeAt(i)`、`removeWhere(p)`、`pop()`、`clear()`、`map(f)`、`filter(p)`、`any(p)`、`all(p)`、`index(p)`（`int?` を返す）、`find(p)`、`contains(x)`、`sum()`、`reversed()`、`slice(from, to)`、`sorted()`、`sorted(by)`、`sort()`、`sort(by)`、`enumerated()`、`joined(separator:)`（文字列の配列）、`+` |
| `[K: V]` | `m[k]`（`V?` を返す）、`m[k] = v`、`count()`、`isEmpty()`、`has(k)`、`get(k)`、`remove(k)`、`keys()`、`values()`（挿入した順） |
| `string` | `count()`（文字数）、`byteCount()`、`isEmpty()`、`upper()`、`lower()`、`trim()`、`trimStart()`、`trimEnd()`、`trimPrefix(s)`、`trimSuffix(s)`、`contains(s)`、`hasPrefix(s)`、`hasSuffix(s)`、`replace(old, new, count:)`（count を指定しなければすべて置き換え）、`split(sep, count:)`、`fields()`（空白で区切った単語）、`lines()`、`find(s)`、`findLast(s)`、`findAny(chars)`（バイト単位の位置。見つからなければ none）、`slice(from, to)`（文字単位）、`byteSlice(from, to)`（バイト単位：find が返す位置）、`padStart(width, with:)`、`padEnd(width, with:)`、`toInt()`、`toFloat()`、`repeat(n)`、`chars()`、`bytes()`、`+` |
| `bytes` | `count()`、`isEmpty()`、`get(i)`（`int?` を返す）、`slice(from, to)`、`concat(b)`、`string()`（UTF-8 として読み、不正な部分は U+FFFD になる）。`bytes()` は空のバイト列、`s.bytes()` は文字列の UTF-8 |
| `float` | `fixed(digits)`、`rounded()`、`floor()`、`ceil()` |
| `Color` | `opacity(a)`：元の不透明度の a 倍で描く（セマンティックカラーはセマンティックカラーのまま） |
| `Duration` | `nanoseconds()`、`milliseconds()`（整数）、`seconds()`、`minutes()`、`hours()`（浮動小数点数）、`rounded(to:)`、`truncated(to:)`、`+`、`-`、`* n`、`/ n` |
| `T?` | `isNone()`、`isSome()` |

`min`、`max`、`abs`、`float(n)`、`int(x)` は関数です。それ以外（`math.sqrt`、`math.pow`、`math.pi` など）は `asuka/math` にあります。

### 値のセマンティクス

文字列、配列、マップ、構造体、列挙型は**値**です。代入したり引数に渡したりすると、見かけ上はコピーが渡されます。コピーのコストはわずかです。コピーどうしは、どれかが変更されるまで同じメモリを共有します。また、一か所からしか持たれていない値は、その場で変更されます。

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

型がわかっている場所では、ケースを `.name` と書きます。`match` はすべてのケースを網羅しなければなりません（または `_ =>` で終えます）。パターンでは、ケースの値を束縛したり（`.from(p)`）、リテラルにマッチさせたり（`0`、`"x"`）、複数のケースをまとめたり（`.a | .b`）、ガードを付けたり（`.from(p) if p.vip =>`）できます。

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

引数は位置で渡すものが先、名前で渡すものが後です。クロージャは `x => expr`、`(a, b) => expr`、`x => { statements }` と書きます。値は値としてキャプチャし、状態は参照としてキャプチャします。

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

ジェネリックな関数と型は型パラメーターを取ります。制約には `Equatable`、`Comparable`、`Hashable` があり、構造体と列挙型は、フィールドがその制約を満たしていれば自身も満たします。

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

`emit name(args)` はイベントを発生させ、呼び出し側の `on name(args) { }` がそれを処理します。

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

文には次のものがあります：`let`、`var`、代入（`= += -= *= /= %= ??=`）、`if`、`for x in xs`、`for (i, x) in xs.enumerated()`、`while`、`match`、`return`、`break`、`continue`、`throw`、`do { } catch { }`、`defer { }`、`emit`。

### 双方向バインディング

コントロールは値を表示し、その値を変更します。`bind` で状態を渡すと、その両方を行えます。

```asuka
state query = ""
state notify = true

TextField { bind text: query; placeholder: "Search" }
Toggle { bind on: notify; title: "Notifications" }
```

`bind` を使わない場合、コントロールは変更を知らせるだけです（`TextField { text: name; on change(t) { name = t.trim() } }`）。

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

### エフェクトとタスク

```asuka
effect { storage.set("draft", draft) }        // すぐに実行し、draft が変わるたびにまた実行する
task { try? await mailbox.refresh() }         // コンポーネントが表示されている間に一度だけ
task(query) { results = await search(query) } // query が変わるたびにもう一度
```

エフェクトはすぐに実行され、読んだものが変わるたびに再び実行されます。タスクは非同期です（後述）。コンポーネントが現れると始まり、丸括弧の中身が変わると前回の実行をキャンセルしてやり直し、コンポーネントがなくなるとキャンセルされます。

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
- `all(a, b)` は複数の呼び出しを同時に実行し、すべての結果を待ちます。`race(a, b)` は最初に終わった結果を取り、残りをキャンセルします。ほかに `sleep(300ms)` と `withTimeout(5s) { … }` があります。これらは構造化されていて、キャンセルした処理が止まり、その `defer` が実行されてから戻るかエラーを投げます。`await untilCancelled()` はタスクが終わるまで待つだけです。コンポーネントが表示されている間、何かを持ち続けるタスクに使います（`defer { release() }; await untilCancelled()`）。
- `spawn { … }` はブロックを別のスレッドで実行します。メインスレッドを止めてしまうような処理（パース、画像処理）に使います。中で使えるのは値だけで、状態、ストア、ビューは使えません。使っていればコンパイラーが教えてくれます。
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

エラーの `message` は、`message` フィールド、`fn message() -> string`、または型とケースの名前です。`Error("…")` はメッセージからエラーを作ります。タスクのキャンセルは `catch` では捕まりません。キャンセルはタスクの最後まで進み、その途中で `defer` を実行していきます。

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

`animate(() => …)` と `animate(anim, () => …)` は、ブロックの代わりにクロージャを渡す同じ書き方です。`animating()` は、いま加えている変更がアニメーションするかどうかを返します。`onFrame(t => …)` は、ディスプレイがフレームを描くたびにそのフレームの時刻を渡してクロージャを呼びます。戻り値のクロージャを呼ぶか、コンポーネントがなくなると止まります：`let stop = onFrame(t => tick(t))`。`frames()` は同じフレームを `Stream<Duration>` として返し、読まれている間だけフレームを描かせます。タスクの中で `for await t in frames() { … }` と書けば、ループが終わるかタスクがなくなったときに止まります。

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

`useKeys` とこれらの `on…` による登録は、コンポーネントの本体（またはアプリのブロック）に書きます。アプリのブロックでは、これらをイベントとしても扱えます：`on openURL(url) { … }`、`on files(paths) { … }`、`on share(items) { … }`。

## ナビゲーション

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

`screen` は、ルートがスタックの画面になるコンポーネントです。ブロックには画面のプロパティ（`title`、`largeTitle` など）とスロット（`toolbar`）を書きます。`push(…)` と `pop()` はいちばん近いスタックに、`present(…)` と `dismiss()` はシートに対して働きます。詳しくは[ナビゲーション](navigation.md)を参照してください。

## ほかの言語のテキスト

`tr.inboxTitle(unread: n)` はアプリのカタログ（`locales/en.json`、`de.json` など）のメッセージを返します。カタログはアプリのコンパイル時にチェックされ、キーや引数が足りなかったり型が違ったりするとエラーになります。バインディングの中で使ったメッセージはアプリの言語に従うので、言語を切り替えると（asuka/i18n の `i18n.setLocale("de")`）表示も変わります。右から左に書く言語では、アプリ全体が右から並びます。詳しくは[多言語対応](i18n.md)を参照してください。

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

モジュールを作ることも呼び出すことも失敗する可能性がある（そのプラットフォームにコードがないかもしれない）ので、どちらにも `try` を付けます（`let h = try Haptics()`、`try h.impact(.light)`）。`asuka native generate` は、各プラットフォームが実装する Swift、Kotlin、JavaScript、Go のコードを生成します。詳しくは[ネイティブコード](native.md)を参照してください。

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

`expect(cond)` は cond が false ならテストを失敗させ、何がどこで失敗したかを示します（比較なら両辺の値も示します）。JSON は `asuka/json` で扱います。`let notes: [Note] = try json.decode(text)` は求められている型として読み込み（`json.decode<[Note]>(text)` とも書けます）、`json.encode(notes)` は JSON に書き出します。ジェネリック関数も自分の `T` を JSON として読み書きできます。その場合、呼び出しごとに `T` は JSON で表せる型でなければならず、コンパイラーが呼び出し箇所でそれをチェックします。

`asuka test` は、パッケージのテストをフェイクホストで実行します。フェイクホストはプラットフォームと同じようにビューをレイアウトし、テキストは 1 文字あたり固定の幅で測ります。また、時間はほかに実行できるものが何もないときにしか進みません（テストの中の `sleep(5s)` には時間がかかりません）。`mount` はアプリを返すので、それを操作して確かめます（`tap`、`type(_, into:)`、`scroll`、`back`、`expect`、`expectNo`、`idle`）。詳しくは[テスト](testing.md)を参照してください。

## メモリとパフォーマンス

ガベージコレクターがないので、停止もありません。値は参照カウントで管理され、書き込むときにコピーされます。状態、ストア、エフェクト、タスクは、それを作ったコンポーネントに属し、コンポーネントと一緒になくなります。

- 変更があっても、実行されるのは変わったものを読んでいるバインディングだけで、プラットフォームに送られるのも変わったプロパティだけです。プロパティは一つのバッチで送られ、次にレイアウトが実行され、最後にフレームがもう一つのバッチで送られます。
- リリースビルドはコアと一緒に C へコンパイルされるので、配布するアプリにインタープリターは含まれません。
- リストは画面に見えている分だけを組み立て、セルを再利用します。

## キーワード

`app as async await bind break catch component continue defer do effect
else emit enum event extern false fn for if import in interface key let load
match native none on prop provide pub return screen self spawn state
store struct style task test throw throws true try type use var while`
