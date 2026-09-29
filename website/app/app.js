// The Guts&Bolts web app: the Player app's site pages (games, catalog,
// create, friends, people, groups, Bolts, sign up / log in) in a browser.
// Pages are picked by the address after '#', like #/games or #/user/12.
import * as gb from './gb.js';

// --- small helpers ------------------------------------------------------------

class Raw { constructor(s) { this.s = s; } }
const raw = (s) => new Raw(s);
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
function put(v) {
  if (v instanceof Raw) return v.s;
  if (Array.isArray(v)) return v.map(put).join('');
  if (v === null || v === undefined || v === false) return '';
  return esc(v);
}
// html`...` escapes everything put into it, so names people type can't become code.
function html(strings, ...vals) {
  let out = '';
  strings.forEach((s, i) => { out += s; if (i < vals.length) out += put(vals[i]); });
  return raw(out);
}

const $ = (sel, root = document) => root.querySelector(sel);

function show(content) { view.innerHTML = content.s; }
const view = $('#view');
const KINDS = { hat: 'Hat', shirt: 'Shirt', pants: 'Pants', audio: 'Audio', plugin: 'Plugin', game: 'Game', decal: 'Decal' };
const FEES = { decal: 5, hat: 10, shirt: 10, pants: 10, audio: 20, plugin: 20, game: 0 };

let me = null;          // our account on the server (from "hello")
let pageToken = 0;      // goes up with every page change; a slower old page must not draw over a newer one
class Stale extends Error {}
let serverName = '';
let route = [];

