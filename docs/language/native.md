# Native code

What only the platform can do goes in a native package, declared in
Asuka and implemented in each platform's own language:

```asuka
pub native interface Haptics {
  fn impact(style: Feedback)
}

pub native component MapView(region: Region) {
  prop pins: [Pin] = []
  event regionChanged(region: Region)
}
```

Making a module and calling it may fail (its platform may have no code
for it), so both take `try`: `let h = try Haptics()`, `try
h.impact(.light)`. `asuka native generate` writes the Swift, Kotlin,
JavaScript and C each platform implements. See [Native
packages](../native.md).

C is called directly. `extern` declarations say what a C function
takes and gives back, in C's types, and Asuka sees each as one of its
own (an `Int32` is an `int`, a `CString` a `string`):

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

An `extern type` is a pointer C gives, freed by its release as the last
reference goes; an `extern struct` crosses by value; a function passed
to C is a callback. `asuka.toml`'s `[c]` says what C the app compiles
and links. See [C from Asuka](../c.md).
