const std = @import("std");

const gpa = std.heap.smp_allocator;

fn next(s: u64) u64 {
    return (s *% 1103515245 +% 12345) % 2147483648;
}

fn fib(n: i64) i64 {
    return if (n < 2) n else fib(n - 1) + fib(n - 2);
}

fn sieve(n: usize) !usize {
    const composite = try gpa.alloc(bool, n + 1);
    defer gpa.free(composite);
    @memset(composite, false);
    var count: usize = 0;
    var i: usize = 2;
    while (i <= n) : (i += 1) {
        if (!composite[i]) {
            count += 1;
            var j = i * i;
            while (j <= n) : (j += i) composite[j] = true;
        }
    }
    return count;
}

const Body = struct { x: f64, y: f64, z: f64, vx: f64, vy: f64, vz: f64, mass: f64 };

fn energy(b: []Body) f64 {
    var e: f64 = 0;
    for (b, 0..) |p, i| {
        e += 0.5 * p.mass * (p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
        for (b[i + 1 ..]) |q| {
            const dx = p.x - q.x;
            const dy = p.y - q.y;
            const dz = p.z - q.z;
            e -= p.mass * q.mass / @sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
    return e;
}

fn nbody(steps: usize, buf: []u8) ![]u8 {
    const pi = 3.141592653589793;
    const solar = 4 * pi * pi;
    const days = 365.24;
    var b = [_]Body{
        .{ .x = 0, .y = 0, .z = 0, .vx = 0, .vy = 0, .vz = 0, .mass = solar },
        .{ .x = 4.84143144246472090e+00, .y = -1.16032004402742839e+00, .z = -1.03622044471123109e-01, .vx = 1.66007664274403694e-03 * days, .vy = 7.69901118419740425e-03 * days, .vz = -6.90460016972063023e-05 * days, .mass = 9.54791938424326609e-04 * solar },
        .{ .x = 8.34336671824457987e+00, .y = 4.12479856412430479e+00, .z = -4.03523417114321381e-01, .vx = -2.76742510726862411e-03 * days, .vy = 4.99852801234917238e-03 * days, .vz = 2.30417297573763929e-05 * days, .mass = 2.85885980666130812e-04 * solar },
        .{ .x = 1.28943695621391310e+01, .y = -1.51111514016986312e+01, .z = -2.23307578892655734e-01, .vx = 2.96460137564761618e-03 * days, .vy = 2.37847173959480950e-03 * days, .vz = -2.96589568540237556e-05 * days, .mass = 4.36624404335156298e-05 * solar },
        .{ .x = 1.53796971148509165e+01, .y = -2.59193146099879641e+01, .z = 1.79258772950371181e-01, .vx = 2.68067772490389322e-03 * days, .vy = 1.62824170038242295e-03 * days, .vz = -9.51592254519715870e-05 * days, .mass = 5.15138902046611451e-05 * solar },
    };
    var px: f64 = 0;
    var py: f64 = 0;
    var pz: f64 = 0;
    for (b) |x| {
        px += x.vx * x.mass;
        py += x.vy * x.mass;
        pz += x.vz * x.mass;
    }
    b[0].vx = -px / solar;
    b[0].vy = -py / solar;
    b[0].vz = -pz / solar;
    const before = energy(&b);
    const dt = 0.01;
    for (0..steps) |_| {
        for (0..b.len) |i| {
            for (i + 1..b.len) |j| {
                const dx = b[i].x - b[j].x;
                const dy = b[i].y - b[j].y;
                const dz = b[i].z - b[j].z;
                const d2 = dx * dx + dy * dy + dz * dz;
                const mag = dt / (d2 * @sqrt(d2));
                const mi = b[i].mass * mag;
                const mj = b[j].mass * mag;
                b[i].vx -= dx * mj;
                b[i].vy -= dy * mj;
                b[i].vz -= dz * mj;
                b[j].vx += dx * mi;
                b[j].vy += dy * mi;
                b[j].vz += dz * mi;
            }
        }
        for (&b) |*p| {
            p.x += dt * p.vx;
            p.y += dt * p.vy;
            p.z += dt * p.vz;
        }
    }
    return std.fmt.bufPrint(buf, "{d:.9} {d:.9}", .{ before, energy(&b) });
}

const Tree = struct { left: ?*Tree, right: ?*Tree };

fn make(d: u32) !*Tree {
    const t = try gpa.create(Tree);
    t.* = if (d == 0) .{ .left = null, .right = null } else .{ .left = try make(d - 1), .right = try make(d - 1) };
    return t;
}

fn check(t: *Tree) i64 {
    var n: i64 = 1;
    if (t.left) |l| n += check(l);
    if (t.right) |r| n += check(r);
    return n;
}

fn free(t: *Tree) void {
    if (t.left) |l| free(l);
    if (t.right) |r| free(r);
    gpa.destroy(t);
}

fn trees(max: u32) !i64 {
    const first = try make(max + 1);
    var total = check(first);
    free(first);
    const long = try make(max);
    var d: u32 = 4;
    while (d <= max) : (d += 2) {
        const it = @as(u64, 1) << @intCast(max - d + 4);
        for (0..it) |_| {
            const t = try make(d);
            total += check(t);
            free(t);
        }
    }
    total += check(long);
    free(long);
    return total;
}

fn words(n: usize, buf: []u8) ![]u8 {
    const syl = [_][]const u8{ "ka", "to", "ri", "su", "me", "no", "ha", "yu", "chi", "ra" };
    var seed: u64 = 42;
    var text: std.ArrayList(u8) = .empty;
    defer text.deinit(gpa);
    for (0..n) |i| {
        seed = next(seed);
        const a = syl[seed % 10];
        seed = next(seed);
        if (i > 0) try text.append(gpa, ' ');
        try text.appendSlice(gpa, a);
        try text.appendSlice(gpa, syl[seed % 10]);
    }
    var counts = std.StringHashMap(u32).init(gpa);
    defer counts.deinit();
    var it = std.mem.splitScalar(u8, text.items, ' ');
    while (it.next()) |w| {
        const e = try counts.getOrPut(w);
        if (!e.found_existing) e.value_ptr.* = 0;
        e.value_ptr.* += 1;
    }
    var keys: std.ArrayList([]const u8) = .empty;
    defer keys.deinit(gpa);
    var ki = counts.keyIterator();
    while (ki.next()) |k| try keys.append(gpa, k.*);
    const Ctx = struct {
        c: *std.StringHashMap(u32),
        fn less(ctx: @This(), a: []const u8, b: []const u8) bool {
            const ca = ctx.c.get(a).?;
            const cb = ctx.c.get(b).?;
            return ca > cb or (ca == cb and std.mem.lessThan(u8, a, b));
        }
    };
    std.mem.sort([]const u8, keys.items, Ctx{ .c = &counts }, Ctx.less);
    return std.fmt.bufPrint(buf, "{d} {s} {d}", .{ counts.count(), keys.items[0], counts.get(keys.items[0]).? });
}

const Item = struct { id: i64, name: []const u8, price: f64, tags: []const []const u8, active: bool };

fn jsonRound(n: usize) !usize {
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const a = arena.allocator();
    const items = try a.alloc(Item, n);
    for (items, 0..) |*it, i| {
        const tags = try a.alloc([]const u8, 2);
        tags[0] = "a";
        tags[1] = try std.fmt.allocPrint(a, "b{d}", .{i % 7});
        it.* = .{ .id = @intCast(i), .name = try std.fmt.allocPrint(a, "item {d}", .{i}), .price = @as(f64, @floatFromInt(i)) * 1.5, .tags = tags, .active = i % 2 == 0 };
    }
    var total: usize = 0;
    for (0..5) |_| {
        const text = try std.json.Stringify.valueAlloc(gpa, items, .{});
        defer gpa.free(text);
        const back = try std.json.parseFromSlice([]Item, gpa, text, .{});
        defer back.deinit();
        total += back.value.len + text.len % 7;
    }
    return total;
}

fn sortInts(n: usize) !u64 {
    var seed: u64 = 7;
    var xs: std.ArrayList(u64) = .empty;
    defer xs.deinit(gpa);
    for (0..n) |_| {
        seed = next(seed);
        try xs.append(gpa, seed);
    }
    std.mem.sort(u64, xs.items, {}, std.sort.asc(u64));
    return xs.items[0] + xs.items[n / 2] + xs.items[n - 1];
}

pub fn main(init: std.process.Init.Minimal) !void {
    var args = std.process.Args.Iterator.init(init.args);
    _ = args.skip();
    const name = args.next() orelse "fib";
    var buf: [256]u8 = undefined;
    const out: []const u8 = if (std.mem.eql(u8, name, "fib"))
        try std.fmt.bufPrint(&buf, "{d}", .{fib(38)})
    else if (std.mem.eql(u8, name, "sieve"))
        try std.fmt.bufPrint(&buf, "{d}", .{try sieve(50000000)})
    else if (std.mem.eql(u8, name, "nbody"))
        try nbody(5000000, &buf)
    else if (std.mem.eql(u8, name, "trees"))
        try std.fmt.bufPrint(&buf, "{d}", .{try trees(18)})
    else if (std.mem.eql(u8, name, "words"))
        try words(2000000, &buf)
    else if (std.mem.eql(u8, name, "json"))
        try std.fmt.bufPrint(&buf, "{d}", .{try jsonRound(200000)})
    else
        try std.fmt.bufPrint(&buf, "{d}", .{try sortInts(5000000)});
    std.debug.print("{s}\n", .{out});
}
