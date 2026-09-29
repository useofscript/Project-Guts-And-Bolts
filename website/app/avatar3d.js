// Avatars in 3D, drawn like the game draws them: the same body model
// (player.obj, a copy of assets/models/player.obj), the same face and hats
// (Player::buildRig / Player::applyHat), your colours and the clothes you wear.
//
// One hidden WebGL canvas does all the drawing (browsers only allow a few);
// big avatars copy each frame onto their own canvas, small ones become images.

const SCALE = 0.5;   // the model is Roblox-sized; characters in the game are half that
const PART_OF = { Torso: 'torso', Head: 'head', Left_Arm: 'leftArm', Right_Arm: 'rightArm', Left_Leg: 'leftLeg', Right_Leg: 'rightLeg' };
const HAT_COLORS = { 1: [30, 30, 36], 2: [204, 46, 41], 3: [240, 190, 40], 4: [219, 51, 158] };

// --- the body model --------------------------------------------------------------

let modelPromise = null;
function loadModel() {
  if (!modelPromise) {
    modelPromise = fetch(new URL('player.obj', import.meta.url)).then((r) => {
      if (!r.ok) throw new Error('no model');
      return r.text();
    }).then(parseObj);
  }
  return modelPromise;
}

// Each named object -> triangles (positions + normals), in game units.
function parseObj(text) {
  const v = [], vn = [], parts = {};
  let cur = null;
  for (const line of text.split('\n')) {
    const w = line.trim().split(/\s+/);
    if (w[0] === 'o') { cur = parts[w[1]] = { pos: [], nrm: [], min: [1e9, 1e9, 1e9], max: [-1e9, -1e9, -1e9] }; }
    else if (w[0] === 'v') v.push([+w[1] * SCALE, +w[2] * SCALE, +w[3] * SCALE]);
    else if (w[0] === 'vn') vn.push([+w[1], +w[2], +w[3]]);
    else if (w[0] === 'f' && cur) {
      const corners = w.slice(1).map((c) => c.split('/').map((x) => parseInt(x, 10)));
      for (let i = 1; i + 1 < corners.length; ++i) {
        for (const c of [corners[0], corners[i], corners[i + 1]]) {
          const p = v[c[0] - 1], n = c[2] ? vn[c[2] - 1] : [0, 1, 0];
          cur.pos.push(...p); cur.nrm.push(...n);
          for (let k = 0; k < 3; ++k) { cur.min[k] = Math.min(cur.min[k], p[k]); cur.max[k] = Math.max(cur.max[k], p[k]); }
        }
      }
    }
  }
  return parts;
}

// --- simple shapes (the game's Cube / Cylinder / Sphere, 1 unit across) ------------

function cube() {
  const pos = [], nrm = [];
  const faces = [[[1, 0, 0], [0, 1, 0], [0, 0, 1]], [[-1, 0, 0], [0, 1, 0], [0, 0, -1]], [[0, 1, 0], [0, 0, 1], [1, 0, 0]],
    [[0, -1, 0], [0, 0, -1], [1, 0, 0]], [[0, 0, 1], [-1, 0, 0], [0, 1, 0]], [[0, 0, -1], [1, 0, 0], [0, 1, 0]]];
  for (const [n, u, t] of faces) {
    const c = (a, b) => n.map((x, i) => x * 0.5 + u[i] * a * 0.5 + t[i] * b * 0.5);
    const q = [c(-1, -1), c(1, -1), c(1, 1), c(-1, -1), c(1, 1), c(-1, 1)];
    for (const p of q) { pos.push(...p); nrm.push(...n); }
  }
  return { pos, nrm };
}
function cylinder(seg = 28) {
  const pos = [], nrm = [];
  for (let i = 0; i < seg; ++i) {
    const a0 = (i / seg) * Math.PI * 2, a1 = ((i + 1) / seg) * Math.PI * 2;
    const x0 = Math.cos(a0) * 0.5, z0 = Math.sin(a0) * 0.5, x1 = Math.cos(a1) * 0.5, z1 = Math.sin(a1) * 0.5;
    const n0 = [Math.cos(a0), 0, Math.sin(a0)], n1 = [Math.cos(a1), 0, Math.sin(a1)];
    pos.push(x0, -0.5, z0, x1, 0.5, z1, x1, -0.5, z1, x0, -0.5, z0, x0, 0.5, z0, x1, 0.5, z1);
    nrm.push(...n0, ...n1, ...n1, ...n0, ...n0, ...n1);
    pos.push(0, 0.5, 0, x1, 0.5, z1, x0, 0.5, z0); nrm.push(0, 1, 0, 0, 1, 0, 0, 1, 0);
    pos.push(0, -0.5, 0, x0, -0.5, z0, x1, -0.5, z1); nrm.push(0, -1, 0, 0, -1, 0, 0, -1, 0);
  }
  return { pos, nrm };
}
function sphere(rings = 14, seg = 24) {
  const pos = [], nrm = [];
  const pt = (r, s) => {
    const th = (r / rings) * Math.PI, ph = (s / seg) * Math.PI * 2;
    return [Math.sin(th) * Math.cos(ph), Math.cos(th), Math.sin(th) * Math.sin(ph)];
  };
  for (let r = 0; r < rings; ++r) for (let s = 0; s < seg; ++s) {
    const a = pt(r, s), b = pt(r + 1, s), c = pt(r + 1, s + 1), d = pt(r, s + 1);
    for (const p of [a, b, c, a, c, d]) { pos.push(p[0] * 0.5, p[1] * 0.5, p[2] * 0.5); nrm.push(...p); }
  }
  return { pos, nrm };
}
const SHAPES = { cube: cube(), cylinder: cylinder(), sphere: sphere() };

