// The engine's stack, for the core (core/src/base/stack.c): wasm's frames
// run on the engine's own stack, JavaScript's, which the core's C stack
// in its memory says nothing of. As its C stack grows, the core asks
// whether a quarter of the engine's stack is still free (stack_room), and
// throws its own "stack overflow" when it isn't, before the engine's
// RangeError would end the page.
//
// The engine's stack is measured once, as the core starts, in frames of
// down (warmed up first, so that they're the size they'll stay); asking
// is going down a quarter as many from where the core asks.

let reached = 0;

function down(n) {
  reached++;
  if (n > 0) down(n - 1);
}

/** fits says whether n frames of down fit on the stack from here. */
function fits(n) {
  reached = 0;
  try {
    down(n);
    return true;
  } catch {
    return false;
  }
}

/** stackRoom is the core's stack_room import: 1 when a quarter of the engine's stack is free. */
export function stackRoom() {
  let total = 0;
  for (let i = 0; i < 4; i++) {
    fits(Infinity);
    total = Math.max(total, reached);
  }
  const quarter = Math.max(1, Math.floor(total / 4));
  return () => (fits(quarter) ? 1 : 0);
}
