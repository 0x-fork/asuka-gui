// ../asuka/main.asuka's work in JavaScript, on Bun (its native APIs where
// it has them), for comparison: the same inputs, the same results.
// `bun bench/std/bun/main.js <name> [dir]` prints the result and the
// nanoseconds the work took, on stderr. What Bun has no equivalent of
// prints "n/a".
import { createHash, createHmac, createCipheriv, createDecipheriv } from "node:crypto";
import { gzipSync, gunzipSync } from "node:zlib";
import { readFileSync, writeFileSync, unlinkSync } from "node:fs";

const syllables = ["ka", "to", "ri", "su", "me", "no", "ha", "yu", "chi", "ra"];
const next = (s) => (Math.imul(s, 1103515245) + 12345) & 0x7fffffff;

function corpus(n) {
  let seed = 42;
  const parts = [];
  for (let i = 0; i < n; i++) {
    seed = next(seed);
    let w = syllables[seed % 10] + syllables[Math.floor(seed / 10) % 10];
    if (seed % 13 === 0) w += String(seed % 1000);
    parts.push(w, i % 12 === 11 ? "\n" : " ");
  }
  return parts.join("");
}

const utf8len = (s) => Buffer.byteLength(s, "utf8");

function lines(s) {
  const out = s.split("\n").map((l) => (l.endsWith("\r") ? l.slice(0, -1) : l));
  if (out.length && out[out.length - 1] === "") out.pop();
  return out;
}

function interp(n) {
  const parts = [];
  for (let i = 0; i < n; i++) parts.push(`item ${i}: ${i * 3} ok`);
  return String(parts.join(",").length);
}

function concat(n) {
  let total = 0;
  for (let i = 0; i < n; i++) {
    const k = syllables[i % 10] + "-" + syllables[Math.floor(i / 10) % 10] + "/" + syllables[Math.floor(i / 100) % 10];
    total += k.length;
  }
  return String(total);
}

function splitting(text) {
  let total = 0;
  for (let r = 0; r < 3; r++) {
    total += text.split(" ").length + lines(text).length + text.split(/[ \t\n\r\v\f]+/).filter((x) => x).length;
  }
  return String(total);
}

function finding(text) {
  let total = 0;
  for (let r = 0; r < 5; r++) {
    total += text.replaceAll("kato", "X").length;
    total += text.split("chira").length;
    if (text.includes("suyuq")) total++;
    if (text.includes("ra\nka")) total++;
    total += text.indexOf("meme1") + text.lastIndexOf("kaka");
  }
  return String(total);
}

const fold = (s) => s.toLowerCase().replaceAll("ß", "ss").replaceAll("ς", "σ");

function casing(text, uni) {
  let total = 0;
  for (let r = 0; r < 3; r++) {
    total += text.toUpperCase().toLowerCase().length;
    total += utf8len(uni.toUpperCase()) + utf8len(uni.toLowerCase()) + utf8len(fold(uni));
    if (text.toLowerCase().includes("kato")) total++;
    if (fold(uni).endsWith(fold("STRASSE"))) total++;
  }
  return String(total);
}

function counting(text) {
  let total = 0;
  for (let r = 0; r < 20; r++) {
    let n = 0;
    for (const _ of text) n++;
    total += n;
  }
  const chars = Array.from(text);
  const step = Math.floor(chars.length / 2000);
  for (let i = 0; i < 2000; i++) total += utf8len(chars.slice(i * step, i * step + 8).join(""));
  return String(total);
}

function numbers(n) {
  const ints = [];
  for (let i = 0; i < n; i++) ints.push(String(((i * 7919) % 1000003) - 500000));
  let total = 0;
  for (const s of ints) total += parseInt(s, 10);
  const floats = [];
  for (let i = 0; i < n; i++) floats.push(String(i / 7.0 + 0.1));
  let sum = 0;
  for (const s of floats) sum += Number(s);
  total += floats.join("").length;
  return `${total} ${sum.toFixed(3)}`;
}

function expo(x) {
  // Python's and Go's %e: 6 digits, a two-digit exponent at least.
  const [m, e] = x.toExponential(6).split("e");
  const n = Math.abs(Number(e));
  return `${m}e${e[0] === "-" ? "-" : "+"}${n < 10 ? "0" + n : n}`;
}