// --- what to draw for one avatar ------------------------------------------------

const clamp255 = (x) => Math.max(0, Math.min(255, Number(x) | 0)) / 255;
const colorOf = (c, fallback) => (Array.isArray(c) && c.length === 3 ? c : fallback).map(clamp255);

// Pieces: { shape (model part or simple shape), at, size, color, shine, glow }.
function pieces(model, av, items) {
  const a = av || {};
  const def = { head: [245, 205, 48], torso: [13, 105, 172], leftArm: [245, 205, 48], rightArm: [245, 205, 48], leftLeg: [75, 151, 75], rightLeg: [75, 151, 75] };
  const col = {};
  for (const k of Object.keys(def)) col[k] = colorOf(a[k], def[k]);
  let hat = a.hat | 0;
  let hatTint = Array.isArray(a.hatColor) && a.hatColor[0] >= 0 ? colorOf(a.hatColor) : null;
  for (const it of items || []) {
    const c = it.meta && Array.isArray(it.meta.color) ? colorOf(it.meta.color) : null;
    if (!c) continue;
    if (it.kind === 'shirt') { col.torso = c; col.leftArm = c; col.rightArm = c; }
    if (it.kind === 'pants') { col.leftLeg = c; col.rightLeg = c; }
    if (it.kind === 'hat') { hat = Number(it.meta.style) || 2; hatTint = c; }
  }
  const out = [];
  for (const [name, part] of Object.entries(model)) {
    const key = PART_OF[name];
    if (key) out.push({ mesh: part, color: col[key] });
  }
  // The face (Player::buildRig), in the head's own space.
  const head = model.Head;
  const hc = head ? head.min.map((m, i) => (m + head.max[i]) / 2) : [0, 2.325, 0];
  const hs = head ? head.min.map((m, i) => head.max[i] - m) : [0.72, 0.65, 0.72];
  const onHead = (p, s, c) => out.push({ shape: 'cube', at: [hc[0] + p[0] * hs[0], hc[1] + p[1] * hs[1], hc[2] + p[2] * hs[2]],
    size: [s[0] * hs[0], s[1] * hs[1], s[2] * hs[2]], color: c });
  const black = [0.06, 0.06, 0.07];
  // Two small oval eyes and a smooth U-shaped smile (Player::addFace).
  const surfaceZ = (x) => Math.sqrt(Math.max(0, 0.25 - x * x)) - 0.005;
  for (const x of [-0.1, 0.1]) out.push({ shape: 'sphere', at: [hc[0] + x * hs[0], hc[1] + 0.16 * hs[1], hc[2] + surfaceZ(x) * hs[2]],
    size: [0.065 * hs[0], 0.13 * hs[1], 0.05 * hs[2]], color: black });
  const curve = (x) => -0.27 + 0.22 * Math.pow(Math.abs(x) / 0.2, 1.7);
  for (let i = 0; i < 10; i++) {
    const x0 = -0.2 + 0.04 * i, x1 = x0 + 0.04, xm = (x0 + x1) / 2;
    const dx = (x1 - x0) * hs[0], dy = (curve(x1) - curve(x0)) * hs[1];
    out.push({ shape: 'cube', at: [hc[0] + xm * hs[0], hc[1] + (curve(x0) + curve(x1)) / 2 * hs[1], hc[2] + surfaceZ(xm) * hs[2]],
      size: [Math.hypot(dx, dy) + 0.035 * hs[0], 0.055 * hs[1], 0.05 * hs[2]], roll: Math.atan2(dy, dx), color: black });
  }
  // The hat (Player::applyHat).
  const top = hc[1] + hs[1] * 0.5, w = hs[0] / 0.72;
  const main = (normal) => hatTint || normal;
  const H = (shape, p, s, c, extra = {}) => out.push(Object.assign({ shape, at: [p[0] * w, p[1], p[2] * w], size: [s[0] * w, s[1], s[2] * w], color: c }, extra));
  const hatDefault = HAT_COLORS[hat] ? HAT_COLORS[hat].map(clamp255) : [0.1, 0.1, 0.1];
  if (hat === 1) {
    H('cylinder', [0, top + 0.025, 0], [1.0, 0.05, 1.0], main(hatDefault));
    H('cylinder', [0, top + 0.325, 0], [0.62, 0.55, 0.62], main(hatDefault));
    H('cylinder', [0, top + 0.11, 0], [0.64, 0.1, 0.64], [0.75, 0.12, 0.12]);
  } else if (hat === 2) {
    H('sphere', [0, top - 0.05, 0], [0.78, 0.5, 0.78], main([0.85, 0.15, 0.15]));
    H('cube', [0, top - 0.03, 0.46], [0.52, 0.04, 0.36], main([0.85, 0.15, 0.15]));
  } else if (hat === 3) {
    H('cylinder', [0, top + 0.14, 0], [0.74, 0.28, 0.74], main([1.0, 0.78, 0.2]), { shine: 1 });
    H('cube', [0, top + 0.14, 0.37], [0.12, 0.12, 0.05], [0.9, 0.1, 0.2], { glow: 1 });
  } else if (hat === 4) {   // hair (Player::applyHat, Ponytail)
    const hair = main([0.86, 0.2, 0.62]);
    H('sphere', [0, top - 0.1, -0.05], [0.8, 0.46, 0.82], hair);
    H('cube', [0, top - 0.3, -0.3], [0.74, 0.42, 0.16], hair);
    H('sphere', [0, top + 0.12, -0.3], [0.36, 0.34, 0.36], hair);
    H('sphere', [0, top - 0.14, -0.5], [0.26, 0.56, 0.26], hair);
  }
  return out;
}

