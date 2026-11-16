# Styles

Every element takes the style props: layout first (`direction`,
`justify`, `align`, `gap`, `padding`, `margin`, `width`, `height`,
`grow`, `shrink`, `absolute`, `top`, `grid`, ...), then look
(`background`, `radius`, `border`, `opacity`, `shadow`, `font`,
`color`, `lines`, ...). See [Layout and style](../layout.md).

```asuka
style card {
  padding: 16; radius: 12; gap: 8
  background: .secondaryBackground
}

View { style card; background: .accent }   // later props win
```

A `pub style` is another package's too: `style theme.card`, its values
read in the package that declares it.

An element that sets no style looks like the platform's own.

A container (`View`, `Row`, `Column`, `SafeArea`) takes **variants**:
blocks of style props named after an interaction state, laid over its
style while it's in that state (CSS's `:hover` and `:active`). They are
`hovered`, `pressed`, `dragging` (while it's dragged) and `dragOver`
(while a drag it takes is over it), laid in that order, so `pressed`
wins over `hovered`. A variant sets only the props it names, and its
values are bindings like any prop's:

```asuka
View {
  padding: 16; radius: 12; background: .secondaryBackground
  hovered { borderColor: .accent }
  pressed { background: .separator; opacity: 0.8 }
  on press { open() }
}
```

A view reports a state only once something asks for it, so one with no
`pressed` variant keeps the platform's own pressed look. A variant may
take a `style` too (`pressed { style raised }`). Other views follow a
view's states through its name ([Naming a view](#naming-a-view)):
`View as card { … }` then `card.hovered` (or `card.pressed`,
`card.dragging`, `card.dragOver`) in any binding, a child's (a group's
hover) or a sibling's. What isn't a style uses the handlers: `on
pressedChange(p)`, `on hoverChange(h)`.

## Naming a view

Some things are done to a view rather than set on it: play a video,
run script in a page, put text in a field, scroll a list. A `Ref` is a
view's handle: `ref: r` on any element puts its view in `r`, and
`Element as name` declares one for the rest of the block it's in.

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

A Ref holds its view while the view is shown, and nothing before or
after: a command then does nothing, and a state is false. Its states
are tracked, so a binding that reads one follows it:

| On | Ref's members |
|---|---|
| any view | `hovered`, `pressed`, `dragging`, `dragOver` (bools, tracked); `focus()`, `blur()` |
| `Scroll`, `List` | `scrollTo(x, y, animated: true)` |
| `Video` | `play()`, `pause()`, `seek(seconds)` |
| `WebView` | `try await eval(script)` (its value, as JSON), `run(script)` (without waiting), `goBack()`, `goForward()`, `reload()`, `stopLoading()` |
| `TextField` | `insert(text)`, `format(.bold, on)`, `toggleFormat(.italic)`, `setLink(url)` (a rich field's selection) |

A Ref can be passed along (`shareFiles(paths, from: button)`,
`a11yFocus(field)`), and kept in a file's `let` for views of several
components.
