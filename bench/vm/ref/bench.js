// The VM benchmarks' interpreter-bound ones in JavaScript, the same
// sizes: a reference point for a JIT (bun bench.js fib; bun
// --jitless... not offered: Bun's JavaScriptCore always JITs).
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2) }
function loop(n) { let s = 0, i = 0; while (i < n) { s = (s + i * 3) % 1000003; i++ } return s }
function sieve(n) {
  const composite = new Array(n + 1).fill(false)
  let count = 0
  for (let i = 2; i <= n; i++) {
    if (!composite[i]) { count++; for (let j = i * i; j <= n; j += i) composite[j] = true }
  }
  return count
}
function energy(bodies) {
  let e = 0
  for (let i = 0; i < bodies.length; i++) {
    const b = bodies[i]
    e += 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy + b.vz * b.vz)
    for (let j = i + 1; j < bodies.length; j++) {
      const c = bodies[j], dx = b.x - c.x, dy = b.y - c.y, dz = b.z - c.z
      e -= b.mass * c.mass / Math.sqrt(dx * dx + dy * dy + dz * dz)
    }
  }
  return e
}
function nbody(steps) {
  const pi = 3.141592653589793, solar = 4 * pi * pi, days = 365.24
  const B = (x, y, z, vx, vy, vz, mass) => ({ x, y, z, vx, vy, vz, mass })
  const bodies = [
    B(0, 0, 0, 0, 0, 0, solar),
    B(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01, 1.66007664274403694e-03 * days, 7.69901118419740425e-03 * days, -6.90460016972063023e-05 * days, 9.54791938424326609e-04 * solar),
    B(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01, -2.76742510726862411e-03 * days, 4.99852801234917238e-03 * days, 2.30417297573763929e-05 * days, 2.85885980666130812e-04 * solar),
    B(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01, 2.96460137564761618e-03 * days, 2.37847173959480950e-03 * days, -2.96589568540237556e-05 * days, 4.36624404335156298e-05 * solar),
    B(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01, 2.68067772490389322e-03 * days, 1.62824170038242295e-03 * days, -9.51592254519715870e-05 * days, 5.15138902046611451e-05 * solar),
  ]
  let px = 0, py = 0, pz = 0
  for (const b of bodies) { px += b.vx * b.mass; py += b.vy * b.mass; pz += b.vz * b.mass }
  bodies[0].vx = -px / solar; bodies[0].vy = -py / solar; bodies[0].vz = -pz / solar
  const before = energy(bodies), n = bodies.length, dt = 0.01
  for (let s = 0; s < steps; s++) {
    for (let i = 0; i < n; i++) {
      const bi = bodies[i]
      for (let j = i + 1; j < n; j++) {
        const bj = bodies[j], dx = bi.x - bj.x, dy = bi.y - bj.y, dz = bi.z - bj.z
        const d2 = dx * dx + dy * dy + dz * dz, mag = dt / (d2 * Math.sqrt(d2))
        const mi = bi.mass * mag, mj = bj.mass * mag
        bi.vx -= dx * mj; bi.vy -= dy * mj; bi.vz -= dz * mj
        bj.vx += dx * mi; bj.vy += dy * mi; bj.vz += dz * mi
      }
    }
    for (const b of bodies) { b.x += dt * b.vx; b.y += dt * b.vy; b.z += dt * b.vz }
  }
  return `${before.toFixed(9)} ${energy(bodies).toFixed(9)}`
}
function make(d) { return d === 0 ? { l: null, r: null } : { l: make(d - 1), r: make(d - 1) } }
function check(t) { let n = 1; if (t.l) n += check(t.l); if (t.r) n += check(t.r); return n }
function trees(max) {
  let total = check(make(max + 1))
  const long = make(max)
  for (let d = 4; d <= max; d += 2) for (let k = 0; k < 1 << (max - d + 4); k++) total += check(make(d))
  return total + check(long)
}
function apply(f, x) { return f(x) }
function closures(n) { let s = 0; for (let i = 0; i < n; i++) { const k = i % 7; s = apply(x => x + k, s) % 1000003 } return s }
const benches = { fib: () => fib(32), loop: () => loop(20000000), sieve: () => sieve(20000000), nbody: () => nbody(300000), trees: () => trees(16), closures: () => closures(8000000) }
console.log(benches[process.argv[2] || "fib"]())