function formats(n) {
  let total = 0;
  for (let i = 0; i < n; i++) {
    const x = i * 1.25;
    total += `${x.toFixed(3).padStart(12)}|${String(i).padStart(8, "0")}|${i.toLocaleString("en-US")}|${expo(x)}`.length;
  }
  return String(total);
}

function mapInt(n) {
  const m = new Map();
  for (let i = 0; i < n; i++) {
    const k = (i * 7919) % 1000003;
    m.set(k, (m.get(k) ?? 0) + 1);
  }
  let hits = 0;
  for (let i = 0; i < n; i++) hits += m.get(i) ?? 0;
  return `${m.size} ${hits}`;
}

function mapStr(keys) {
  const m = new Map();
  for (const k of keys) m.set(k, (m.get(k) ?? 0) + 1);
  let hits = 0;
  for (const k of keys) hits += m.get(k) ?? 0;
  return `${m.size} ${hits}`;
}

function sets(n) {
  const s = new Set();
  for (let i = 0; i < n; i++) s.add((i * 7919) % 1000003);
  let hits = 0;
  for (let i = 0; i < n; i++) if (s.has(i)) hits++;
  return `${s.size} ${hits}`;
}

function ints(n) {
  let seed = 7;
  const xs = [];
  for (let i = 0; i < n; i++) {
    seed = next(seed);
    xs.push(seed);
  }
  return xs;
}

function regexes(text) {
  const count = (re) => (text.match(re) ?? []).length;
  let total = count(/\d+/g);
  total += count(/\b\w+chi\w*\b/g);
  total += count(/chira/g);
  total += count(/(\w+)@(\w+)\.com/g);
  total += count(/kato|meme|yuyu/gi);
  const d = /\d+/;
  for (const l of lines(text)) if (d.test(l)) total++;
  total += text.replace(/\d+/g, "#").length;
  return String(total);
}

function csvText(n) {
  const rows = ["id,name,price,tags,note"];
  for (let i = 0; i < n; i++) rows.push(`${i},item ${i},${i * 0.25},"a,b${i % 7}",plain text here`);
  return rows.join("\n") + "\n";
}

function tomlText(n) {
  const parts = ['title = "bench"\n'];
  for (let i = 0; i < n; i++) {
    parts.push(`[server${i}]\nhost = "10.0.${i % 256}.${i % 100}"\nport = ${8000 + i}\nenabled = ${i % 2 === 0}\nweights = [1.5, 2.5, ${i}]\n\n`);
  }
  return parts.join("");
}

function aead(data, alg, nonceSize) {
  const key = createHash("sha256").update("key").digest();
  const nonce = createHash("sha256").update("nonce").digest().subarray(0, nonceSize);
  let total = 0;
  for (let r = 0; r < 2; r++) {
    const c = createCipheriv(alg, key, nonce, { authTagLength: 16 });
    const sealed = Buffer.concat([c.update(data), c.final(), c.getAuthTag()]);
    const d = createDecipheriv(alg, key, nonce, { authTagLength: 16 });
    d.setAuthTag(sealed.subarray(sealed.length - 16));
    const back = Buffer.concat([d.update(sealed.subarray(0, sealed.length - 16)), d.final()]);
    total += back.length + sealed[sealed.length - 1];
  }
  return String(total);
}

function bigints(n) {
  let f = 1n;
  for (let i = 1n; i <= BigInt(n); i++) f *= i;
  const s = f.toString();
  const p = 3n ** 200000n;
  const q = p / (f / 7n);
  return `${s.length} ${p.toString().length} ${q.toString(2).length}`;
}

function modpow(b, e, m) {
  let r = 1n;
  b %= m;
  while (e > 0n) {
    if (e & 1n) r = (r * b) % m;
    b = (b * b) % m;
    e >>= 1n;
  }
  return r;
}

function modpows(n) {
  const m = (1n << 2048n) - 1942289n;
  let b = 65537n;
  for (let i = 0; i < n; i++) b = modpow(b, m - 2n, m) + 1n;
  return String(b.toString(2).length);
}

function times(n) {
  let total = 0;
  const t = 1700000000;
  for (let i = 0; i < n; i++) {
    const s = new Date((t + i * 7919) * 1000).toISOString().replace(".000Z", "Z");
    total += (Math.floor(Date.parse(s) / 1000) % 1000) + s.length;
  }
  return String(total);
}

