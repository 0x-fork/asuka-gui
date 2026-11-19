-- The VM benchmarks' interpreter-bound ones in Lua, the same sizes: a
-- reference point for an interpreter (luajit -joff: LuaJIT's
-- interpreter; luajit: its JIT). luajit -joff bench.lua fib
local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end

local function loop(n)
  local s, i = 0, 0
  while i < n do s = (s + i * 3) % 1000003; i = i + 1 end
  return s
end

local function sieve(n)
  local composite = {}
  for i = 0, n do composite[i] = false end
  local count, i = 0, 2
  while i <= n do
    if not composite[i] then
      count = count + 1
      local j = i * i
      while j <= n do composite[j] = true; j = j + i end
    end
    i = i + 1
  end
  return count
end

local function body(x, y, z, vx, vy, vz, mass) return {x = x, y = y, z = z, vx = vx, vy = vy, vz = vz, mass = mass} end

local function energy(bodies)
  local e = 0.0
  for i = 1, #bodies do
    local b = bodies[i]
    e = e + 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy + b.vz * b.vz)
    for j = i + 1, #bodies do
      local c = bodies[j]
      local dx, dy, dz = b.x - c.x, b.y - c.y, b.z - c.z
      e = e - b.mass * c.mass / math.sqrt(dx * dx + dy * dy + dz * dz)
    end
  end
  return e
end

local function nbody(steps)
  local pi = 3.141592653589793
  local solar, days = 4 * pi * pi, 365.24
  local bodies = {
    body(0, 0, 0, 0, 0, 0, solar),
    body(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01, 1.66007664274403694e-03 * days, 7.69901118419740425e-03 * days, -6.90460016972063023e-05 * days, 9.54791938424326609e-04 * solar),
    body(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01, -2.76742510726862411e-03 * days, 4.99852801234917238e-03 * days, 2.30417297573763929e-05 * days, 2.85885980666130812e-04 * solar),
    body(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01, 2.96460137564761618e-03 * days, 2.37847173959480950e-03 * days, -2.96589568540237556e-05 * days, 4.36624404335156298e-05 * solar),
    body(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01, 2.68067772490389322e-03 * days, 1.62824170038242295e-03 * days, -9.51592254519715870e-05 * days, 5.15138902046611451e-05 * solar),
  }
  local px, py, pz = 0, 0, 0
  for _, b in ipairs(bodies) do px = px + b.vx * b.mass; py = py + b.vy * b.mass; pz = pz + b.vz * b.mass end
  bodies[1].vx, bodies[1].vy, bodies[1].vz = -px / solar, -py / solar, -pz / solar
  local before = energy(bodies)
  local n, dt = #bodies, 0.01
  for _ = 1, steps do
    for i = 1, n do
      local bi = bodies[i]
      for j = i + 1, n do
        local bj = bodies[j]
        local dx, dy, dz = bi.x - bj.x, bi.y - bj.y, bi.z - bj.z
        local d2 = dx * dx + dy * dy + dz * dz
        local mag = dt / (d2 * math.sqrt(d2))
        local mi, mj = bi.mass * mag, bj.mass * mag
        bi.vx = bi.vx - dx * mj; bi.vy = bi.vy - dy * mj; bi.vz = bi.vz - dz * mj
        bj.vx = bj.vx + dx * mi; bj.vy = bj.vy + dy * mi; bj.vz = bj.vz + dz * mi
      end
    end
    for i = 1, n do
      local b = bodies[i]
      b.x = b.x + dt * b.vx; b.y = b.y + dt * b.vy; b.z = b.z + dt * b.vz
    end
  end
  return string.format("%.9f %.9f", before, energy(bodies))
end

local function make(d) if d == 0 then return {} end return {make(d - 1), make(d - 1)} end
local function check(t)
  local n = 1
  if t[1] then n = n + check(t[1]) end
  if t[2] then n = n + check(t[2]) end
  return n
end
local function trees(maxDepth)
  local total = check(make(maxDepth + 1))
  local long = make(maxDepth)
  local d = 4
  while d <= maxDepth do
    for _ = 1, 2 ^ (maxDepth - d + 4) do total = total + check(make(d)) end
    d = d + 2
  end
  return total + check(long)
end

local function apply(f, x) return f(x) end
local function closures(n)
  local s = 0
  for i = 0, n - 1 do
    local k = i % 7
    s = apply(function(x) return x + k end, s) % 1000003
  end
  return s
end

local benches = {
  fib = function() return fib(32) end, loop = function() return loop(20000000) end,
  sieve = function() return sieve(20000000) end, nbody = function() return nbody(300000) end,
  trees = function() return trees(16) end, closures = function() return closures(8000000) end,
}
print(benches[arg[1] or "fib"]())
