const next = (s) => (Math.imul(s, 1103515245) + 12345) & 0x7fffffff;

function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

function sieve(n) {
  const composite = new Uint8Array(n + 1);
  let count = 0;
  for (let i = 2; i <= n; i++) {
    if (!composite[i]) {
      count++;
      for (let j = i * i; j <= n; j += i) composite[j] = 1;
    }
  }
  return count;
}

function energy(b) {
  let e = 0;
  for (let i = 0; i < b.length; i++) {
    const p = b[i];
    e += 0.5 * p.mass * (p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
    for (let j = i + 1; j < b.length; j++) {
      const q = b[j];
      const dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
      e -= p.mass * q.mass / Math.sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  return e;
}

function nbody(steps) {
  const pi = 3.141592653589793, solar = 4 * pi * pi, days = 365.24;
  const B = (x, y, z, vx, vy, vz, mass) => ({ x, y, z, vx, vy, vz, mass });
  const b = [
    B(0, 0, 0, 0, 0, 0, solar),
    B(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01, 1.66007664274403694e-03 * days, 7.69901118419740425e-03 * days, -6.90460016972063023e-05 * days, 9.54791938424326609e-04 * solar),
    B(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01, -2.76742510726862411e-03 * days, 4.99852801234917238e-03 * days, 2.30417297573763929e-05 * days, 2.85885980666130812e-04 * solar),
    B(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01, 2.96460137564761618e-03 * days, 2.37847173959480950e-03 * days, -2.96589568540237556e-05 * days, 4.36624404335156298e-05 * solar),
    B(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01, 2.68067772490389322e-03 * days, 1.62824170038242295e-03 * days, -9.51592254519715870e-05 * days, 5.15138902046611451e-05 * solar),
  ];
  let px = 0, py = 0, pz = 0;
  for (const x of b) { px += x.vx * x.mass; py += x.vy * x.mass; pz += x.vz * x.mass; }
  b[0].vx = -px / solar; b[0].vy = -py / solar; b[0].vz = -pz / solar;
  const before = energy(b);
  const n = b.length, dt = 0.01;
  for (let s = 0; s < steps; s++) {
    for (let i = 0; i < n; i++) {
      const p = b[i];
      for (let j = i + 1; j < n; j++) {
        const q = b[j];
        const dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
        const d2 = dx * dx + dy * dy + dz * dz;
        const mag = dt / (d2 * Math.sqrt(d2));
        const mi = p.mass * mag, mj = q.mass * mag;
        p.vx -= dx * mj; p.vy -= dy * mj; p.vz -= dz * mj;
        q.vx += dx * mi; q.vy += dy * mi; q.vz += dz * mi;
      }
    }
    for (let i = 0; i < n; i++) { const p = b[i]; p.x += dt * p.vx; p.y += dt * p.vy; p.z += dt * p.vz; }
  }
  return `${before.toFixed(9)} ${energy(b).toFixed(9)}`;
}

function make(d) { return d === 0 ? { left: null, right: null } : { left: make(d - 1), right: make(d - 1) }; }
function check(t) { let n = 1; if (t.left) n += check(t.left); if (t.right) n += check(t.right); return n; }
function trees(max) {
  let total = check(make(max + 1));
  const long = make(max);
  for (let d = 4; d <= max; d += 2) {
    const it = 1 << (max - d + 4);
    for (let i = 0; i < it; i++) total += check(make(d));
  }
  return total + check(long);
}

function words(n) {
  const syl = ["ka", "to", "ri", "su", "me", "no", "ha", "yu", "chi", "ra"];
  let seed = 42;
  const parts = [];
  for (let i = 0; i < n; i++) {
    seed = next(seed); const a = syl[seed % 10];
    seed = next(seed); parts.push(a + syl[seed % 10]);
  }
  const text = parts.join(" ");
  const counts = new Map();
  for (const w of text.split(" ")) counts.set(w, (counts.get(w) ?? 0) + 1);
  const keys = [...counts.keys()].sort((a, b) => (counts.get(b) - counts.get(a)) || (a < b ? -1 : a > b ? 1 : 0));
  return `${counts.size} ${keys[0]} ${counts.get(keys[0])}`;
}

function jsonRound(n) {
  const items = [];
  for (let i = 0; i < n; i++) items.push({ id: i, name: `item ${i}`, price: i * 1.5, tags: ["a", `b${i % 7}`], active: i % 2 === 0 });
  let total = 0;
  for (let r = 0; r < 5; r++) {
    const text = JSON.stringify(items);
    const back = JSON.parse(text);
    total += back.length + Buffer.byteLength(text) % 7;
  }
  return total;
}

function sortInts(n) {
  let seed = 7;
  const xs = new Int32Array(n);
  for (let i = 0; i < n; i++) { seed = next(seed); xs[i] = seed; }
  xs.sort();
  return xs[0] + xs[n >> 1] + xs[n - 1];
}

const name = process.argv[2] ?? "fib";
const run = { fib: () => fib(38), sieve: () => sieve(50000000), nbody: () => nbody(5000000), trees: () => trees(18), words: () => words(2000000), json: () => jsonRound(200000), sort: () => sortInts(5000000) };
console.log(String(run[name]()));