function semvers(n) {
  const vs = [];
  let seed = 3;
  for (let i = 0; i < n; i++) {
    seed = next(seed);
    const pre = seed % 5 === 0 ? `-beta.${seed % 4}` : "";
    vs.push(`${seed % 20}.${Math.floor(seed / 20) % 30}.${Math.floor(seed / 600) % 40}${pre}`);
  }
  const sorted = [...vs].sort(Bun.semver.order);
  const ok = vs.filter((v) => Bun.semver.satisfies(v, ">=3.1.0 <12.0.0 || ^15.2")).length;
  return `${sorted[0]} ${sorted[sorted.length - 1]} ${ok}`;
}

function seqs(n) {
  let a = 0;
  for (let x = 0; x < n; x++) {
    const y = x * 3;
    if (y % 2 === 0) a += y + 1;
  }
  // The same through arrays, a step at a time, as Asuka's
  // toArray().map().filter().map().reduce() is.
  const xs = Array.from({ length: n }, (_, i) => i);
  const b = xs.map((x) => x * 3).filter((x) => x % 2 === 0).map((x) => x + 1).reduce((s, x) => s + x, 0);
  return `${a} ${b}`;
}

const name = process.argv[2] ?? "";
if (name === "serve") {
  // GET /hello: text; /json: JSON, as ../asuka's serve.
  Bun.serve({
    port: Number(process.argv[3] ?? 8080),
    hostname: "127.0.0.1",
    fetch(req) {
      if (new URL(req.url).pathname === "/json") return Response.json({ message: "Hello, world!", id: 42, tags: ["a", "b"] });
      return new Response("Hello, world!", { headers: { "content-type": "text/plain; charset=utf-8" } });
    },
  });
  await new Promise(() => {});
}
const dir = process.argv[3] ?? "/tmp";
const text = ["split", "find", "case", "count", "regex", "files", "lines"].includes(name) ? corpus(2000000) : "";
const uni = name === "case" ? "Ärger über die Straße, ΣΟΦΟΣ λόγος — Ёлка и ЁЖ; ".repeat(40000) : "";
const blob = ["sha256", "sha512", "aesgcm", "chacha", "gzip", "sha1", "md5", "crc32"].includes(name) ? Buffer.from(corpus(10000000)) : null;
const marked = name === "html" ? corpus(2000000).replaceAll("kato", "<k&o>").replaceAll("chi", "c>i") : "";
const small = ["base64", "hex"].includes(name) ? Buffer.from(corpus(2000000)) : null;
const keys = name === "mapstr" ? Array.from({ length: 2000000 }, (_, i) => "key" + ((i * 7919) % 500009)) : [];
const xs = ["sortint", "heap", "sortedmap"].includes(name) ? ints(name === "sortint" ? 5000000 : 1000000) : [];
const strs = name === "sortstr" ? ints(1000000).map((x) => "w" + x) : [];
const scores = name === "sortby" ? ints(1000000).map((x) => ({ id: x, score: x % 100000 })) : [];
const csvIn = name === "csv" ? csvText(500000) : "";
const tomlIn = name === "toml" ? tomlText(50000) : "";