// Everything in one list of triangles: position, normal, colour, shine.
function build(list) {
  const data = [];
  for (const p of list) {
    const src = p.mesh || SHAPES[p.shape];
    const at = p.at || [0, 0, 0], size = p.size || [1, 1, 1];
    const rc = Math.cos(p.roll || 0), rs = Math.sin(p.roll || 0);   // turned about Z (the smile's pieces)
    for (let i = 0; i < src.pos.length; i += 3) {
      let lx = src.pos[i] * size[0], ly = src.pos[i + 1] * size[1];
      if (p.roll) [lx, ly] = [lx * rc - ly * rs, lx * rs + ly * rc];
      const x = p.mesh ? src.pos[i] : at[0] + lx;
      const y = p.mesh ? src.pos[i + 1] : at[1] + ly;
      const z = p.mesh ? src.pos[i + 2] : at[2] + src.pos[i + 2] * size[2];
      let nx = src.nrm[i], ny = src.nrm[i + 1], nz = src.nrm[i + 2];
      if (!p.mesh) { nx /= size[0]; ny /= size[1]; nz /= size[2]; }
      if (p.roll) [nx, ny] = [nx * rc - ny * rs, nx * rs + ny * rc];
      const l = Math.hypot(nx, ny, nz) || 1;
      data.push(x, y, z, nx / l, ny / l, nz / l, p.color[0], p.color[1], p.color[2], p.glow ? 2 : p.shine ? 1 : 0);
    }
  }
  return new Float32Array(data);
}

// --- the one WebGL canvas --------------------------------------------------------