function toast(text, ms = 3500) {
  const t = $('#toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { t.hidden = true; }, ms);
}

function ago(t) {
  const d = Math.floor(Date.now() / 1000) - t;
  if (d < 120) return 'just now';
  if (d < 7200) return Math.floor(d / 60) + ' minutes ago';
  if (d < 172800) return Math.floor(d / 3600) + ' hours ago';
  return Math.floor(d / 86400) + ' days ago';
}

const boltIcon = raw('<svg class="bolt" viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="7" fill="#f2b705" stroke="#a87400"/><path d="M5 7h6M5 9h6" stroke="#7a5300" stroke-width="1.4"/></svg>');
const checkIcon = raw('<svg class="check" viewBox="0 0 16 16" aria-label="Verified"><circle cx="8" cy="8" r="7" fill="#1d9bf0"/><path d="M4.5 8.2l2.3 2.3 4.7-4.9" stroke="#fff" stroke-width="1.8" fill="none"/></svg>');
const bolts = (n) => html`<span class="price">${boltIcon}${Number(n).toLocaleString()}</span>`;
const verified = (v) => (v ? checkIcon : '');
const signedIn = () => me && me.userId > 0;

// A game's picture: a colourful gradient made from its id (like the Player).
function gameColors(id) {
  let h = 2166136261;
  for (const c of String(id)) h = Math.imul(h ^ c.charCodeAt(0), 16777619) >>> 0;
  const col = (shift, base) => base + ((h >>> shift) & 0x5f);
  return `linear-gradient(rgb(${col(0, 40)},${col(8, 60)},${col(16, 110)}), rgb(${col(4, 20)},${col(12, 30)},${col(20, 60)}))`;
}

// Hats, shirts and pants, drawn in their colour.
function itemIcon(a) {
  const m = a.meta || {};
  const c = Array.isArray(m.color) ? `rgb(${m.color.map((x) => Number(x) | 0).join(',')})` : '#c33';
  const k = a.kind;
  let shape;
  if (k === 'hat') {
    const style = Number(m.style) || 2;
    shape = style === 1
      ? `<rect x="30" y="18" width="40" height="46" rx="3" fill="${c}"/><rect x="18" y="62" width="64" height="9" rx="3" fill="${c}"/><rect x="30" y="52" width="40" height="7" fill="rgba(0,0,0,.25)"/>`
      : style === 3
        ? `<path d="M22 70 L26 32 L40 50 L50 26 L60 50 L74 32 L78 70 Z" fill="${c}"/><rect x="22" y="64" width="56" height="9" fill="rgba(0,0,0,.2)"/>`
        : `<path d="M22 64 Q24 30 50 30 Q76 30 78 64 Z" fill="${c}"/><path d="M50 64 L92 64 Q92 72 80 72 L50 72 Z" fill="${c}"/><circle cx="50" cy="31" r="4" fill="rgba(0,0,0,.25)"/>`;
  } else if (k === 'shirt') {
    shape = `<path d="M34 20 L18 30 L24 46 L32 42 L32 82 L68 82 L68 42 L76 46 L82 30 L66 20 Q50 30 34 20 Z" fill="${c}"/>`;
  } else if (k === 'pants') {
    shape = `<path d="M30 18 L70 18 L74 84 L56 84 L50 42 L44 84 L26 84 Z" fill="${c}"/><rect x="30" y="18" width="40" height="7" fill="rgba(0,0,0,.2)"/>`;
  } else {
    return html`<span>${KINDS[k] || '?'}</span>`;
  }
  return raw(`<svg viewBox="0 0 100 100" width="80%" height="80%" aria-hidden="true">${shape}</svg>`);
}

// A game's picture (set when it's published from Studio), or a colourful card.
function gamePic(g, cls = 'pic') {
  if (g.thumb) return html`<div class="${cls}" style="background:#223 center / cover no-repeat url('/thumb/${encodeURIComponent(g.id)}?v=${g.thumb}')"
    role="img" aria-label="${g.name}"></div>`;
  return html`<div class="${cls}" style="background:${raw(gameColors(g.id))}">${g.name}</div>`;
}

// --- avatars ---
// The same colours and hats as the Player (Player::colorPresets, HatStyle).
const PRESETS = [
  ['Classic Noob', [[245, 205, 48], [13, 105, 172], [245, 205, 48], [245, 205, 48], [75, 151, 75], [75, 151, 75]]],
  ['Guest', [[163, 163, 168], [31, 31, 36], [163, 163, 168], [163, 163, 168], [217, 217, 222], [217, 217, 222]]],
  ['Builder', [[245, 204, 166], [237, 140, 41], [245, 204, 166], [245, 204, 166], [89, 69, 51], [89, 69, 51]]],
  ['Ninja', [[31, 31, 36], [31, 31, 36], [31, 31, 36], [31, 31, 36], [31, 31, 36], [31, 31, 36]]],
  ['Red Team', [[245, 204, 166], [204, 46, 41], [245, 204, 166], [245, 204, 166], [31, 31, 36], [31, 31, 36]]],
  ['Blue Team', [[245, 204, 166], [41, 92, 217], [245, 204, 166], [245, 204, 166], [31, 31, 36], [31, 31, 36]]],
  ['Robot', [[184, 189, 199], [115, 122, 135], [184, 189, 199], [184, 189, 199], [115, 122, 135], [115, 122, 135]]],
];
const PARTS = ['head', 'torso', 'leftArm', 'rightArm', 'leftLeg', 'rightLeg'];
const PART_NAMES = { head: 'Head', torso: 'Torso', leftArm: 'Left Arm', rightArm: 'Right Arm', leftLeg: 'Left Leg', rightLeg: 'Right Leg' };
const HATS = ['None', 'Top Hat', 'Cap', 'Crown'];
const HAT_COLORS = { 1: [30, 30, 36], 2: [204, 46, 41], 3: [240, 190, 40] };   // a hat's normal colours

function defaultAvatar() {
  const a = { hat: 0, hatColor: [-1, -1, -1], wearing: [] };
  PARTS.forEach((p, i) => { a[p] = PRESETS[0][1][i].slice(); });
  return a;
}
const rgbCss = (c) => `rgb(${c.map((x) => Math.max(0, Math.min(255, Number(x) | 0))).join(',')})`;
const hexOf = (c) => '#' + c.map((x) => (Math.max(0, Math.min(255, x | 0))).toString(16).padStart(2, '0')).join('');
const fromHex = (h) => [1, 3, 5].map((i) => parseInt(h.substr(i, 2), 16));

// A blocky character, front view. Clothes from the catalog colour the parts they cover.
function avatarSvg(av, size = 160, items = []) {
  const a = Object.assign(defaultAvatar(), av || {});
  const col = {};
  PARTS.forEach((p) => { col[p] = rgbCss(Array.isArray(a[p]) ? a[p] : defaultAvatar()[p]); });
  let hat = a.hat | 0, hatCol = Array.isArray(a.hatColor) && a.hatColor[0] >= 0 ? rgbCss(a.hatColor) : rgbCss(HAT_COLORS[hat] || [0, 0, 0]);
  for (const it of items) {
    const c = it.meta && Array.isArray(it.meta.color) ? rgbCss(it.meta.color) : null;
    if (!c) continue;
    if (it.kind === 'shirt') { col.torso = c; col.leftArm = c; col.rightArm = c; }
    if (it.kind === 'pants') { col.leftLeg = c; col.rightLeg = c; }
    if (it.kind === 'hat') { hat = Number(it.meta.style) || 2; hatCol = c; }
  }
  const hats = {
    1: `<rect x="37" y="-6" width="26" height="22" rx="2" fill="${hatCol}"/><rect x="30" y="13" width="40" height="5" rx="2" fill="${hatCol}"/><rect x="37" y="9" width="26" height="4" fill="rgba(0,0,0,.25)"/>`,
    2: `<path d="M34 17 Q35 3 50 3 Q65 3 66 17 Z" fill="${hatCol}"/><path d="M50 15 L76 15 Q76 19 70 19 L50 19 Z" fill="${hatCol}"/>`,
    3: `<path d="M35 16 L36 2 L43 9 L50 0 L57 9 L64 2 L65 16 Z" fill="${hatCol}"/><rect x="35" y="13" width="30" height="4" fill="rgba(0,0,0,.2)"/>`,
  };
  const svg = `<svg viewBox="0 -8 100 128" width="${size}" height="${size * 1.28}" role="img" aria-label="Avatar">
    <rect x="37" y="12" width="26" height="22" rx="4" fill="${col.head}" stroke="rgba(0,0,0,.25)"/>
    <circle cx="45" cy="21" r="1.8" fill="#222"/><circle cx="55" cy="21" r="1.8" fill="#222"/>
    <path d="M44 27 Q50 31 56 27" stroke="#222" stroke-width="1.6" fill="none"/>
    <rect x="30" y="35" width="40" height="38" fill="${col.torso}" stroke="rgba(0,0,0,.25)"/>
    <rect x="12" y="35" width="17" height="38" fill="${col.rightArm}" stroke="rgba(0,0,0,.25)"/>
    <rect x="71" y="35" width="17" height="38" fill="${col.leftArm}" stroke="rgba(0,0,0,.25)"/>
    <rect x="30" y="74" width="19.5" height="40" fill="${col.rightLeg}" stroke="rgba(0,0,0,.25)"/>
    <rect x="50.5" y="74" width="19.5" height="40" fill="${col.leftLeg}" stroke="rgba(0,0,0,.25)"/>
    ${hats[hat] || ''}</svg>`;
  return raw(svg);
}

function gameCard(g) {
  return html`<a class="card" href="#/game/${g.id}">
    ${gamePic(g)}
    <div class="name">${g.name}</div>
    <div class="by">by ${g.creatorName}${verified(g.creatorVerified)} · ${g.plays || 0} plays</div></a>`;
}

function itemCard(a) {
  return html`<a class="card square" href="#/item/${a.id}">
    <div class="pic">${itemIcon(a)}</div>
    <div class="name">${a.name}</div>
    <div class="by">${a.price > 0 ? bolts(a.price) : raw('<span class="muted">Free</span>')} · by ${a.creatorName}${verified(a.creatorVerified)}</div></a>`;
}

function needSignIn(what) {
  return html`<div class="box info">You need to be signed in to ${what}.
    <a class="btn blue small" href="#/login">Log in</a> <a class="btn green small" href="#/signup">Sign up</a></div>`;
}

// --- talking to the server -------------------------------------------------------

async function call(op, args) {
  const r = await gb.call(op, args);
  if (r.me) setMe(r.me);
  return r;
}

// The same, for loading a page: if the visitor has moved on to another page by
// the time the answer comes, this page stops (so it can't draw over the new one).
async function pageCall(op, args) {
  const token = pageToken;
  const r = await call(op, args);
  if (token !== pageToken) throw new Stale();
  return r;
}

function setMe(m) {
  me = m;
  const staffLink = $('#nav a[data-page=staff]');
  if (staffLink) staffLink.hidden = !(signedIn() && me.staff);
  const box = $('#me');
  if (signedIn()) {
    box.innerHTML = html`Hi, <a href="#/user/${me.userId}">${me.username}</a>${verified(me.verified)}<br>
      <a href="#/bolts">${bolts(me.bolts)}</a> · <a href="#" data-act="logout">Log out</a>`.s;
  } else {
    box.innerHTML = html`<a href="#/login">Log in</a> · <a href="#/signup"><b>Sign up</b></a>`.s;
  }
}

// How many friend requests are waiting (shown on the Friends link).
async function checkRequests() {
  const link = $('#nav a[data-page=friends]');
  if (!link) return;
  let n = 0;
  if (signedIn()) {
    const r = await gb.call('friends.list', {});
    if (r.ok) n = r.incoming.length;
  }
  link.innerHTML = 'Friends' + (n ? ` <span class="badge">${n}</span>` : '');
}

async function hello() {
  const r = await gb.call('hello', { name: signedIn() ? me.username : '', grants: [], protocol: 1 });
  const st = $('#status');
  if (r.ok) {
    serverName = (r.server && r.server.name) || 'Online';
    st.className = 'status on';
    st.querySelector('b').textContent = serverName;
    setMe(r.me);
  } else {
    st.className = 'status off';
    st.querySelector('b').textContent = 'Server offline';
    me = null;
    $('#me').innerHTML = html`<span class="muted">Offline</span>`.s;
  }
  return r;
}

// --- pages ---------------------------------------------------------------------------

const pages = {};

pages.home = async () => {
  const games = await pageCall('list', { kind: 'game', sort: 'popular', limit: 12 });
  const items = await pageCall('list', { kind: 'clothing', limit: 6 });
  let top = '';
  if (signedIn()) {
    top = html`<h1>Welcome back, ${me.username}!</h1>
      ${me.canDaily ? html`<div class="box gold row"><div class="grow"><b>Your daily Bolts are ready!</b><br>
        <span class="small">Claim 25 Bolts every day. Spend them in the Catalog.</span></div>
        <button class="btn green" data-act="daily">Claim</button></div>` : ''}`;
  } else {
    top = html`<h1>Welcome to Guts&amp;Bolts!</h1>
      <div class="box info row"><div class="grow">Build games, play them with friends and fall apart spectacularly.
        Sign up to get <b>100 Bolts</b>, make friends and upload your own stuff.</div>
        <a class="btn green" href="#/signup">Sign up</a><a class="btn" href="#/login">Log in</a></div>`;
  }
  show(html`${top}
    <h2>Popular games <a class="btn blue small" href="#/games" style="float:right">See all</a></h2>
    ${games.ok && games.assets.length ? html`<div class="grid">${games.assets.map(gameCard)}</div>`
      : html`<p class="muted">${games.ok ? 'No games published yet. Publish one from Studio!' : games.error}</p>`}
    <h2>New in the catalog <a class="btn blue small" href="#/catalog" style="float:right">See all</a></h2>
    ${items.ok && items.assets.length ? html`<div class="grid">${items.assets.map(itemCard)}</div>`
      : html`<p class="muted">Nothing here yet.</p>`}`);
};

pages.games = async () => {
  const q = new URLSearchParams(location.hash.split('?')[1] || '');
  const sort = q.get('sort') || 'popular', query = q.get('q') || '';
  show(html`<h1>Games</h1><p class="muted">Loading...</p>`);
  const r = await pageCall('list', { kind: 'game', sort, query, limit: 100 });
  show(html`<h1>Games</h1>
    <form class="row" data-form="gameSearch">
      <input type="search" name="q" placeholder="Search games" value="${query}" style="max-width:280px">
      <select name="sort" style="width:auto"><option value="popular" ${sort === 'popular' ? 'selected' : ''}>Most played</option>
        <option value="new" ${sort === 'new' ? 'selected' : ''}>Newest</option></select>
      <button class="btn blue">Search</button></form><br>
    ${r.ok ? (r.assets.length ? html`<div class="grid">${r.assets.map(gameCard)}</div>` : html`<p class="muted">No games found.</p>`)
      : html`<p class="error">${r.error}</p>`}`);
};

pages.game = async (id) => {
  const [r, s] = await Promise.all([pageCall('list', { kind: 'game', limit: 100 }), pageCall('servers.list', { game: id })]);
  const g = r.ok && r.assets.find((a) => a.id === id);
  if (!g) { show(html`<h1>Game not found</h1><p class="muted">${r.ok ? 'It may have been deleted.' : r.error}</p>`); return; }
  const servers = s.ok ? s.servers : [];
  show(html`<p><a href="#/games">&lt; Games</a></p>
    <div class="hero">${gamePic(g)}
      <div><h1>${g.name}</h1>
        <p>by <a href="#/user/${g.creator}">${g.creatorName}</a>${verified(g.creatorVerified)}</p>
        <p class="muted">${g.plays || 0} plays · published ${ago(g.created)}</p>
        <button class="btn green big" data-act="play" data-name="${g.name}">Play</button>
        <p class="small muted">Games run in the Guts&amp;Bolts app (Windows, Mac, Linux and Android).</p></div></div>
    <h2>Description</h2><p style="white-space:pre-wrap">${g.description || 'No description yet.'}</p>
    <h2>Servers</h2>
    ${servers.length ? html`<div class="list">${servers.map((sv) => html`<div><span class="grow">${sv.title || g.name}</span>
        <span class="muted">${sv.players || 1} / ${sv.max || '?'} players${sv.friends ? html` · ${sv.friends} friend(s)` : ''}</span></div>`)}</div>`
      : html`<p class="muted">Nobody's playing right now. Be the first!</p>`}`);
};

pages.catalog = async () => {
  const q = new URLSearchParams(location.hash.split('?')[1] || '');
  const kind = q.get('kind') || 'clothing', query = q.get('q') || '';
  const r = await pageCall('list', { kind, query, limit: 100 });
  const tab = (k, label) => html`<a class="btn ${kind === k ? 'blue' : ''}" href="#/catalog?kind=${k}">${label}</a>`;
  show(html`<h1>Catalog</h1>
    <div class="tabs">${tab('clothing', 'Everything')}${tab('hat', 'Hats')}${tab('shirt', 'Shirts')}${tab('pants', 'Pants')}</div>
    <form class="row" data-form="catalogSearch"><input type="hidden" name="kind" value="${kind}">
      <input type="search" name="q" placeholder="Search the catalog" value="${query}" style="max-width:280px">
      <button class="btn blue">Search</button></form><br>
    ${r.ok ? (r.assets.length ? html`<div class="grid">${r.assets.map(itemCard)}</div>` : html`<p class="muted">Nothing here yet.</p>`)
      : html`<p class="error">${r.error}</p>`}`);
};

pages.item = async (id) => {
  const r = await pageCall('list', { kind: 'clothing', limit: 100 });
  const a = r.ok && r.assets.find((x) => x.id === id);
  if (!a) { show(html`<h1>Item not found</h1>`); return; }
  const owned = signedIn() && (me.owned || []).includes(a.id);
  show(html`<p><a href="#/catalog">&lt; Catalog</a></p>
    <div class="hero"><div class="card square"><div class="pic">${itemIcon(a)}</div></div>
      <div><h1>${a.name}</h1><p>${KINDS[a.kind]} by <a href="#/user/${a.creator}">${a.creatorName}</a>${verified(a.creatorVerified)}</p>
        <p>${a.price > 0 ? bolts(a.price) : 'Free'} · <span class="muted">${a.sales || 0} sold</span></p>
        ${owned ? html`<p class="ok"><b>You own this.</b></p><p class="small muted">Wear it from the Avatar page in the Guts&amp;Bolts app.</p>`
          : signedIn() ? html`<button class="btn green big" data-act="buy" data-id="${a.id}">${a.price > 0 ? 'Buy' : 'Get it'}</button>`
            : needSignIn('buy things')}
        <p style="white-space:pre-wrap">${a.description}</p></div></div>`);
};

// Decal pictures: fetched once, shown from memory.
const pictures = new Map();
async function decalPicture(img) {
  const id = img.dataset.decal;
  if (!pictures.has(id)) {
    pictures.set(id, (async () => {
      const r = await gb.call('get', { id });
      if (!r.ok) return '';
      const type = (r.asset.meta && r.asset.meta.ext) === 'jpg' ? 'image/jpeg' : 'image/png';
      return URL.createObjectURL(new Blob([gb.base64Bytes(r.data)], { type }));
    })());
  }
  const url = await pictures.get(id);
  if (url) img.src = url;
}

pages.create = async (tab = 'games') => {
  const tabs = [['games', 'My Games'], ['decal', 'Decals'], ['audio', 'Audio'], ['hat', 'Hats'], ['shirt', 'Shirts'], ['pants', 'Pants'], ['plugin', 'Plugins']];
  const head = html`<h1>Create</h1><div class="tabs">${tabs.map(([k, l]) => html`<a class="btn ${tab === k ? 'blue' : ''}" href="#/create/${k}">${l}</a>`)}</div>`;
  if (!signedIn()) { show(html`${head}${needSignIn('upload things')}`); return; }
  const r = await pageCall('list', { creator: me.id, limit: 100 });
  const mine = r.ok ? r.assets : [];
  const kind = tab === 'games' ? 'game' : tab;
  const list = mine.filter((a) => a.kind === kind);
  const costs = me.verified ? html`<b>You're Verified!</b> Uploading is free, with no daily limit, and you can sell what you make.`
    : html`Uploading costs a few Bolts (decals 5, clothes 10, audio 20, plugins 20; games are free).
      <span class="muted">${me.uploadsLeft} uploads left today.</span>`;
  const clothing = ['hat', 'shirt', 'pants'].includes(kind);
  const accept = { decal: '.png,.jpg,.jpeg', audio: '.mp3,.wav,.ogg,.flac', plugin: '.lua', game: '.gbscene' }[kind] || '';
  const form = html`<h2>${kind === 'game' ? 'Publish a game' : 'Upload a new ' + KINDS[kind]}</h2>
    <form class="form" data-form="upload"><input type="hidden" name="kind" value="${kind}">
      <label>Name</label><input type="text" name="name" maxlength="50" required>
      <label>Description</label><textarea name="description" maxlength="1000"></textarea>
      ${clothing ? html`${kind === 'hat' ? html`<label>Style</label><select name="style"><option value="1">Top Hat</option>
          <option value="2" selected>Cap</option><option value="3">Crown</option></select>` : ''}
        <label>Colour</label><input type="color" name="color" value="#e63333">`
      : html`<label>File</label><input type="file" name="file" accept="${accept}" required>
        <p class="small muted">${{ decal: 'A .png or .jpg picture (up to 4 MB).', audio: 'An .mp3, .wav, .ogg or .flac file (up to 6 MB).',
          plugin: 'A Lua plugin for Studio.', game: 'A .gbscene file saved from Studio (or use File > Publish in Studio).' }[kind]}</p>`}
      ${me.verified && kind !== 'game' ? html`<label>Price (Bolts)</label><input type="number" name="price" min="0" value="0">` : ''}
      ${kind === 'decal' ? html`<img class="thumb" id="preview" alt="Preview" hidden style="width:120px;height:120px;margin-top:10px">` : ''}
      <p><button class="btn green">${FEES[kind] && !me.verified ? 'Upload for ' + FEES[kind] + ' Bolts' : 'Upload (free)'}</button>
        <span id="uploadMsg"></span></p></form>`;
  const row = (a) => html`<div>
      ${a.kind === 'decal' ? html`<img class="thumb" data-decal="${a.id}" alt="">` : ''}
      ${clothing ? html`<div class="thumb" style="display:flex;align-items:center;justify-content:center;background:#fff">${itemIcon(a)}</div>` : ''}
      <div class="grow"><b>${a.name}</b><br><span class="small muted">
        ${a.kind === 'game' ? html`${a.plays} plays` : html`${a.price > 0 ? bolts(a.price) : 'free'} · ${a.sales} sold`}
        ${a.kind === 'decal' || a.kind === 'audio' ? html` · ID gb:${a.id}` : ''}</span></div>
      ${a.kind === 'decal' || a.kind === 'audio' ? html`<button class="btn small" data-act="copyId" data-id="${a.id}">Copy ID</button>` : ''}
      ${a.kind === 'game' ? html`<a class="btn small" href="#/game/${a.id}">View</a>
        <button class="btn small" data-act="renameGame" data-id="${a.id}" data-name="${a.name}">Edit name</button>` : ''}
      <button class="btn small red" data-act="deleteAsset" data-id="${a.id}" data-name="${a.name}">Delete</button></div>`;
  show(html`${head}<div class="box">${costs}</div>
    ${kind === 'game' ? html`<h2>My published games</h2>` : html`<h2>My ${KINDS[kind]}${kind === 'pants' ? '' : 's'}</h2>`}
    ${list.length ? html`<div class="list">${list.map(row)}</div>` : html`<p class="muted">Nothing yet.</p>`}
    ${form}`);
  view.querySelectorAll('img[data-decal]').forEach(decalPicture);
  const file = view.querySelector('input[type=file]'), prev = $('#preview');
  if (file && prev) file.addEventListener('change', () => { if (file.files[0]) { prev.src = URL.createObjectURL(file.files[0]); prev.hidden = false; } });
};

pages.people = async () => {
  const query = new URLSearchParams(location.hash.split('?')[1] || '').get('q') || '';
  const r = await pageCall('users.search', { query });
  show(html`<h1>People</h1>
    <form class="row" data-form="peopleSearch"><input type="search" name="q" placeholder="Search by username or #number" value="${query}" style="max-width:280px">
      <button class="btn blue">Search</button></form>
    <div class="list">${r.ok ? r.users.map((u) => html`<div><a class="grow" href="#/user/${u.userId}"><b>${u.username}</b></a>${verified(u.verified)}
      <span class="muted small">#${u.userId}${u.official ? ' · Guts&Bolts staff' : ''}</span></div>`) : html`<p class="error">${r.error}</p>`}</div>`);
};

pages.user = async (id) => {
  const r = await pageCall('profile', { id });
  if (!r.ok) { show(html`<h1>Not found</h1><p class="muted">${r.error}</p>`); return; }
  const u = r.user;
  const f = r.friendship;
  const friendBtn = !signedIn() || f === 'self' ? ''
    : f === 'friends' ? html`<button class="btn small" data-act="friend" data-op="friends.remove" data-user="${u.id}">Unfriend</button>`
      : f === 'sent' ? html`<button class="btn small" data-act="friend" data-op="friends.cancel" data-user="${u.id}">Cancel request</button>`
        : f === 'received' ? html`<button class="btn green small" data-act="friend" data-op="friends.accept" data-user="${u.id}">Accept friend request</button>`
          : html`<button class="btn green small" data-act="friend" data-op="friends.add" data-user="${u.id}">Add friend</button>`;
  const games = r.creations.filter((a) => a.kind === 'game'), items = r.creations.filter((a) => ['hat', 'shirt', 'pants'].includes(a.kind));
  const worn = u.avatar && Array.isArray(u.avatar.wearing) && u.avatar.wearing.length
    ? (await pageCall('list', { kind: 'clothing', limit: 100 })).assets || [] : [];
  show(html`<div class="row top"><div class="box" style="text-align:center;margin:0 16px 10px 0">
      ${avatarSvg(u.avatar, 120, worn.filter((a) => u.avatar.wearing.includes(a.id)))}</div><div class="grow">
    <h1>${u.username}${verified(u.verified)}</h1>
    <p class="muted">User #${u.userId} · joined ${ago(u.created)} · ${r.friendCount} friends
      ${u.official ? html` · <b>Guts&amp;Bolts staff</b>` : ''}${u.banned ? html` · <span class="error">banned</span>` : ''}</p>
    ${(r.user.badges || []).length ? html`<p>Badges: ${r.user.badges.map((b) => html`<span class="btn small" style="cursor:default">${b}</span> `)}</p>` : ''}
    <p>${friendBtn}${f === 'self' ? html` <a class="btn small" href="#/avatar">Change my avatar</a>` : ''}</p></div></div>
    <h2>Games</h2>${games.length ? html`<div class="grid">${games.map(gameCard)}</div>` : html`<p class="muted">None yet.</p>`}
    <h2>Creations</h2>${items.length ? html`<div class="grid">${items.map(itemCard)}</div>` : html`<p class="muted">None yet.</p>`}
    <h2>Groups</h2>${r.groups.length ? html`<div class="list">${r.groups.map((g) => html`<div><a class="grow" href="#/group/${g.id}">${g.name}</a>
      <span class="muted small">${g.role}</span></div>`)}</div>` : html`<p class="muted">None yet.</p>`}`);
};

pages.friends = async () => {
  if (!signedIn()) { show(html`<h1>Friends</h1>${needSignIn('have friends')}`); return; }
  const r = await pageCall('friends.list', {});
  if (!r.ok) { show(html`<h1>Friends</h1><p class="error">${r.error}</p>`); return; }
  const person = (p) => html`<a class="grow" href="#/user/${p.id}"><b>${p.name}</b></a>${verified(p.verified)}`;
  show(html`<h1>Friends</h1>
    ${r.incoming.length ? html`<h2>Friend requests</h2><div class="list">${r.incoming.map((p) => html`<div>${person(p)}
      <button class="btn green small" data-act="friend" data-op="friends.accept" data-user="${p.id}">Accept</button>
      <button class="btn small" data-act="friend" data-op="friends.decline" data-user="${p.id}">Decline</button></div>`)}</div>` : ''}
    <h2>My friends (${r.friends.length})</h2>
    ${r.friends.length ? html`<div class="list">${r.friends.map((p) => html`<div><span class="dot ${p.online ? 'on' : ''}"></span>${person(p)}
      <span class="muted small">${p.playing ? 'Playing ' + p.playing.title : p.online ? 'Online' : 'Offline'}</span>
      <button class="btn small" data-act="friend" data-op="friends.remove" data-user="${p.id}">Unfriend</button></div>`)}</div>`
      : html`<p class="muted">No friends yet. Find people on the <a href="#/people">People</a> page!</p>`}
    ${r.outgoing.length ? html`<h2>Waiting for them</h2><div class="list">${r.outgoing.map((p) => html`<div>${person(p)}
      <button class="btn small" data-act="friend" data-op="friends.cancel" data-user="${p.id}">Cancel</button></div>`)}</div>` : ''}`);
};

pages.groups = async () => {
  const query = new URLSearchParams(location.hash.split('?')[1] || '').get('q') || '';
  const [r, mine] = await Promise.all([pageCall('groups.list', { query }), signedIn() ? pageCall('groups.mine', {}) : { ok: false }]);
  const row = (g) => html`<div><span style="width:14px;height:14px;border-radius:3px;background:#${Number(g.color).toString(16).padStart(6, '0')}"></span>
    <a class="grow" href="#/group/${g.id}"><b>${g.name}</b></a><span class="muted small">${g.members} members${g.open ? '' : ' · invite only'}</span></div>`;
  show(html`<h1>Groups</h1>
    ${mine.ok && mine.groups.length ? html`<h2>My groups</h2><div class="list">${mine.groups.map(row)}</div>` : ''}
    <h2>Find groups</h2>
    <form class="row" data-form="groupSearch"><input type="search" name="q" placeholder="Search groups" value="${query}" style="max-width:280px">
      <button class="btn blue">Search</button></form>
    <div class="list">${r.ok ? r.groups.map(row) : html`<p class="error">${r.error}</p>`}</div>
    <h2>Make a group</h2>
    ${signedIn() ? html`<form class="form" data-form="groupCreate">
      <label>Name</label><input type="text" name="name" maxlength="40" required>
      <label>Description</label><textarea name="description" maxlength="1000"></textarea>
      <label>Colour</label><input type="color" name="color" value="#3a7bd5">
      <label><input type="checkbox" name="open" checked> Anyone can join (untick: people ask to join)</label>
      <p><button class="btn green">Make it${me.verified ? ' (free)' : ' for 50 Bolts'}</button></p></form>` : needSignIn('make a group')}`);
};

pages.group = async (id) => {
  const r = await pageCall('groups.get', { id });
  if (!r.ok) { show(html`<h1>Group not found</h1><p class="muted">${r.error}</p>`); return; }
  const g = r.group, role = r.myRole, manage = role === 'Owner' || role === 'Admin';
  const color = '#' + Number(g.color).toString(16).padStart(6, '0');
  const join = !signedIn() ? '' : role ? (role === 'Owner' ? '' : html`<button class="btn small" data-act="group" data-op="groups.leave" data-id="${g.id}">Leave</button>`)
    : r.requested ? html`<span class="muted">You asked to join.</span>`
      : html`<button class="btn green" data-act="group" data-op="groups.join" data-id="${g.id}">${g.open ? 'Join' : 'Ask to join'}</button>`;
  show(html`<p><a href="#/groups">&lt; Groups</a></p>
    <div class="banner-color" style="background:${color}"></div>
    <div class="box" style="border-top:0;border-radius:0 0 6px 6px"><h1>${g.name}</h1>
      <p class="muted">Owned by <a href="#/user/${g.owner}">${g.ownerName}</a>${verified(g.ownerVerified)} · ${g.members} members
        ${role ? html` · you're ${role === 'Owner' ? 'the owner' : 'a' + (role === 'Admin' ? 'n admin' : ' member')}` : ''}</p>
      <p style="white-space:pre-wrap">${g.description}</p>${join}</div>
    ${r.shoutInfo && r.shoutInfo.text ? html`<div class="box info"><b>${r.shoutInfo.name}:</b> ${r.shoutInfo.text}
      <span class="small muted">· ${ago(r.shoutInfo.time)}</span></div>` : ''}
    ${manage ? html`<form class="row" data-form="groupShout"><input type="hidden" name="id" value="${g.id}">
      <input type="text" name="text" maxlength="200" placeholder="Shout something to the whole group" style="max-width:420px">
      <button class="btn blue">Shout</button></form>` : ''}
    <h2>Wall</h2>
    ${role ? html`<form class="row" data-form="groupPost"><input type="hidden" name="id" value="${g.id}">
      <input type="text" name="text" maxlength="300" placeholder="Say something" style="max-width:420px" required>
      <button class="btn blue">Post</button></form>` : ''}
    <div class="wall">${r.wall.length ? r.wall.slice().reverse().map((p) => html`<div><a href="#/user/${p.id}"><b>${p.name}</b></a>${verified(p.verified)}
      <span class="small muted">${ago(p.time)}</span>
      ${signedIn() && (p.id === me.id || manage) ? html`<button class="btn small" data-act="group" data-op="groups.deletePost" data-id="${g.id}"
        data-by="${p.id}" data-time="${p.time}">Delete</button>` : ''}<br>${p.text}</div>`) : html`<p class="muted">Nothing on the wall yet.</p>`}</div>
    ${manage && r.requests && r.requests.length ? html`<h2>Waiting to join</h2><div class="list">${r.requests.map((u) => html`<div>
      <a class="grow" href="#/user/${u.id}">${u.name}</a>
      <button class="btn green small" data-act="group" data-op="groups.request" data-id="${g.id}" data-user="${u.id}" data-accept="1">Let in</button>
      <button class="btn small" data-act="group" data-op="groups.request" data-id="${g.id}" data-user="${u.id}">Decline</button></div>`)}</div>` : ''}
    <h2>Members</h2><div class="list">${r.memberList.map((u) => html`<div><a class="grow" href="#/user/${u.id}">${u.name}</a>${verified(u.verified)}
      <span class="muted small">${u.role}</span>
      ${role === 'Owner' && u.role !== 'Owner' ? html`
        <button class="btn small" data-act="group" data-op="groups.member" data-id="${g.id}" data-user="${u.id}" data-action="${u.role === 'Admin' ? 'member' : 'admin'}">${u.role === 'Admin' ? 'Make member' : 'Make admin'}</button>` : ''}
      ${manage && u.role !== 'Owner' && u.id !== me.id && (role === 'Owner' || u.role === 'Member') ? html`
        <button class="btn small red" data-act="group" data-op="groups.member" data-id="${g.id}" data-user="${u.id}" data-action="kick">Remove</button>` : ''}</div>`)}</div>
    ${manage ? html`<h2>Settings</h2><form class="form" data-form="groupEdit"><input type="hidden" name="id" value="${g.id}">
      <label>Description</label><textarea name="description" maxlength="1000">${g.description}</textarea>
      <label>Colour</label><input type="color" name="color" value="${color}">
      <label><input type="checkbox" name="open" ${g.open ? 'checked' : ''}> Anyone can join</label>
      <p><button class="btn blue">Save</button>
      ${role === 'Owner' ? html` <button type="button" class="btn red" data-act="group" data-op="groups.delete" data-id="${g.id}">Delete group</button>` : ''}</p></form>` : ''}`);
};

let avatarDraft = null;   // the avatar being edited (saved with the Save button)

pages.avatar = async () => {
  if (!signedIn()) { show(html`<h1>Avatar</h1>${needSignIn('change your avatar')}`); return; }
  const r = await pageCall('list', { kind: 'clothing', limit: 100 });
  const owned = (r.ok ? r.assets : []).filter((a) => (me.owned || []).includes(a.id));
  if (!avatarDraft) avatarDraft = Object.assign(defaultAvatar(), JSON.parse(JSON.stringify(me.avatar || {})));
  const a = avatarDraft;
  const wornItems = owned.filter((it) => a.wearing.includes(it.id));
  show(html`<h1>Avatar</h1>
    <div class="row top">
      <div class="box" style="text-align:center;margin-right:16px"><div id="avatarPreview">${avatarSvg(a, 180, wornItems)}</div>
        <p><button class="btn green" data-act="saveAvatar">Save</button>
          <button class="btn" data-act="resetAvatar">Undo changes</button></p>
        <p class="small muted" style="max-width:200px">Your avatar is the same in the app and on the website.</p></div>
      <div class="grow">
        <h2 style="margin-top:0">Colours</h2>
        <div class="row">${PRESETS.map(([name], i) => html`<button class="btn small" data-act="avatarPreset" data-i="${i}">${name}</button>`)}</div>
        <div class="row" style="margin-top:10px">${PARTS.map((p) => html`<label style="margin:0 12px 0 0;font-weight:normal">
          <input type="color" data-avatar-part="${p}" value="${hexOf(a[p])}"> ${PART_NAMES[p]}</label>`)}</div>
        <h2>Hat</h2>
        <div class="row">${HATS.map((h, i) => html`<button class="btn small ${a.hat === i ? 'blue' : ''}" data-act="avatarHat" data-i="${i}">${h}</button>`)}</div>
        ${a.hat ? html`<p class="row"><label style="margin:0;font-weight:normal"><input type="checkbox" data-avatar-hatcolor-on
          ${a.hatColor[0] >= 0 ? 'checked' : ''}> My own colour</label>
          <input type="color" data-avatar-hatcolor value="${hexOf(a.hatColor[0] >= 0 ? a.hatColor : HAT_COLORS[a.hat])}"></p>` : ''}
        <h2>My clothes</h2>
        ${owned.length ? html`<div class="grid">${owned.map((it) => html`<div class="card square">
            <div class="pic" style="${a.wearing.includes(it.id) ? 'outline:3px solid #16a34a' : ''}">${itemIcon(it)}</div>
            <div class="name">${it.name}</div>
            <button class="btn small ${a.wearing.includes(it.id) ? 'green' : ''}" data-act="avatarWear" data-id="${it.id}" data-kind="${it.kind}">
              ${a.wearing.includes(it.id) ? 'Wearing' : 'Wear'}</button></div>`)}</div>`
          : html`<p class="muted">You don't have any clothes yet. Get some in the <a href="#/catalog">Catalog</a>!</p>`}
      </div></div>`);
  // Colours change the preview straight away (without redrawing the page, so the colour picker stays open).
  const preview = () => { $('#avatarPreview').innerHTML = avatarSvg(a, 180, wornItems).s; };
  view.querySelectorAll('[data-avatar-part]').forEach((inp) => inp.addEventListener('input', () => {
    a[inp.dataset.avatarPart] = fromHex(inp.value); preview();
  }));
  const hc = view.querySelector('[data-avatar-hatcolor]'), hcOn = view.querySelector('[data-avatar-hatcolor-on]');
  if (hc) hc.addEventListener('input', () => { a.hatColor = fromHex(hc.value); if (hcOn) hcOn.checked = true; preview(); });
  if (hcOn) hcOn.addEventListener('change', () => { a.hatColor = hcOn.checked ? fromHex(hc.value) : [-1, -1, -1]; preview(); });
};

// Staff tools (like the Player's Staff page): verify people, give Bolts, ban.
pages.staff = async () => {
  if (!signedIn() || !me.staff) { show(html`<h1>Staff</h1><p class="muted">Only Guts&amp;Bolts staff can see this page.</p>`); return; }
  const query = new URLSearchParams(location.hash.split('?')[1] || '').get('q') || '';
  const r = await pageCall('admin.find', { query });
  show(html`<h1>Staff</h1>
    <p class="muted">${me.official ? 'You\'re the official Guts account: you can verify people, make staff, give Bolts and ban.'
      : 'Staff can verify people and take Verified away.'}</p>
    <form class="row" data-form="staffSearch"><input type="search" name="q" placeholder="Search by name or account ID" value="${query}" style="max-width:320px">
      <button class="btn blue">Search</button></form>
    <div class="list">${r.ok ? r.users.map((u) => html`<div>
      <a class="grow" href="#/user/${u.id}"><b>${u.username || u.name}</b></a>${verified(u.verified)}
      <span class="small muted">${u.userId ? '#' + u.userId : 'not signed up'}${u.staff ? ' · staff' : ''}${u.banned ? ' · banned' : ''}</span>
      ${u.official ? '' : html`
        ${u.verified ? html`<button class="btn small" data-act="staff" data-op="revoke" data-key="verified" data-id="${u.id}">Unverify</button>`
          : html`<button class="btn small green" data-act="staff" data-op="grant" data-key="verified" data-id="${u.id}">Verify</button>`}
        ${me.official ? html`
          ${u.staff ? html`<button class="btn small" data-act="staff" data-op="revoke" data-key="staff" data-id="${u.id}">Remove staff</button>`
            : html`<button class="btn small" data-act="staff" data-op="grant" data-key="staff" data-id="${u.id}">Make staff</button>`}
          <button class="btn small" data-act="staff" data-op="bolts" data-id="${u.id}" data-name="${u.username || u.name}">Give Bolts</button>
          <button class="btn small red" data-act="staff" data-op="ban" data-on="${u.banned ? '' : '1'}" data-id="${u.id}">${u.banned ? 'Unban' : 'Ban'}</button>` : ''}`}
      </div>`) : html`<p class="error">${r.error}</p>`}</div>`);
};

pages.bolts = async () => {
  if (!signedIn()) { show(html`<h1>Bolts</h1>${needSignIn('have Bolts')}`); return; }
  const r = await pageCall('bolts.history', {});
  show(html`<h1>Bolts</h1>
    <div class="box row"><div class="grow" style="font-size:24px">${bolts(me.bolts)}</div>
      ${me.canDaily ? html`<button class="btn green" data-act="daily">Claim today's 25 Bolts</button>` : html`<span class="muted">Daily Bolts claimed. Come back tomorrow!</span>`}</div>
    <p class="muted">Earn Bolts every day, by playing games in the app (5 every 5 minutes, up to 50 a day) and by selling things you make.</p>
    <h2>Redeem a code</h2><form class="row" data-form="redeem"><input type="text" name="code" placeholder="BOLTS-..." style="max-width:360px">
      <button class="btn blue">Redeem</button></form>
    <h2>History</h2>
    <div class="list">${r.ok && r.history.length ? r.history.slice().reverse().map((h) => html`<div><span class="grow">${h.reason}</span>
      <span class="${h.amount >= 0 ? 'ok' : 'error'}">${h.amount >= 0 ? '+' : ''}${h.amount}</span><span class="small muted">${ago(h.time)}</span></div>`)
      : html`<p class="muted">Nothing yet.</p>`}</div>`);
};

pages.login = async () => loginPage(false);
pages.signup = async () => loginPage(true);

function loginPage(signup) {
  if (signedIn()) { show(html`<h1>You're signed in as ${me.username}.</h1><p><a class="btn" href="#/">Home</a></p>`); return; }
  show(html`<div class="form" style="margin:0 auto">
    <h1>${signup ? 'Sign Up' : 'Log In'}</h1>
    <p class="muted">${signup ? 'Make your Guts&Bolts account. It\'s free, and you get 100 Bolts!' : 'Welcome back!'}</p>
    <div class="tabs"><a class="btn ${signup ? 'blue' : ''}" href="#/signup">Sign Up</a><a class="btn ${signup ? '' : 'blue'}" href="#/login">Log In</a></div>
    <form data-form="${signup ? 'signup' : 'login'}">
      <label>Username</label><input type="text" name="username" autocomplete="username" required maxlength="20"
        ${signup ? raw('placeholder="3-20 letters or numbers"') : ''}>
      ${signup ? html`<div class="small" id="nameCheck"></div>` : ''}
      <label>Password</label><input type="password" name="password" autocomplete="${signup ? 'new-password' : 'current-password'}" required>
      ${signup ? html`<label>Password again</label><input type="password" name="password2" autocomplete="new-password" required>` : ''}
      <p><button class="btn green big" style="width:100%">${signup ? 'Sign Up' : 'Log In'}</button></p>
      <p class="error" id="loginMsg"></p></form>
    <p class="small muted">${signup ? 'Usernames can\'t be changed. Your password never leaves this page: if you forget it, nobody can get it back, so write it down somewhere safe.'
      : 'Use the same username and password as in the Guts&Bolts app.'}</p></div>`);
  if (signup) {
    const input = view.querySelector('input[name=username]'), out = $('#nameCheck');
    let timer;
    input.addEventListener('input', () => {
      clearTimeout(timer);
      const name = input.value;
      timer = setTimeout(async () => {
        if (!name) { out.textContent = ''; return; }
        const r = await gb.call('account.check', { username: name });
        if (input.value !== name || !r.ok) return;
        out.className = 'small ' + (r.available ? 'ok' : 'error');
        out.textContent = r.available ? name + ' is available!' : r.problem;
      }, 400);
    });
  }
}

// --- clicks and forms ----------------------------------------------------------

const actions = {
  async logout() {
    if (!confirm('Log out? You can log back in with your username and password.')) return;
    await gb.forgetKey();
    await hello();
    location.hash = '#/';
    render();
  },
  async daily() {
    const r = await call('bolts.daily', {});
    toast(r.ok ? 'You got 25 Bolts!' : r.error);
    render();
  },
  play(d) {
    const box = document.createElement('div');
    box.className = 'modal';
    box.innerHTML = html`<div><h1>Play ${d.name}</h1>
      <p>Games run in the <b>Guts&amp;Bolts app</b>:</p>
      <ol><li><a href="../#download">Download it</a> (Windows, Mac, Linux or Android) and open <b>Guts&amp;Bolts Player</b>.</li>
        <li>Click <b>Offline</b> in its menu bar and connect to the same server as this website.</li>
        <li>Log in with the same username and password, and find <b>${d.name}</b> under <b>Online Games</b>.</li></ol>
      <p><button class="btn blue" data-act="closeModal">OK</button></p></div>`.s;
    document.body.appendChild(box);
  },
  closeModal(d, el) { el.closest('.modal').remove(); },
  async buy(d) {
    const r = await call('buy', { id: d.id });
    toast(r.ok ? 'It\'s yours! Wear it from the Avatar page in the app.' : r.error);
    render();
  },
  async copyId(d) {
    try { await navigator.clipboard.writeText('gb:' + d.id); toast('Copied gb:' + d.id + '. Paste it into Studio.'); }
    catch { prompt('Copy this ID:', 'gb:' + d.id); }
  },
  async deleteAsset(d) {
    if (!confirm('Delete "' + d.name + '" for good?')) return;
    const r = await call('delete', { id: d.id });
    toast(r.ok ? 'Deleted.' : r.error);
    render();
  },
  async renameGame(d) {
    const name = prompt('New name for the game:', d.name);
    if (!name || name === d.name) return;
    toast('Renaming...');
    const got = await gb.call('get', { id: d.id });
    if (!got.ok) { toast(got.error); return; }
    let game;
    try { game = JSON.parse(new TextDecoder().decode(gb.base64Bytes(got.data))); } catch { toast('That game file couldn\'t be read.'); return; }
    game.info = Object.assign({}, game.info, { title: name });
    const bytes = new TextEncoder().encode(JSON.stringify(game, null, 2));
    const r = await call('update', { id: d.id, name, data: await gb.fileBase64(new Blob([bytes])) });
    toast(r.ok ? 'Renamed to "' + name + '".' : r.error);
    render();
  },
  resetAvatar() { avatarDraft = null; render(); },
  avatarPreset(d) { PRESETS[Number(d.i)][1].forEach((c, i) => { avatarDraft[PARTS[i]] = c.slice(); }); render(); },
  avatarHat(d) { avatarDraft.hat = Number(d.i); avatarDraft.hatColor = [-1, -1, -1]; render(); },
  avatarWear(d) {
    const w = avatarDraft.wearing;
    if (w.includes(d.id)) w.splice(w.indexOf(d.id), 1);
    else if (w.length < 8) w.push(d.id);
    render();
  },
  async saveAvatar() {
    const a = avatarDraft, avatar = { hat: a.hat, hatColor: a.hatColor.map((x) => x | 0), wearing: a.wearing };
    PARTS.forEach((p) => { avatar[p] = a[p].map((x) => x | 0); });
    const r = await call('avatar.set', { avatar });
    toast(r.ok ? 'Saved! You\'ll look like this in every game.' : r.error);
    if (r.ok) { avatarDraft = null; render(); }
  },
  async staff(d) {
    let r;
    if (d.op === 'grant') {
      // A badge is a signature: the official account signs it, or a staff member
      // signs it and adds their own Staff badge (only for Verified).
      const message = 'gb-badge:' + d.key + ':' + d.id;
      let sig = await gb.sign(message);
      if (!me.official) {
        const mine = (me.grants || []).find((g) => g[0] === 'staff');
        if (!mine) { toast('You need the Staff badge for that.'); return; }
        sig = 's:' + me.id + ':' + mine[1] + ':' + sig;
      }
      r = await call('admin.grant', { to: d.id, key: d.key, sig });
    } else if (d.op === 'revoke') {
      r = await call('admin.revoke', { to: d.id, key: d.key });
    } else if (d.op === 'bolts') {
      const amount = parseInt(prompt('How many Bolts to give ' + d.name + '? (A minus number takes them away.)', '100') || '0', 10);
      if (!amount) return;
      r = await call('admin.giveBolts', { to: d.id, amount, reason: '' });
    } else if (d.op === 'ban') {
      if (d.on && !confirm('Ban this account?')) return;
      r = await call('admin.ban', { to: d.id, on: !!d.on });
    }
    toast(r && r.ok ? 'Done.' : (r && r.error) || 'That didn\'t work.');
    render();
  },
  async friend(d) {
    const r = await call(d.op, { user: d.user });
    toast(r.ok ? ({ friends: 'You\'re friends now!', sent: 'Friend request sent.', none: 'Done.' }[r.status] || 'Done.') : r.error);
    render();
  },
  async group(d) {
    if (d.op === 'groups.delete' && !confirm('Delete this group for good?')) return;
    if (d.op === 'groups.leave' && !confirm('Leave this group?')) return;
    const args = { id: d.id };
    if (d.user) args.user = d.user;
    if (d.action) args.action = d.action;
    if (d.op === 'groups.request') args.accept = d.accept === '1';
    if (d.op === 'groups.deletePost') { args.by = d.by; args.time = Number(d.time); }
    const r = await call(d.op, args);
    if (!r.ok) toast(r.error);
    else if (r.requested) toast('Asked to join. An admin will let you in.');
    if (d.op === 'groups.delete' && r.ok) { location.hash = '#/groups'; return; }
    render();
  },
};

const forms = {
  gameSearch(f) { location.hash = '#/games?' + new URLSearchParams({ q: f.q.value, sort: f.sort.value }); },
  catalogSearch(f) { location.hash = '#/catalog?' + new URLSearchParams({ kind: f.kind.value, q: f.q.value }); },
  peopleSearch(f) { location.hash = '#/people?' + new URLSearchParams({ q: f.q.value }); },
  staffSearch(f) { location.hash = '#/staff?' + new URLSearchParams({ q: f.q.value }); },
  groupSearch(f) { location.hash = '#/groups?' + new URLSearchParams({ q: f.q.value }); },
  async redeem(f) {
    const r = await call('bolts.redeem', { code: f.code.value });
    toast(r.ok ? 'You got ' + r.got + ' Bolts!' : r.error);
    if (r.ok) render();
  },
  async login(f) {
    setTimeout(checkRequests, 1500);
    const msg = $('#loginMsg');
    msg.className = 'muted'; msg.textContent = 'Logging in...';
    const r = await gb.logIn(f.username.value.trim(), f.password.value);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    await hello();
    toast('Welcome back, ' + me.username + '!');
    location.hash = '#/';
  },
  async signup(f) {
    const msg = $('#loginMsg');
    if (f.password.value.length < 8) { msg.textContent = 'Your password needs at least 8 characters.'; return; }
    if (f.password.value !== f.password2.value) { msg.textContent = 'The two passwords don\'t match.'; return; }
    msg.className = 'muted'; msg.textContent = 'Signing up...';
    const r = await gb.signUp(f.username.value.trim(), f.password.value);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    await hello();
    toast('Welcome to Guts&Bolts, ' + me.username + '! You\'re user #' + me.userId + '.', 6000);
    location.hash = '#/';
  },
  async upload(f) {
    const kind = f.kind.value, msg = $('#uploadMsg');
    const args = { kind, name: f.name.value, description: f.description.value, price: f.price ? Number(f.price.value) || 0 : 0 };
    if (['hat', 'shirt', 'pants'].includes(kind)) {
      const c = f.color.value;
      args.meta = { color: [1, 3, 5].map((i) => parseInt(c.substr(i, 2), 16)) };
      if (kind === 'hat') args.meta.style = Number(f.style.value);
      args.data = '';
    } else {
      const file = f.file.files[0];
      if (!file) { msg.textContent = 'Pick a file first.'; return; }
      args.data = await gb.fileBase64(file);
      if (kind === 'audio') args.meta = { ext: (file.name.split('.').pop() || '').toLowerCase() };
    }
    msg.className = 'muted'; msg.textContent = ' Uploading...';
    f.querySelector('button').disabled = true;
    const r = await call('upload', args);
    f.querySelector('button').disabled = false;
    if (!r.ok) { msg.className = 'error'; msg.textContent = ' ' + r.error; return; }
    toast('Uploaded "' + r.asset.name + '"!' + (r.fee ? ' (' + r.fee + ' Bolts)' : ''));
    render();
  },
  async groupCreate(f) {
    const r = await call('groups.create', { name: f.name.value, description: f.description.value,
      color: parseInt(f.color.value.slice(1), 16), open: f.open.checked });
    if (!r.ok) { toast(r.error); return; }
    location.hash = '#/group/' + r.group.id;
  },
  async groupPost(f) {
    const r = await call('groups.post', { id: f.id.value, text: f.text.value });
    if (!r.ok) toast(r.error); else render();
  },
  async groupShout(f) {
    const r = await call('groups.shout', { id: f.id.value, text: f.text.value });
    if (!r.ok) toast(r.error); else render();
  },
  async groupEdit(f) {
    const r = await call('groups.edit', { id: f.id.value, description: f.description.value,
      color: parseInt(f.color.value.slice(1), 16), open: f.open.checked });
    toast(r.ok ? 'Saved.' : r.error);
    if (r.ok) render();
  },
};

document.addEventListener('click', async (e) => {
  const el = e.target.closest('[data-act]');
  if (!el || !actions[el.dataset.act]) return;
  e.preventDefault();
  if (el.disabled) return;
  el.disabled = true;
  try { await actions[el.dataset.act](el.dataset, el); } finally { el.disabled = false; }
});

document.addEventListener('submit', async (e) => {
  const f = e.target;
  if (!f.dataset.form || !forms[f.dataset.form]) return;
  e.preventDefault();
  const btn = f.querySelector('button:not([type=button])');
  if (btn) btn.disabled = true;
  try { await forms[f.dataset.form](f); } finally { if (btn) btn.disabled = false; }
});

// --- the page picker ------------------------------------------------------------------

async function render() {
  const token = ++pageToken;
  const path = (location.hash.replace(/^#\/?/, '').split('?')[0] || 'home').split('/');
  route = path;
  const name = pages[path[0]] ? path[0] : 'home';
  document.querySelectorAll('#nav a[data-page]').forEach((a) => {
    const p = a.dataset.page;
    a.classList.toggle('on', p === name || (p === 'games' && name === 'game') || (p === 'catalog' && name === 'item') ||
      (p === 'people' && name === 'user') || (p === 'groups' && name === 'group'));
  });
  if (name !== 'avatar') avatarDraft = null;   // leaving the avatar page drops unsaved changes
  if (!me) {
    show(html`<h1>Can't reach the Guts&amp;Bolts server</h1>
      <p class="muted">It might be switched off right now. Try again in a bit.</p>
      <p><button class="btn blue" data-act="retry">Try again</button></p>`);
    return;
  }
  try {
    await pages[name](...path.slice(1).map(decodeURIComponent));
  } catch (err) {
    if (err instanceof Stale || token !== pageToken) return;   // the visitor moved on: the new page draws itself
    show(html`<h1>Something went wrong</h1><p class="error">${String(err && err.message || err)}</p>`);
  }
}
actions.retry = async () => { await hello(); render(); };

window.addEventListener('hashchange', () => {
  document.querySelectorAll('.modal').forEach((m) => m.remove());
  window.scrollTo(0, 0);
  render();
});
(async () => {
  await hello();
  render();
  checkRequests();
  setInterval(() => { if (document.visibilityState === 'visible') checkRequests(); }, 60000);
})();