// STDBENCH_REPS: the work that many times (the runner's -instr).
const reps = Math.max(1, Number(process.env.STDBENCH_REPS) || 1);
const t0 = Bun.nanoseconds();
let out;
for (let rep = 0; rep < reps; rep++) {
  switch (name) {
    case "interp": out = interp(2000000); break;
    case "concat": out = concat(2000000); break;
    case "split": out = splitting(text); break;
    case "find": out = finding(text); break;
    case "case": out = casing(text, uni); break;
    case "count": out = counting(text); break;
    case "num": out = numbers(1000000); break;
    case "fmt": out = formats(1000000); break;
    case "mapint": out = mapInt(2000000); break;
    case "mapstr": out = mapStr(keys); break;
    case "set": out = sets(2000000); break;
    case "sortint": {
      const a = Float64Array.from(xs).sort();
      out = String(a[0] + a[a.length >> 1] + a[a.length - 1]);
      break;
    }
    case "sortstr": {
      const a = [...strs].sort();
      out = `${a[0]} ${a[a.length >> 1]} ${a[a.length - 1]}`;
      break;
    }
    case "sortby": {
      const a = [...scores].sort((p, q) => p.score - q.score);
      const b = [...scores].sort((p, q) => q.score - p.score);
      out = `${a[0].id} ${a[a.length >> 1].id} ${b[0].id}`;
      break;
    }
    case "regex": out = regexes(text); break;
    case "files": {
      const path = `${dir}/stdbench.txt`;
      await Bun.write(path, text);
      const a = await Bun.file(path).text();
      const b = await Bun.file(path).bytes();
      unlinkSync(path);
      out = String(utf8len(a) + b.length);
      break;
    }
    case "lines": {
      const path = `${dir}/stdbench-lines.txt`;
      writeFileSync(path, text);
      let total = 0;
      for (const l of lines(readFileSync(path, "utf8"))) total += utf8len(l);
      unlinkSync(path);
      out = String(total);
      break;
    }
    case "print":
      for (let i = 0; i < 1000000; i++) console.log("line " + i + " of the output");
      out = "1000000";
      break;
    case "stdout": {
      let block = [];
      for (let i = 0; i < 1000000; i++) {
        block.push("line " + i + " of the output\n");
        if (block.length === 1024) {
          await Bun.write(Bun.stdout, block.join(""));
          block = [];
        }
      }
      await Bun.write(Bun.stdout, block.join(""));
      out = "1000000";
      break;
    }
    case "url": {
      let total = 0;
      for (let i = 0; i < 200000; i++) {
        const w = syllables[i % 10] + syllables[Math.floor(i / 10) % 10];
        const u = new URL(`https://user${i % 7}@host${i % 50}.example.com:${8000 + (i % 1000)}/a/${w}/${i}?q=${w}+x${i}&page=${i % 10}#f${i}`);
        total += u.hostname.length + u.pathname.length + (u.searchParams.get("q") ?? "").length + Number(u.port || 0);
        total += utf8len(new URLSearchParams({ q: `${w} & ${i}/é` }).toString()) - 2;
      }
      out = String(total);
      break;
    }
    case "base64": {
      let total = 0;
      for (let r = 0; r < 4; r++) {
        const s = small.toString("base64");
        total += s.length + Buffer.from(s, "base64").length;
      }
      out = String(total);
      break;
    }
    case "hex": {
      let total = 0;
      for (let r = 0; r < 2; r++) {
        const s = small.toString("hex");
        total += s.length + Buffer.from(s, "hex").length;
      }
      out = String(total);
      break;
    }
    case "html": out = "n/a"; break; // Bun has escapeHTML, but nothing that reads references back
    case "zip": out = "n/a"; break;
    case "tar": out = "n/a"; break;
    case "sha1":
    case "md5": {
      let h;
      for (let r = 0; r < 4; r++) h = createHash(name).update(blob).digest("hex");
      out = h;
      break;
    }
    case "crc32": {
      let c = 0;
      for (let r = 0; r < 4; r++) c = Bun.hash.crc32(blob, c);
      out = String(c);
      break;
    }
    case "toml": out = String(JSON.stringify(Bun.TOML.parse(tomlIn)).length); break;
    case "sha256": {
      let h;
      for (let r = 0; r < 4; r++) h = createHash("sha256").update(blob).digest("hex");
      out = h;
      break;
    }
    case "sha512": {
      let h;
      for (let r = 0; r < 4; r++) h = createHash("sha512").update(blob).digest("hex");
      out = h;
      break;
    }
    case "hmac": {
      const key = Buffer.from("the key");
      let msg = Buffer.from("a message of some sixty-four bytes, as a request's body might be");
      for (let i = 0; i < 500000; i++) msg = createHmac("sha256", key).update(msg).digest();
      out = msg.toString("hex");
      break;
    }
    case "aesgcm": out = aead(blob, "aes-256-gcm", 12); break;
    case "chacha":
      try {
        out = aead(blob, "chacha20-poly1305", 12);
      } catch {
        out = "n/a"; // Bun's node:crypto has no ChaCha20-Poly1305
      }
      break;
    case "bcrypt": {
      let total = 0;
      for (let i = 0; i < 4; i++) total += (await Bun.password.hash("password", { algorithm: "bcrypt", cost: 10 })).length;
      out = String(total);
      break;
    }
    case "gzip": {
      const packed = gzipSync(blob, { level: 6 });
      let total = packed.length;
      for (let r = 0; r < 3; r++) total += gunzipSync(packed).length;
      out = String(total);
      break;
    }
    case "bigint": out = bigints(10000); break;
    case "modpow": out = modpows(20); break;
    case "time": out = times(300000); break;
    case "semver": out = semvers(200000); break;
    case "seq": out = seqs(5000000); break;
    default: out = "n/a";
  }
}
process.stderr.write(`${out}\t${Bun.nanoseconds() - t0}\n`);