const VS = `
attribute vec3 aPos; attribute vec3 aNrm; attribute vec3 aCol; attribute float aShine;
uniform mat4 uMvp; uniform mat3 uRot;
varying vec3 vNrm; varying vec3 vCol; varying float vShine; varying float vY;
void main() {
  vNrm = uRot * aNrm; vCol = aCol; vShine = aShine; vY = aPos.y;
  gl_Position = uMvp * vec4(aPos, 1.0);
}`;
const FS = `
precision mediump float;
varying vec3 vNrm; varying vec3 vCol; varying float vShine; varying float vY;
uniform float uShadow;
void main() {
  if (uShadow > 0.5) { gl_FragColor = vec4(0.0, 0.0, 0.0, 0.22 * vCol.r); return; }
  vec3 n = normalize(vNrm);
  vec3 key = normalize(vec3(-0.45, 0.75, 0.6));
  float diff = max(dot(n, key), 0.0);
  float fill = max(dot(n, normalize(vec3(0.6, 0.2, 0.4))), 0.0) * 0.25;
  float sky = 0.5 + 0.5 * n.y;
  vec3 h = normalize(key + vec3(0.0, 0.0, 1.0));
  float spec = pow(max(dot(n, h), 0.0), vShine > 0.5 ? 40.0 : 24.0) * (vShine > 0.5 ? 0.8 : 0.18);
  vec3 c = vCol * (0.32 + 0.18 * sky + 0.62 * diff + fill) + vec3(spec);
  if (vShine > 1.5) c = vCol * 1.6;
  gl_FragColor = vec4(pow(c, vec3(0.95)), 1.0);
}`;

let gl = null, glCanvas = null, prog = null, vbo = null, shadowVbo = null, loc = null;

function initGl() {
  if (gl) return gl;
  glCanvas = document.createElement('canvas');
  gl = glCanvas.getContext('webgl', { antialias: true, alpha: true, premultipliedAlpha: false }) ||
       glCanvas.getContext('experimental-webgl');
  if (!gl) return null;
  const sh = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); return s; };
  prog = gl.createProgram();
  gl.attachShader(prog, sh(gl.VERTEX_SHADER, VS));
  gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, FS));
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) { gl = null; return null; }
  loc = {
    pos: gl.getAttribLocation(prog, 'aPos'), nrm: gl.getAttribLocation(prog, 'aNrm'),
    col: gl.getAttribLocation(prog, 'aCol'), shine: gl.getAttribLocation(prog, 'aShine'),
    mvp: gl.getUniformLocation(prog, 'uMvp'), rot: gl.getUniformLocation(prog, 'uRot'),
    shadow: gl.getUniformLocation(prog, 'uShadow'),
  };
  vbo = gl.createBuffer();
  // A soft round shadow under the feet (rings fading out).
  const d = [];
  const ring = (r0, r1, a0, a1) => {
    for (let i = 0; i < 32; ++i) {
      const t0 = (i / 32) * Math.PI * 2, t1 = ((i + 1) / 32) * Math.PI * 2;
      const P = (r, t, a) => [Math.cos(t) * r, 0.001, Math.sin(t) * r * 0.8, 0, 1, 0, a, 0, 0, 0];
      d.push(...P(r0, t0, a0), ...P(r1, t0, a1), ...P(r1, t1, a1), ...P(r0, t0, a0), ...P(r1, t1, a1), ...P(r0, t1, a0));
    }
  };
  ring(0, 0.55, 1, 0.8); ring(0.55, 0.95, 0.8, 0);
  shadowVbo = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, shadowVbo);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(d), gl.STATIC_DRAW);
  return gl;
}

// Column-major 4x4 helpers.
function perspective(fovY, aspect, near, far) {
  const f = 1 / Math.tan(fovY / 2), nf = 1 / (near - far);
  return [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0];
}
function mul(a, b) {
  const o = new Array(16).fill(0);
  for (let c = 0; c < 4; ++c) for (let r = 0; r < 4; ++r) for (let k = 0; k < 4; ++k) o[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
  return o;
}

function draw(buffer, count, w, h, yaw, pitch) {
  if (glCanvas.width !== w || glCanvas.height !== h) { glCanvas.width = w; glCanvas.height = h; }
  gl.viewport(0, 0, w, h);
  gl.clearColor(0, 0, 0, 0);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.useProgram(prog);
  // Look at the middle of the character from the front, a little from above.
  const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
  // model: turn about Y; view: tilt about X, then back off.
  const rotY = [cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1];
  const center = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1.45, 0, 1];
  const tilt = [1, 0, 0, 0, 0, cp, -sp, 0, 0, sp, cp, 0, 0, 0, 0, 1];
  const back = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -7.2, 1];
  const proj = perspective(0.42, w / h, 0.5, 50);
  const view = mul(back, mul(tilt, center));
  const mvp = mul(proj, mul(view, rotY));
  const vr = mul(tilt, rotY);   // normals only turn
  gl.uniformMatrix4fv(loc.mvp, false, new Float32Array(mvp));
  gl.uniformMatrix3fv(loc.rot, false, new Float32Array([vr[0], vr[1], vr[2], vr[4], vr[5], vr[6], vr[8], vr[9], vr[10]]));
  const attrs = () => {
    const S = 40;
    gl.enableVertexAttribArray(loc.pos); gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, S, 0);
    gl.enableVertexAttribArray(loc.nrm); gl.vertexAttribPointer(loc.nrm, 3, gl.FLOAT, false, S, 12);
    gl.enableVertexAttribArray(loc.col); gl.vertexAttribPointer(loc.col, 3, gl.FLOAT, false, S, 24);
    gl.enableVertexAttribArray(loc.shine); gl.vertexAttribPointer(loc.shine, 1, gl.FLOAT, false, S, 36);
  };
  // Shadow first (no depth), then the body.
  gl.disable(gl.DEPTH_TEST);
  gl.enable(gl.BLEND);
  gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  gl.bindBuffer(gl.ARRAY_BUFFER, shadowVbo);
  attrs();
  gl.uniform1f(loc.shadow, 1);
  gl.drawArrays(gl.TRIANGLES, 0, 32 * 12);
  gl.disable(gl.BLEND);
  gl.enable(gl.DEPTH_TEST);
  gl.uniform1f(loc.shadow, 0);
  gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
  gl.bufferData(gl.ARRAY_BUFFER, buffer, gl.DYNAMIC_DRAW);
  attrs();
  gl.drawArrays(gl.TRIANGLES, 0, count);
}

