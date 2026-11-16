// asuka/shader on the web: the program's GLSL ES 3.00 (compiled with the
// app, Program's glsl) drawn with WebGL 2 over the view's box. Its
// uniforms are U0, U1... in the order of the code's, their values the
// lanes the app's uniforms give; the view sets rectSize, pixelScale and
// Time. Animated, it draws at each animation frame.

const vertex = `#version 300 es
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}`;

/** decodeCode reads a program's code (internal/shadercode's Encode): its lanes and uniforms. */
function decodeCode(b) {
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  let at = 0;
  const u32 = () => { const x = at + 4 <= b.length ? v.getUint32(at, true) : 0; at += 4; return x; };
  const str = () => { const n = u32(); const s = new TextDecoder().decode(b.subarray(at, at + n)); at += n; return s; };
  str(); str(); str(); // Kage, Metal, AGSL
  const lanes = u32(), size = u32(), scale = u32(), time = u32() - 1;
  u32(); // Metal's buffer size
  for (let i = 0; i < lanes; i++) u32();
  const n = u32();
  const uniforms = [];
  for (let i = 0; i < n && at < b.length; i++) {
    const name = str(), lane = u32(), count = u32();
    const kind = at < b.length ? b[at++] : 0;
    uniforms.push({ name, lane, lanes: count, kind });
  }
  return { lanes, size, scale, time, uniforms };
}

/** words are the lanes the app's uniforms set: a float's bits, an int's, a bool's 0 or 1. */
function words(code, us) {
  const w = new Uint32Array(code.lanes);
  const f = new Float32Array(w.buffer), i = new Int32Array(w.buffer);
  for (const u of us) {
    if (u.name.startsWith("__")) continue;
    const c = code.uniforms.find((x) => x.name === u.name);
    if (!c) continue;
    for (let j = 0; j < Math.min(c.lanes, u.value.length); j++) {
      const x = u.value[j], at = c.lane + j;
      if (at >= w.length) break;
      if (c.kind === 0) f[at] = x;
      else if (c.kind === 3) w[at] = x ? 1 : 0;
      else i[at] = Math.round(x);
    }
  }
  return w;
}

export function makeShader(events) {
  const el = document.createElement("div");
  el.style.cssText = "display:flex;overflow:hidden";
  const c = document.createElement("canvas");
  c.style.cssText = "display:block;width:100%;height:100%";
  el.appendChild(c);
  const s = { code: null, glsl: "", lanes: new Uint32Array(0), uniforms: [], program: null, gl: null, animated: false, dead: false, ticking: false, start: performance.now(), size: [0, 0] };

  const compile = () => {
    const gl = s.gl;
    const sh = (type, src) => {
      const x = gl.createShader(type);
      gl.shaderSource(x, src);
      gl.compileShader(x);
      if (!gl.getShaderParameter(x, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(x));
      return x;
    };
    const prog = gl.createProgram();
    gl.attachShader(prog, sh(gl.VERTEX_SHADER, vertex));
    gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, s.glsl));
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
    // Where each active uniform's values come from: its uniform's lanes.
    const setters = [];
    const count = gl.getProgramParameter(prog, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < count; i++) {
      const info = gl.getActiveUniform(prog, i);
      const m = /^U(\d+)(\[0\])?$/.exec(info.name);
      if (!m) continue;
      const u = s.code.uniforms[Number(m[1])];
      if (!u) continue;
      setters.push({ loc: gl.getUniformLocation(prog, info.name), type: info.type, lane: u.lane, lanes: u.lanes });
    }
    s.program = prog;
    s.setters = setters;
  };

  const draw = () => {
    if (!s.glsl || !s.code || s.dead) return;
    const [w, hh] = s.size;
    if (w <= 0 || hh <= 0) return;
    const k = window.devicePixelRatio || 1;
    const pw = Math.max(1, Math.round(w * k)), ph = Math.max(1, Math.round(hh * k));
    if (c.width !== pw) c.width = pw;
    if (c.height !== ph) c.height = ph;
    if (!s.gl) s.gl = c.getContext("webgl2", { premultipliedAlpha: true, alpha: true, antialias: false });
    const gl = s.gl;
    if (!gl) return;
    if (!s.program) {
      try { compile(); } catch (e) { console.warn("asuka: shader:", e.message); s.glsl = ""; return; }
    }
    const code = s.code;
    const lanes = new Uint32Array(code.lanes);
    lanes.set(s.lanes.subarray(0, lanes.length));
    const f = new Float32Array(lanes.buffer);
    f[code.size] = w;
    f[code.size + 1] = hh;
    f[code.scale] = k;
    if (code.time >= 0) f[code.time] = (performance.now() - s.start) / 1000;
    const i = new Int32Array(lanes.buffer);
    gl.viewport(0, 0, pw, ph);
    gl.useProgram(s.program);
    for (const x of s.setters) {
      const fl = f.subarray(x.lane, x.lane + x.lanes), it = i.subarray(x.lane, x.lane + x.lanes), ui = lanes.subarray(x.lane, x.lane + x.lanes);
      switch (x.type) {
        case gl.FLOAT: gl.uniform1fv(x.loc, fl); break;
        case gl.FLOAT_VEC2: gl.uniform2fv(x.loc, fl); break;
        case gl.FLOAT_VEC3: gl.uniform3fv(x.loc, fl); break;
        case gl.FLOAT_VEC4: gl.uniform4fv(x.loc, fl); break;
        case gl.FLOAT_MAT2: gl.uniformMatrix2fv(x.loc, false, fl); break;
        case gl.FLOAT_MAT3: gl.uniformMatrix3fv(x.loc, false, fl); break;
        case gl.FLOAT_MAT4: gl.uniformMatrix4fv(x.loc, false, fl); break;
        case gl.INT: case gl.BOOL: gl.uniform1iv(x.loc, it); break;
        case gl.INT_VEC2: case gl.BOOL_VEC2: gl.uniform2iv(x.loc, it); break;
        case gl.INT_VEC3: case gl.BOOL_VEC3: gl.uniform3iv(x.loc, it); break;
        case gl.INT_VEC4: case gl.BOOL_VEC4: gl.uniform4iv(x.loc, it); break;
        case gl.UNSIGNED_INT: gl.uniform1uiv(x.loc, ui); break;
        case gl.UNSIGNED_INT_VEC2: gl.uniform2uiv(x.loc, ui); break;
        case gl.UNSIGNED_INT_VEC3: gl.uniform3uiv(x.loc, ui); break;
        case gl.UNSIGNED_INT_VEC4: gl.uniform4uiv(x.loc, ui); break;
      }
    }
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  };

  const tick = () => {
    if (s.ticking) return;
    s.ticking = true;
    const f = () => {
      if (!s.animated || s.dead) { s.ticking = false; return; }
      draw();
      requestAnimationFrame(f);
    };
    requestAnimationFrame(f);
  };

  const ro = new ResizeObserver(() => {
    s.size = [el.clientWidth, el.clientHeight];
    draw();
  });
  ro.observe(el);

  return {
    el,
    setProgram(p) {
      s.code = decodeCode(p.code);
      s.glsl = p.glsl;
      s.program = null;
      s.lanes = words(s.code, s.uniforms);
      draw();
    },
    setUniforms(us) {
      s.uniforms = us;
      if (s.code) s.lanes = words(s.code, us);
      draw();
    },
    setAnimated(on) {
      s.animated = on;
      if (on) tick();
    },
    measure() { return [0, 0]; },
    destroy() { s.dead = true; ro.disconnect(); },
  };
}
