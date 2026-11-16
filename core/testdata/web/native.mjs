// native.asuka's Echo, in JavaScript: what a native package's generated
// web module (x.asuka.js) and its own (x.js) register, by hand.
const { NativeWriter, UnsupportedError, registerModule } = globalThis.asuka.native;

registerModule(".", {
  make(iface) {
    if (iface === 0) return {};
    throw new UnsupportedError(`no interface ${iface}`);
  },
  call(o, iface, method, r, w) {
    switch (iface * 65536 + method) {
      case 1: { // Echo.upper
        const s = r.string();
        r.check();
        w.string(s.toUpperCase());
        return;
      }
      case 3: { // Echo.each: its callback, answered during the call
        const n = r.len();
        const items = [];
        for (let i = 0; i < n; i++) items.push(r.string());
        const f = r.callback();
        r.check();
        let sum = 0;
        for (const s of items) sum += r.ask(f, new NativeWriter().string(s)).int();
        w.int(sum);
        return;
      }
    }
    throw new UnsupportedError(`no method ${iface}.${method}`);
  },
  start(o, iface, method, r, signal) {
    switch (iface * 65536 + method) {
      case 2: { // Echo.later
        const s = r.string();
        r.check();
        return new Promise((ok) => setTimeout(() => ok(NativeWriter.ok().string(s + "!")), 5));
      }
      case 4: { // Echo.ticks: its callback, called on its own, later
        const n = r.int();
        const tick = r.callback();
        r.check();
        return (async () => {
          for (let i = 1; i <= n; i++) {
            await new Promise((ok) => setTimeout(ok, 2));
            r.notify(tick, new NativeWriter().int(i));
          }
          return NativeWriter.ok();
        })();
      }
    }
    throw new UnsupportedError(`no method ${iface}.${method}`);
  },
  makeView() { return null; },
});