export function supported() { return !!initGl(); }

// --- avatars on the page ---------------------------------------------------------

// A turnable 3D avatar inside `el` (replacing what's there once it's ready).
// Returns { set(avatar, items) } to change it, or null (no WebGL: keep the 2D one).
export async function mountAvatar(el, avatar, items = [], opts = {}) {
  if (!initGl()) return null;
  let model;
  try { model = await loadModel(); } catch { return null; }
  if (!el.isConnected) return null;
  const width = opts.width || 220, height = opts.height || Math.round(width * 1.25);
  const canvas = document.createElement('canvas');
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  canvas.width = Math.round(width * dpr);
  canvas.height = Math.round(height * dpr);
  canvas.style.width = width + 'px';
  canvas.style.height = height + 'px';
  canvas.style.cursor = 'grab';
  canvas.style.touchAction = 'pan-y';
  canvas.title = 'Drag to turn';
  canvas.setAttribute('role', 'img');
  canvas.setAttribute('aria-label', 'Avatar in 3D');
  const ctx = canvas.getContext('2d');
  let buffer = build(pieces(model, avatar, items)), count = buffer.length / 10;
  let yaw = opts.yaw ?? -0.35, spin = 0, dragging = false, lastX = 0, frame = 0;
  const paint = () => {
    draw(buffer, count, canvas.width, canvas.height, yaw, 0.12);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(glCanvas, 0, 0);
  };
  const loop = () => {
    frame = 0;
    if (!canvas.isConnected) return;
    if (!dragging && Math.abs(spin) > 0.0005) { yaw += spin; spin *= 0.92; }
    paint();
    if (dragging || Math.abs(spin) > 0.0005) frame = requestAnimationFrame(loop);
  };
  const kick = () => { if (!frame) frame = requestAnimationFrame(loop); };
  canvas.addEventListener('pointerdown', (e) => {
    dragging = true; lastX = e.clientX; spin = 0;
    canvas.setPointerCapture(e.pointerId); canvas.style.cursor = 'grabbing'; kick();
  });
  canvas.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const dx = e.clientX - lastX;
    lastX = e.clientX;
    yaw += dx * 0.012; spin = dx * 0.012;
  });
  const up = () => { dragging = false; canvas.style.cursor = 'grab'; kick(); };
  canvas.addEventListener('pointerup', up);
  canvas.addEventListener('pointercancel', up);
  canvas.addEventListener('dblclick', () => { yaw = opts.yaw ?? -0.35; spin = 0; kick(); });
  el.replaceChildren(canvas);
  paint();
  return {
    set(av, its = []) { buffer = build(pieces(model, av, its)); count = buffer.length / 10; paint(); },
  };
}

// A still picture of an avatar (for friends lists): a data: URL, or null.
const pictureCache = new Map();
export async function avatarPicture(avatar, items = [], size = 96) {
  if (!initGl()) return null;
  let model;
  try { model = await loadModel(); } catch { return null; }
  const key = JSON.stringify([avatar, (items || []).map((i) => i.id), size]);
  if (pictureCache.has(key)) return pictureCache.get(key);
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const buffer = build(pieces(model, avatar, items));
  draw(buffer, buffer.length / 10, Math.round(size * dpr), Math.round(size * 1.25 * dpr), -0.35, 0.12);
  const url = glCanvas.toDataURL('image/png');
  pictureCache.set(key, url);
  return url;
}
