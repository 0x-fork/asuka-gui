# Animation

`animate { … }` runs its block at once, and the changes it makes to
frames and to what's drawn (opacity, background, a corner's radius, a
shape's trim) animate natively. `animate(.spring) { … }` takes an
`Animation`: its duration, curve, delay, repetitions, a spring's
damping and velocity. Awaited, it returns once the animation is
complete; it needn't be.

```asuka
on press {
  animate { expanded = !expanded }
}
on press {
  await animate(Animation(duration: 500ms, curve: .spring)) { offset = 0 }
  done = true
}
```

`animate(() => …)` and `animate(anim, () => …)` are the same as
closures. `repeat: Animation.forever` repeats until the props change
again. `animating()` says whether the changes made now animate.
`onFrame(t => …)` calls its closure at each frame the display draws,
with the frame's time, until the closure it returns is called or the
component goes: `let stop = onFrame(t => tick(t))`. `frames()` is the
same as a `Stream<Duration>`, drawn only while it's read: `for await t
in frames() { … }` in a task, which stops as the loop ends or the task
goes.
