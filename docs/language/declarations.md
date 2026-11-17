# Declarations

```asuka
let pi = 3.14159             // a constant
var total = 0                // a variable (in functions and handlers)
state count = 0              // state: a value the UI follows
```

Types are inferred; write one where it helps: `let ids: [int] = []`.

`type` names a type, at the top of a file: `type Pair<A, B> = (A, B)`,
`type Check = fn(string) -> bool`. It's another name for the same type
(an alias): a `Pair<int, string>` is an `(int, string)`.

## Structs

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

A struct is made by calling it with its fields, by name or in order;
fields with a default may be left out. Its methods see its fields by
their names; they can't change it (it's a value): one that would
returns a new one. `let` in a struct's body is a constant of the
type (`Font.title`), which `.title` names wherever a `Font` is
expected. `static fn` in a struct's or an enum's body is a function of
the type's, with no `self`: called on the type with named arguments and
defaults as any function (`Money.of(dollars: 2)`), or as `.of(…)`
wherever the type is expected.

```asuka check
struct Money {
  cents: int

  static fn of(dollars: int, cents: int = 0) -> Money = Money(cents: dollars * 100 + cents)
}

let price: Money = .of(dollars: 4, cents: 50)
```

A struct may take type parameters, inferred where it's made:

```asuka check
struct Page<T> {
  items: [T]
  next: string? = none
}

let page = Page(items: [1, 2, 3])     // a Page<int>
```

A struct pattern matches a struct's fields by their names, in `match`
and `if let`: `y` alone binds the field `y`, `y: py` binds it as `py`,
`x: 0` matches a value, and the fields it leaves out match anything.
Patterns nest:

```asuka
match line {
  Line(from: Point(x: 0, y: 0), dashed: true) => "dashed, from the origin"
  Line(from: Point(x: 0, y), to) => "from {y} on the y axis to {to.x}"
  Line(dashed) => "a line"
}
```

A struct or an enum takes operators from its methods: `fn add(o: T) ->
R` is `a + b`, `fn sub(o: T) -> R` (or `minus`) is `a - b`, and `fn
compare(o: T) -> int` (negative, zero or positive) is `<`, `<=`, `>`
and `>=`, `sorted()`, and `T: Comparable`
([Functions](functions.md#interfaces-and-generics)). `a += b` is `a = a +
b` with them, when `add` gives `a`'s type back, on a var, state, a field
or an element (state changes once). That's how `std/time`'s `Time` adds a
`Duration` and subtracts another `Time`:

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

A struct isn't ordered field by field: what its order means is its
`compare`'s to say.

## Enums

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

Where the type is known, a case is written `.name`. `match` must cover
every case (or end with `_ =>`); patterns bind payloads (`.from(p)`, by
their labels too: `.server(code: c)`), match literals (`0`, `"x"`),
several cases (`.a | .b`), nest (`.from(Person(vip: true))`) and take
guards (`.from(p) if p.vip =>`). The compiler follows nested patterns
when it checks that a match covers everything, and says what's missing
as patterns: `match doesn't cover .key(_, false)`. A case's labeled payload with a zero value
(`false`, `0`, `""`, an empty array or map, `none`, an enum's first case)
may be left out where it's made: `.button(title: "Copy", run: copy)`
leaves `Action.button`'s `icon`, `role`, `disabled` and `checked` so.

A case's payload may hold the enum itself, so an enum can be a tree:

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
