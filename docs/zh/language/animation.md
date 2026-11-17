# 动画

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
