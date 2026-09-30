// The Guts&Bolts web app: the Player app's site pages (games, catalog,
// create, friends, people, groups, Bolts, sign up / log in) in a browser.
// Pages are picked by the address after '#', like #/games or #/user/12.
import * as gb from './gb.js';
import { mountAvatar, avatarPicture } from './avatar3d.js';

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
const KINDS = { hat: 'Hat', shirt: 'Shirt', pants: 'Pants', audio: 'Audio', plugin: 'Plugin', game: 'Game', decal: 'Decal', model: 'Model',
  hair: 'Hair', faceacc: 'Face Accessory', neck: 'Neck Accessory', shoulder: 'Shoulder Accessory', waist: 'Waist Accessory', face: 'Face',
  tshirt: 'T-Shirt' };
// Things you wear on the body, made in Studio's Accessory window (old-style hats are just a shape).
const ACCESSORIES = ['hat', 'hair', 'faceacc', 'neck', 'shoulder', 'waist'];
const WEARABLE = ['shirt', 'pants', 'tshirt', 'face', ...ACCESSORIES];
// Only Guts' own accessories and faces can be Limited.
const canBeLimited = (a) => ACCESSORIES.includes(a.kind) || a.kind === 'face';
// Items with a real picture (Studio accessories, faces) show that instead of a drawing.
const hasPicture = (a) => !!a.thumb && (a.kind === 'face' || a.kind === 'tshirt' || (a.meta && a.meta.model));
const FEES = { decal: 5, hat: 10, shirt: 10, pants: 10, audio: 20, plugin: 20, game: 0, hair: 10, faceacc: 10, neck: 10, shoulder: 10, waist: 10, face: 0, tshirt: 10 };

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
  if (hasPicture(a)) return html`<img class="item-thumb" data-thumb="${a.id}" alt="${a.name}">`;
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
  } else if (k === 'tshirt') {   // (no picture yet)
    shape = `<path d="M34 20 L18 30 L24 46 L32 42 L32 82 L68 82 L68 42 L76 46 L82 30 L66 20 Q50 30 34 20 Z" fill="#fff" stroke="#999" stroke-width="2"/>
      <rect x="38" y="40" width="24" height="24" rx="3" fill="#ddd"/>`;
  } else if (k === 'face') {
    shape = `<rect x="18" y="18" width="64" height="64" rx="12" fill="#f5d33b" stroke="rgba(0,0,0,.25)"/><ellipse cx="40" cy="42" rx="4" ry="7" fill="#111"/>
      <ellipse cx="60" cy="42" rx="4" ry="7" fill="#111"/><path d="M34 58 Q50 74 66 58" stroke="#111" stroke-width="4" fill="none" stroke-linecap="round"/>`;
  } else if (ACCESSORIES.includes(k)) {
    shape = `<circle cx="50" cy="50" r="30" fill="${c}"/><circle cx="50" cy="50" r="14" fill="rgba(255,255,255,.35)"/>`;
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

// A game's icon (a small square picture), or its first letter on its colours.
function gameIcon(g, size = 48) {
  if (g.icon) return html`<img class="game-icon" src="/icon/${encodeURIComponent(g.id)}?v=${g.icon}" alt="" width="${size}" height="${size}">`;
  return html`<span class="game-icon" style="width:${size}px;height:${size}px;background:${raw(gameColors(g.id))}">${(g.name || '?').slice(0, 1)}</span>`;
}
const ACCESS_NAMES = { public: 'Public', friends: 'Friends only', private: 'Private' };

// Shrink a picture the visitor picked to w x h (cropping to fit), as JPEG base64.
function pictureBase64(file, w, h) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      const c = document.createElement('canvas');
      c.width = w; c.height = h;
      const k = Math.max(w / img.width, h / img.height);
      const dw = img.width * k, dh = img.height * k;
      const g = c.getContext('2d');
      g.fillStyle = '#fff'; g.fillRect(0, 0, w, h);
      g.drawImage(img, (w - dw) / 2, (h - dh) / 2, dw, dh);
      URL.revokeObjectURL(img.src);
      resolve(c.toDataURL('image/jpeg', 0.88).split(',')[1]);
    };
    img.onerror = () => reject(new Error('That file isn\'t a picture.'));
    img.src = URL.createObjectURL(file);
  });
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
const HATS = ['None', 'Top Hat', 'Cap', 'Crown', 'Ponytail'];
const HAT_COLORS = { 1: [30, 30, 36], 2: [204, 46, 41], 3: [240, 190, 40], 4: [219, 51, 158] };   // a hat's normal colours

function defaultAvatar() {
  const a = { hat: 0, hatColor: [-1, -1, -1], wearing: [] };
  PARTS.forEach((p, i) => { a[p] = PRESETS[0][1][i].slice(); });
  return a;
}
const rgbCss = (c) => `rgb(${c.map((x) => Math.max(0, Math.min(255, Number(x) | 0))).join(',')})`;
const hexOf = (c) => '#' + c.map((x) => (Math.max(0, Math.min(255, x | 0))).toString(16).padStart(2, '0')).join('');
const fromHex = (h) => [1, 3, 5].map((i) => parseInt(h.substr(i, 2), 16));

// A blocky character, front view. Clothes from the catalog colour the parts they cover.
// `head`: just the head and hat, square (the round faces on server cards).
function avatarSvg(av, size = 160, items = [], head = false) {
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
    4: `<path d="M35 20 Q35 9 50 9 Q65 9 65 20 Q58 14 50 15 Q42 14 35 20 Z" fill="${hatCol}"/><circle cx="50" cy="6" r="6" fill="${hatCol}"/>`,
  };
  const box = head ? '28 -6 44 44' : '0 -8 100 128', tall = head ? size : size * 1.28;
  const svg = `<svg viewBox="${box}" width="${size}" height="${tall}" role="img" aria-label="Avatar">
    <rect x="37" y="12" width="26" height="22" rx="4" fill="${col.head}" stroke="rgba(0,0,0,.25)"/>
    <ellipse cx="47.5" cy="19.5" rx="1.3" ry="2.4" fill="#111"/><ellipse cx="52.5" cy="19.5" rx="1.3" ry="2.4" fill="#111"/>
    <path d="M44.5 24.5 Q46 30.5 50 30.5 Q54 30.5 55.5 24.5" stroke="#111" stroke-width="1.8" fill="none" stroke-linecap="round"/>
    <rect x="30" y="35" width="40" height="38" fill="${col.torso}" stroke="rgba(0,0,0,.25)"/>
    <rect x="12" y="35" width="17" height="38" fill="${col.rightArm}" stroke="rgba(0,0,0,.25)"/>
    <rect x="71" y="35" width="17" height="38" fill="${col.leftArm}" stroke="rgba(0,0,0,.25)"/>
    <rect x="30" y="74" width="19.5" height="40" fill="${col.rightLeg}" stroke="rgba(0,0,0,.25)"/>
    <rect x="50.5" y="74" width="19.5" height="40" fill="${col.leftLeg}" stroke="rgba(0,0,0,.25)"/>
    ${hats[hat] || ''}</svg>`;
  return raw(svg);
}

// How many people are in each game's servers right now (game id -> players), for the cards.
let playingNow = {};
async function loadPlaying() {
  const r = await gb.call('servers.list', {});
  playingNow = {};
  if (r.ok) for (const s of r.servers) playingNow[s.game] = (playingNow[s.game] || 0) + (s.players || 0);
}

// Genres a game can pick (the server has the same list).
const GENRES = ['Adventure', 'Obby', 'Fighting', 'Horror', 'Roleplay', 'Simulator', 'Tycoon', 'Racing', 'Sports',
  'Shooter', 'Puzzle', 'Survival', 'Comedy', 'Building', 'Sandbox', 'Showcase', 'Town and City', 'Destruction'];
// "87%" liked, or '' before anyone has voted.
const likedPercent = (g) => (g.likes || 0) + (g.dislikes || 0) ? Math.round(100 * g.likes / (g.likes + g.dislikes)) + '%' : '';

function gameCard(g) {
  const n = playingNow[g.id] || g.playing || 0, liked = likedPercent(g);
  return html`<a class="card" href="#/game/${g.id}">
    ${gamePic(g)}
    <div class="name">${g.name}</div>
    <div class="by">by ${g.creatorName}${verified(g.creatorVerified)}</div>
    <div class="by">${liked ? html`<span class="liked" title="${g.likes} likes, ${g.dislikes} dislikes">&#128077; ${liked}</span> · ` : ''}${n ? html`<b class="playing">${n} playing</b>` : html`${g.plays || 0} visits`}</div>
    ${(g.genres || []).length ? html`<div class="by small">${g.genres.join(' · ')}</div>` : ''}</a>`;
}

// Catalog items in 3D: each one worn by a plain grey mannequin (the drawing is
// swapped in once WebGL has made the picture; itemIcon's flat drawing until then).
const MANNEQUIN = { head: [205, 207, 212], torso: [205, 207, 212], leftArm: [205, 207, 212], rightArm: [205, 207, 212],
  leftLeg: [190, 192, 198], rightLeg: [190, 192, 198], hat: 0, hatColor: [-1, -1, -1], wearing: [] };
const items3d = new Map();   // id -> item, for the pictures below
// Shirts, pants and shape hats: the 3D mannequin can wear them.
const drawable3d = (a) => ['shirt', 'pants'].includes(a.kind) || (a.kind === 'hat' && !(a.meta && a.meta.model));

// Fills in <img data-thumb="id"> with the item's picture.
function loadThumbs(root = view) {
  root.querySelectorAll('img[data-thumb]').forEach(async (img) => {
    if (img.src) return;
    const t = await call('thumb.get', { id: img.dataset.thumb });
    if (t.ok && t.data) img.src = 'data:image/png;base64,' + t.data;
  });
}

function upgradeItemPictures() {
  loadThumbs();
  view.querySelectorAll('[data-item3d]').forEach(async (el) => {
    const it = items3d.get(el.dataset.item3d);
    if (!it || !drawable3d(it)) return;
    const url = await avatarPicture(MANNEQUIN, [it], 150);
    if (url && el.isConnected) el.innerHTML = html`<img class="item3d" src="${url}" alt="${it.name}">`.s;
  });
}

// The gold badge on things made by Guts&Bolts staff: official, safe to use (like Roblox's).
function officialBadge(a) {
  if (!a.creatorStaff) return '';
  return raw(`<span class="official-badge" title="Official: made by Guts&amp;Bolts staff, safe to use"><svg viewBox="0 0 24 26" width="24" height="26" aria-label="Official">
    <path d="M1 1 H23 V14 L12 25 L1 14 Z" fill="#f5ac26" stroke="#fff" stroke-width="2"/>
    <circle cx="8.5" cy="8" r="3" fill="#fff"/><path d="M16 4 L20 11 L12 11 Z" fill="#fff"/><rect x="10" y="12.5" width="5.5" height="5.5" fill="#fff"/></svg></span>`);
}

function itemCard(a) {
  items3d.set(a.id, a);
  return html`<a class="card square" href="#/item/${a.id}">
    <div class="pic-wrap"><div class="pic" data-item3d="${a.id}">${itemIcon(a)}</div>${officialBadge(a)}</div>${a.limited ? html`<span class="limited-tag">LIMITED</span>` : ''}
    <div class="name">${a.name}</div>
    <div class="by">${a.limited && a.limited.left <= 0 ? (a.limited.lowest ? html`from ${bolts(a.limited.lowest)}` : raw('<span class="muted">Sold out</span>'))
      : a.price > 0 ? bolts(a.price) : raw('<span class="muted">Free</span>')} · by ${a.creatorName}${verified(a.creatorVerified)}</div></a>`;
}

// A game badge: a coloured medal with a star (games' own badges, made by their creators).
function gameBadgeIcon(b, size = 56) {
  const c = rgbCss(Array.isArray(b.color) ? b.color : [240, 180, 40]);
  return raw(`<svg viewBox="0 0 60 60" width="${size}" height="${size}" aria-hidden="true">
    <circle cx="30" cy="30" r="27" fill="${c}" stroke="rgba(0,0,0,.3)" stroke-width="2"/>
    <circle cx="30" cy="30" r="20" fill="rgba(255,255,255,.18)"/>
    <path d="M30 15 L34.4 25.2 L45.5 26.2 L37.1 33.5 L39.6 44.4 L30 38.7 L20.4 44.4 L22.9 33.5 L14.5 26.2 L25.6 25.2 Z" fill="#fff" opacity=".9"/></svg>`);
}

// Why staff can ban someone (the server has the same list).
const BAN_REASONS = [
  ['sexual', 'Sexual content'], ['extremism', 'Violent extremism'], ['harassment', 'Harassment or bullying'],
  ['hate', 'Hate speech or discrimination'], ['threats', 'Threats of violence'], ['selfharm', 'Promoting self-harm'],
  ['scam', 'Scamming or phishing'], ['personal', 'Sharing personal information'], ['exploit', 'Cheating or exploiting'],
  ['spam', 'Spam'], ['impersonation', 'Impersonation'], ['inappropriate', 'Inappropriate content'],
  ['underage', 'Underage safety violation'], ['other', 'Breaking the rules'],
];

// A classic white popup over a dark page. `body` is html``; returns the box.
function popup(body, cls = '') {
  document.querySelectorAll('.modal').forEach((m) => m.remove());
  const box = document.createElement('div');
  box.className = 'modal';
  box.innerHTML = html`<div class="popup ${cls}"><button class="popup-x" data-act="closeModal" aria-label="Close">&times;</button>${body}</div>`.s;
  box.addEventListener('click', (e) => { if (e.target === box) box.remove(); });
  document.body.appendChild(box);
  return box;
}

// The two guest looks (the app has the same): black clothes, and a black cap or a ponytail.
const GUEST_LOOKS = {
  boy: { head: [240, 240, 235], torso: [27, 27, 30], leftArm: [27, 27, 30], rightArm: [27, 27, 30], leftLeg: [27, 27, 30],
    rightLeg: [27, 27, 30], hat: 2, hatColor: [25, 25, 28], wearing: [] },
  girl: { head: [240, 240, 235], torso: [27, 27, 30], leftArm: [27, 27, 30], rightArm: [27, 27, 30], leftLeg: [27, 27, 30],
    rightLeg: [27, 27, 30], hat: 4, hatColor: [-1, -1, -1], wearing: [] },
};
function guestPicker(id, name) {
  const pick = (guest, label) => html`<a href="#" class="charpick-one" data-act="pickGuest" data-guest="${guest}" data-id="${id}" data-name="${name}">
      <span class="charpick-pic" data-guest-pic="${guest}">${avatarSvg(GUEST_LOOKS[guest], 120)}</span><b>${label}</b></a>`;
  popup(html`<h1 class="popup-title">Choose Your Character:</h1>
    <div class="charpick">${pick('boy', 'Play As Boy')}${pick('girl', 'Play As Girl')}</div>
    <p><a class="charpick-account" href="#/login">Have an Account?</a></p>`, 'wide');
  for (const g of ['boy', 'girl'])
    avatarPicture(GUEST_LOOKS[g], [], 150).then((url) => {
      const el = document.querySelector(`[data-guest-pic="${g}"]`);
      if (url && el) el.innerHTML = html`<img src="${url}" alt="" width="150" height="188">`.s;
    }).catch(() => {});
}
// Open the app on this game (if it's installed), with a way to get it if not.
function launchGame(id, name, guest, server = '') {
  const q = [guest ? 'guest=' + guest : '', server ? 'server=' + encodeURIComponent(server) : ''].filter(Boolean).join('&');
  const url = 'gutsandbolts://play/' + encodeURIComponent(id) + (q ? '?' + q : '');
  popup(html`<h1 class="popup-title">Starting Guts&amp;Bolts...</h1>
    <div class="launch-spin" aria-hidden="true"></div>
    <p>Opening <b>${name}</b> in the Guts&amp;Bolts app${guest ? ' as a guest' : ''}.</p>
    <p class="small muted">If your browser asks, choose <b>Open Guts&amp;Bolts</b>.</p>
    <p class="popup-buttons"><a class="btn green" href="${url}">Try again</a> <a class="btn" href="../#download">Download the app</a></p>
    <p class="small muted">Don't have it yet? Download it (Windows, Mac, Linux or Android), open it once, then press Play again.</p>`);
  location.href = url;
}

// Visitors can look at everything; doing things needs an account.
function loginPopup(what) {
  popup(html`<h1 class="popup-title">You need to log in</h1>
    <p>Log in or sign up (it's free!) to ${what}.</p>
    <p class="popup-buttons"><a class="btn green big" href="#/signup">Sign Up</a> <a class="btn blue big" href="#/login">Log In</a></p>
    <p><a href="#" data-act="closeModal">Not now</a></p>`);
}
// Which clicks and forms need an account, and what to say.
const NEEDS_ACCOUNT = {
  buy: 'get items from the catalog', daily: 'claim your daily Bolts', saveAvatar: 'save your avatar',
  friend: 'add friends', group: 'join groups', redeem: 'redeem codes', upload: 'upload things and publish games',
  groupCreate: 'make a group', groupPost: 'post on group walls', groupShout: 'shout to a group',
};

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
    box.innerHTML = html`Hi, <a href="#/user/${me.userId}">${me.username}</a>${verified(me.verified)}
      | <a href="#/bolts">${bolts(me.bolts)}</a> | <a href="#/settings">Settings</a> | <a href="#" data-act="logout">Logout</a>`.s;
  } else {
    box.innerHTML = html`<a href="#/signup">Sign Up</a> | <a href="#/login">Login</a>`.s;
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
  const [games, items, fr] = await Promise.all([
    pageCall('list', { kind: 'game', sort: 'popular', limit: 8 }),
    pageCall('list', { kind: 'clothing', limit: 6 }),
    signedIn() ? pageCall('friends.list', {}) : Promise.resolve({ ok: false }),
    loadPlaying(),
  ]);
  const gameBox = (title, r, more) => html`<div class="box"><h2 class="boxhead">${title}
      <a href="${more}" style="float:right">See more &raquo;</a></h2>
    ${r.ok && r.assets.length ? html`<div class="grid">${r.assets.map(gameCard)}</div>`
      : html`<p class="muted small">${r.ok ? 'No games published yet. Publish one from Studio!' : r.error}</p>`}</div>`;
  const shopBox = html`<div class="box"><h2 class="boxhead">New in the Catalog
      <a href="#/catalog" style="float:right">See more &raquo;</a></h2>
    ${items.ok && items.assets.length ? html`<div class="grid">${items.assets.map(itemCard)}</div>`
      : html`<p class="muted small">Nothing here yet.</p>`}</div>`;
  const upd = await pageCall('updates.list', { limit: 1 });
  const latest = upd.ok && upd.updates.length ? upd.updates[0] : null;
  const updateBox = latest ? html`<div class="box latest-update"><h2 class="boxhead">Latest update
      <a href="#/updates" style="float:right">All updates &raquo;</a></h2>
    <p><b>${latest.name}</b>${latest.version ? html` <span class="small muted">v${latest.version}</span>` : ''}
      <span class="small muted">&middot; ${ago(latest.time)}</span></p>
    ${latest.summary ? html`<p class="small">${latest.summary}</p>` : ''}</div>` : '';
  if (!signedIn()) {
    show(html`<div class="welcome">
        <div><h1>Welcome to Guts&amp;Bolts!</h1>
          <p>Build games, play them with your friends and fall apart spectacularly.
            Sign up to get <b>100 Bolts</b>, dress up your character, make friends and upload your own stuff.</p>
          <p><a class="btn green big" href="#/signup">Sign Up and Play</a>
            <a class="btn" href="#/login">Login</a></p></div>
        <div class="welcome-guy" id="homeAvatar">${avatarSvg(defaultAvatar(), 150)}</div></div>
      ${updateBox}${gameBox('Best of Guts&Bolts', games, '#/games')}${shopBox}`);
    mountAvatar($('#homeAvatar'), defaultAvatar(), [], { width: 170 }).catch(() => {});
    return;
  }
  const friends = fr.ok ? fr.friends || [] : [];
  const onlineFriends = friends.filter((p) => p.online);
  show(html`<div class="home">
      <div class="home-left">
        <div class="box avatar-box"><h2 class="boxhead">Hi, ${me.username}!</h2>
          <div id="homeAvatar">${avatarSvg(me.avatar || defaultAvatar(), 150)}</div>
          <p class="small"><a href="#/avatar">Change your avatar</a> | <a href="#/user/${me.userId}">Profile</a></p></div>
        <div class="box"><h2 class="boxhead">Your Bolts</h2>
          <div style="font-size:18px">${bolts(me.bolts)}</div>
          ${me.canDaily ? html`<p class="small">Your daily Bolts are ready!</p>
              <button class="btn green" data-act="daily">Claim 25 Bolts</button>`
            : html`<p class="small muted">Come back tomorrow for 25 more.</p>`}</div>
        <div class="box"><h2 class="boxhead">Friends online (${onlineFriends.length})</h2>
          ${onlineFriends.length ? html`<div class="list">${onlineFriends.slice(0, 8).map((p) => html`<div>
              <span class="dot on"></span><a class="grow" href="#/user/${p.userId}">${p.username || p.name}</a></div>`)}</div>`
            : html`<p class="small muted">${friends.length ? 'None of your friends are on right now.' : html`No friends yet. <a href="#/people">Find some!</a>`}</p>`}</div>
      </div>
      <div class="home-right">${updateBox}${gameBox('Best of Guts&Bolts', games, '#/games')}${shopBox}</div>
    </div>`);
  mountAvatar($('#homeAvatar'), me.avatar || defaultAvatar(), [], { width: 170 }).catch(() => {});
};

// --- Updates: the update log, newest first. It checks for new ones by itself while
// you're looking, and the Updates link lights up when there's one you haven't seen.
const UPDATE_TAG_COLORS = { Engine: '#2a7de1', Studio: '#8a4fd1', Website: '#1f9d55', Player: '#e08a00', Server: '#4a5a70', Fix: '#d33c3c' };
function seenUpdate() { try { return localStorage.getItem('gb.seenUpdate') || ''; } catch { return ''; } }
function markUpdateSeen(id) { try { localStorage.setItem('gb.seenUpdate', id); } catch { /* private window */ } }
function updateCard(u, fresh) {
  const color = UPDATE_TAG_COLORS[u.tag] || '#4a5a70';
  return html`<article class="box update${fresh ? ' fresh' : ''}" id="upd-${u.id}">
    <div class="update-head">
      <h2>${u.name}${fresh ? html` <span class="badge">NEW</span>` : ''}</h2>
      <span class="update-tag" style="background:${color}">${u.tag}</span>
    </div>
    <p class="small muted">${u.version ? html`Version ${u.version} &middot; ` : ''}${new Date(u.time * 1000).toLocaleDateString()} (${ago(u.time)})${u.by ? html` &middot; posted by ${u.by}` : ''}</p>
    ${u.summary ? html`<p class="update-summary">${u.summary}</p>` : ''}
    ${(u.items || []).length ? html`<ul class="update-items">${u.items.map((i) => html`<li>${i}</li>`)}</ul>` : ''}
    ${me && me.staff && u.id.startsWith('u-') ? html`<p><button class="btn small" data-act="deleteUpdate" data-id="${u.id}">Delete</button></p>` : ''}
  </article>`;
}
function setUpdatesLink(latest) {
  const link = $('#nav a[data-page=updates]');
  if (link) link.innerHTML = 'Updates' + (latest && latest !== seenUpdate() ? ' <span class="badge">NEW</span>' : '');
}
async function checkUpdates() {
  const r = await gb.call('updates.list', { limit: 1 });
  if (r.ok) setUpdatesLink(r.latest);
  return r;
}

pages.updates = async () => {
  const r = await pageCall('updates.list', { limit: 100 });
  if (!r.ok) { show(html`<h1>Updates</h1><p class="error">${r.error}</p>`); return; }
  const before = seenUpdate();
  let known = new Set(r.updates.map((u) => u.id));
  const staffForm = me && me.staff ? html`<details class="box"><summary><b>Post an update</b> (staff)</summary>
      <form data-form="postUpdate" class="form">
        <label>Name <input type="text" name="name" maxlength="40" required placeholder="Give it a cool name, like Glass Lagoon"></label>
        <label>Version <input type="text" name="version" maxlength="12" placeholder="0.6.1"></label>
        <label>Kind <select name="tag">${['Engine', 'Studio', 'Website', 'Player', 'Server', 'Fix'].map((t) => html`<option>${t}</option>`)}</select></label>
        <label>Summary <input type="text" name="summary" maxlength="200" placeholder="One line about it"></label>
        <label>What changed (one per line) <textarea name="items" rows="5"></textarea></label>
        <p><button class="btn green">Post update</button></p>
      </form></details>` : '';
  // The newest one you hadn't seen yet (and anything newer) is marked NEW.
  const firstSeen = r.updates.findIndex((u) => u.id === before);
  const isFresh = (i) => before && (firstSeen < 0 ? false : i < firstSeen);
  show(html`<h1>Updates</h1>
    <p class="muted">What's new in Guts&amp;Bolts, newest first. This page checks for new updates by itself.
      <span class="live-dot" title="Live"></span></p>
    ${staffForm}
    <div id="updateList">${r.updates.map((u, i) => updateCard(u, isFresh(i)))}</div>`);
  if (r.latest) { markUpdateSeen(r.latest); setUpdatesLink(r.latest); }
  // Live: look again every 20 seconds while this page is open; new ones slide in on top.
  const token = pageToken;
  const poll = setInterval(async () => {
    if (token !== pageToken) { clearInterval(poll); return; }
    if (document.visibilityState !== 'visible') return;
    const n = await gb.call('updates.list', { limit: 20 });
    if (token !== pageToken || !n.ok) return;
    const list = $('#updateList');
    if (!list) return;
    const added = n.updates.filter((u) => !known.has(u.id));
    for (const u of added.reverse()) {
      list.insertAdjacentHTML('afterbegin', updateCard(u, true).s);
      toast('New update: ' + u.name + '!');
    }
    // (and ones staff deleted disappear)
    const now = new Set(n.updates.map((u) => u.id));
    const oldest = n.updates.length ? n.updates[n.updates.length - 1].time : 0;
    for (const id of known) if (!now.has(id) && id.startsWith('u-')) {
      const u = r.updates.find((x) => x.id === id);
      if (!u || u.time >= oldest) { const el = document.getElementById('upd-' + id); if (el) el.remove(); }
    }
    known = new Set([...known, ...now]);
    if (n.latest) { markUpdateSeen(n.latest); setUpdatesLink(n.latest); }
  }, 20000);
};

pages.games = async () => {
  const q = new URLSearchParams(location.hash.split('?')[1] || '');
  const sort = q.get('sort') || 'popular', query = q.get('q') || '', genre = q.get('genre') || '';
  show(html`<h1>Games</h1><p class="muted">Loading...</p>`);
  const [r] = await Promise.all([pageCall('list', { kind: 'game', sort, query, genre, limit: 100 }), loadPlaying()]);
  const sorts = [['popular', 'Most played'], ['playing', 'Playing now'], ['rated', 'Top rated'], ['new', 'Newest'], ['updated', 'Recently updated']];
  const link = (over) => '#/games?' + new URLSearchParams(Object.assign({ q: query, sort, genre }, over));
  show(html`<h1>Games${genre ? html` <span class="muted">· ${genre}</span>` : ''}</h1>
    <form class="row" data-form="gameSearch"><input type="hidden" name="genre" value="${genre}">
      <input type="search" name="q" placeholder="Search games, genres, descriptions" value="${query}" style="max-width:280px">
      <select name="sort" style="width:auto">${sorts.map(([k, l]) => html`<option value="${k}" ${sort === k ? 'selected' : ''}>${l}</option>`)}</select>
      <button class="btn blue">Search</button></form>
    <div class="genre-chips"><a class="chip ${genre ? '' : 'on'}" href="${link({ genre: '' })}">All</a>
      ${GENRES.map((gn) => html`<a class="chip ${genre === gn ? 'on' : ''}" href="${link({ genre: gn })}">${gn}</a>`)}</div>
    ${r.ok ? (r.assets.length ? html`<div class="grid">${r.assets.map(gameCard)}</div>`
        : html`<p class="muted">No games found${genre ? ' in ' + genre : ''}. ${genre || query ? html`<a href="#/games">See all games</a>` : ''}</p>`)
      : html`<p class="error">${r.error}</p>`}`);
};

// Create > Library: everything people have made public, to use in your games.
async function libraryPage(head) {
  const q = new URLSearchParams(location.hash.split('?')[1] || '');
  const kind = q.get('kind') || 'model', query = q.get('q') || '';
  const r = await pageCall('list', { kind, query, sort: 'popular', limit: 100 });
  const chip = (k, l) => html`<a class="chip ${kind === k ? 'on' : ''}" href="#/create/library?kind=${k}">${l}</a>`;
  const card = (a) => html`<div class="card square lib-card">
      <div class="pic">${a.kind === 'decal' ? html`<img class="thumb" data-decal="${a.id}" alt="" style="width:100%;height:100%;object-fit:contain">`
        : a.kind === 'model' && a.thumb ? html`<img class="lib-thumb" data-thumb="${a.id}" alt="">`
        : html`<span class="lib-kind">${KINDS[a.kind] || a.kind}</span>`}${officialBadge(a)}</div>
      <div class="name">${a.name}</div>
      <div class="by">by <a href="#/user/${a.creator}">${a.creatorName}</a>${verified(a.creatorVerified)}</div>
      <div class="by small"><button class="btn small" data-act="copyId" data-id="${a.id}">Copy ID</button></div></div>`;
  show(html`${head}
    <p class="muted">Everything people have made public. Use any of it in your games: in Studio, open the <b>Toolbox</b> (the
      <b>Library</b> tab), or copy an ID into a Decal's Texture / a Sound's File.</p>
    <form class="row" data-form="librarySearch"><input type="hidden" name="kind" value="${kind}">
      <input type="search" name="q" placeholder="Search the Library" value="${query}" style="max-width:280px"><button class="btn blue">Search</button></form>
    <div class="genre-chips">${chip('model', 'Models')}${chip('decal', 'Decals')}${chip('audio', 'Audio')}${chip('plugin', 'Plugins')}</div>
    ${r.ok ? (r.assets.length ? html`<div class="grid">${r.assets.map(card)}</div>` : html`<p class="muted">Nothing here yet.</p>`) : html`<p class="error">${r.error}</p>`}`);
  view.querySelectorAll('img[data-decal]').forEach(decalPicture);
  loadThumbs();
}

// Create > Accessories: hats, hair and the rest, made and placed in Studio.
async function myAccessoriesPage(head) {
  if (!signedIn()) { show(html`${head}${needSignIn('see your accessories')}`); return; }
  const r = await pageCall('list', { creator: me.id, limit: 100 });
  const list = (r.assets || []).filter((a) => ACCESSORIES.includes(a.kind));
  show(html`${head}
    <div class="box">${me.verified || me.staff ? html`Make accessories in <b>Studio</b>: build it, open <b>Avatar &gt; Accessories</b>, pick the type
      (hat, hair, face, neck, shoulder or waist), move it into place on the mannequin, <b>Save position</b>, then <b>Upload</b>.`
      : html`<b>Only Verified creators can make accessories.</b> You can still make <a href="#/create/shirt">shirts</a> and <a href="#/create/pants">pants</a>!`}</div>
    <h2>My accessories</h2>
    ${list.length ? html`<div class="grid">${list.map(itemCard)}</div>` : html`<p class="muted">Nothing yet.</p>`}`);
  upgradeItemPictures();
}

// Create > Models: the models you published from Studio, public or private.
async function myModelsPage(head) {
  if (!signedIn()) { show(html`${head}${needSignIn('see your models')}`); return; }
  const r = await pageCall('list', { creator: me.id, kind: 'model', limit: 100 });
  const left = me.publicModelsLeft;
  show(html`${head}
    <div class="box">Publish models from <b>Studio</b>: select objects, then <b>File &gt; Publish Selection to Library</b>.
      Public models show in everyone's Library; private ones only for you.
      ${left >= 0 ? html`<br><span class="muted">You can make ${left} more model${left === 1 ? '' : 's'} public this week (Verified creators have no limit).</span>` : ''}</div>
    <h2>My Models</h2>
    ${r.ok && r.assets.length ? html`<div class="list">${r.assets.map((a) => html`<div>
        <span class="grow"><b>${a.name}</b> <span class="small muted">${a.id}</span></span>
        <span class="badge-pill">${a.access === 'private' ? 'Private' : 'Public'}</span>
        <button class="btn small" data-act="modelAccess" data-id="${a.id}" data-access="${a.access === 'private' ? 'public' : 'private'}">Make ${a.access === 'private' ? 'public' : 'private'}</button>
        <button class="btn small red" data-act="deleteAsset" data-id="${a.id}" data-name="${a.name}">Delete</button></div>`)}</div>`
      : html`<p class="muted">No models yet.</p>`}`);
}

let serverPage = 1;   // which page of a game's server cards
pages.game = async (id) => {
  const [r, s] = await Promise.all([pageCall('list', { kind: 'game', limit: 100 }), pageCall('servers.list', { game: id })]);
  const g = r.ok && r.assets.find((a) => a.id === id);
  if (!g) { show(html`<h1>Game not found</h1><p class="muted">${r.ok ? 'It may have been deleted, or its creator made it private.' : r.error}</p>`); return; }
  const mine = signedIn() && (g.creator === me.id || me.staff);
  const servers = s.ok ? s.servers : [];
  const shared = new URLSearchParams(location.hash.split('?')[1] || '').get('server') || '';
  // Roblox-style server cards: faces of who's in it, how full it is, Join and Share. 8 a page.
  const perPage = 8, pages = Math.max(1, Math.ceil(servers.length / perPage));
  serverPage = Math.min(Math.max(1, serverPage), pages);
  const shown = servers.slice((serverPage - 1) * perPage, serverPage * perPage);
  const card = (sv) => {
    const people = sv.people || [], more = (sv.players || 1) - people.length;
    return html`<div class="server-card ${sv.id === shared ? 'shared' : ''}">
      <div class="server-faces">${people.map((p) => html`<a class="face" href="#/user/${p.id}" title="${p.name}">${avatarSvg(p.avatar, 52, [], true)}</a>`)}
        ${more > 0 ? html`<span class="face more">+${more}</span>` : ''}</div>
      <p class="server-count">${sv.players || 1} of ${sv.max || '?'} people max${sv.private ? html` <span class="badge-pill">Private</span>` : ''}</p>
      ${sv.friends ? html`<p class="small muted">${sv.friends} friend${sv.friends === 1 ? '' : 's'} here</p>` : ''}
      <div class="server-buttons"><button class="btn green small" data-act="joinServer" data-id="${g.id}" data-name="${g.name}" data-server="${sv.id}">Join</button>
        <button class="btn small" data-act="shareServer" data-id="${g.id}" data-server="${sv.id}">Share</button></div>
      <p class="small muted">ID: ${sv.id.replace(/^s-/, '')}</p></div>`;
  };
  const pager = pages > 1 ? html`<div class="pager">
      <button class="btn small" data-act="serverPage" data-to="1" ${serverPage === 1 ? 'disabled' : ''}>&laquo;</button>
      <button class="btn small" data-act="serverPage" data-to="${serverPage - 1}" ${serverPage === 1 ? 'disabled' : ''}>&lsaquo;</button>
      <span>Page ${serverPage} of ${pages}</span>
      <button class="btn small" data-act="serverPage" data-to="${serverPage + 1}" ${serverPage === pages ? 'disabled' : ''}>&rsaquo;</button>
      <button class="btn small" data-act="serverPage" data-to="${pages}" ${serverPage === pages ? 'disabled' : ''}>&raquo;</button></div>` : '';
  show(html`<p><a href="#/games">&lt; Games</a></p>
    <div class="hero">${gamePic(g)}
      <div><h1 class="game-title">${gameIcon(g, 40)} ${g.name}</h1>
        ${g.access && g.access !== 'public' ? html`<p><span class="badge-pill">${ACCESS_NAMES[g.access]}</span></p>` : ''}
        <p>by <a href="#/user/${g.creator}">${g.creatorName}</a>${verified(g.creatorVerified)}</p>
        ${(g.genres || []).length ? html`<p>${g.genres.map((gn) => html`<a class="chip" href="#/games?genre=${encodeURIComponent(gn)}">${gn}</a> `)}</p>` : ''}
        <div class="votes">
          <button class="btn small ${g.myVote === 1 ? 'green' : ''}" data-act="vote" data-id="${g.id}" data-vote="${g.myVote === 1 ? 0 : 1}" title="I like it">&#128077; ${g.likes || 0}</button>
          <div class="vote-bar"><div style="width:${(g.likes || 0) + (g.dislikes || 0) ? Math.round(100 * g.likes / (g.likes + g.dislikes)) : 50}%"></div></div>
          <button class="btn small ${g.myVote === -1 ? 'red' : ''}" data-act="vote" data-id="${g.id}" data-vote="${g.myVote === -1 ? 0 : -1}" title="Not for me">&#128078; ${g.dislikes || 0}</button></div>
        <table class="stats game-stats">
          <tr><td>Playing</td><td>${g.playing || 0}</td><td>Visits</td><td>${g.plays || 0}</td></tr>
          <tr><td>Created</td><td>${new Date(g.created * 1000).toLocaleDateString()}</td><td>Updated</td><td>${ago(g.updated || g.created)}</td></tr>
          <tr><td>Server size</td><td>${g.maxPlayers || 12}</td><td>Genre</td><td>${(g.genres || []).join(', ') || 'All'}</td></tr></table>
        <button class="btn green big" data-act="play" data-id="${g.id}" data-name="${g.name}">Play</button>
        ${mine ? html` <a class="btn" href="#/configure/${g.id}">Configure this game</a>` : ''}
        <p class="small muted">Games run in the Guts&amp;Bolts app (Windows, Mac, Linux and Android).</p></div></div>
    <h2>Description</h2><p style="white-space:pre-wrap">${g.description || 'No description yet.'}</p>
    ${(g.badges || []).length ? html`<h2>Badges</h2><div class="list">${g.badges.map((b) => html`<div>
        ${gameBadgeIcon(b, 44)}<span class="grow"><b>${b.name}</b><br><span class="small muted">${b.description || ''}</span></span>
        <span class="small muted">Won ${b.awarded || 0} time${b.awarded === 1 ? '' : 's'}</span></div>`)}</div>` : ''}
    <h2>Servers</h2>
    ${servers.length ? html`<div class="server-grid">${shown.map(card)}</div>${pager}`
      : html`<p class="muted">Nobody's playing right now. Be the first!</p>`}`);
};

pages.catalog = async () => {
  const q = new URLSearchParams(location.hash.split('?')[1] || '');
  const kind = q.get('kind') || 'clothing', query = q.get('q') || '';
  const r = await pageCall('list', { kind, query, limit: 100 });
  const tab = (k, label) => html`<a class="btn ${kind === k ? 'blue' : ''}" href="#/catalog?kind=${k}">${label}</a>`;
  show(html`<h1>Catalog</h1>
    <div class="tabs">${tab('clothing', 'Everything')}${tab('hat', 'Hats')}${tab('hair', 'Hair')}${tab('face', 'Faces')}${tab('faceacc', 'Face Accessories')}${tab('neck', 'Neck')}
      ${tab('shoulder', 'Shoulder')}${tab('waist', 'Waist')}${tab('shirt', 'Shirts')}${tab('tshirt', 'T-Shirts')}${tab('pants', 'Pants')}</div>
    <form class="row" data-form="catalogSearch"><input type="hidden" name="kind" value="${kind}">
      <input type="search" name="q" placeholder="Search the catalog" value="${query}" style="max-width:280px">
      <button class="btn blue">Search</button></form><br>
    ${r.ok ? (r.assets.length ? html`<div class="grid">${r.assets.map(itemCard)}</div>` : html`<p class="muted">Nothing here yet.</p>`)
      : html`<p class="error">${r.error}</p>`}`);
  upgradeItemPictures();
};

pages.item = async (id) => {
  const r = await pageCall('list', { kind: 'clothing', limit: 100 });
  const a = r.ok && r.assets.find((x) => x.id === id);
  if (!a) { show(html`<h1>Item not found</h1>`); return; }
  const owned = signedIn() && (me.owned || []).includes(a.id);
  const canEdit = signedIn() && (a.creator === me.id || me.staff);
  const L = a.limited;
  const copies = L ? ((await pageCall('item.copies', { id: a.id })).copies || []) : [];
  const forSale = copies.filter((c) => c.price > 0 && !c.mine).sort((x, y) => x.price - y.price);
  const mineCopies = copies.filter((c) => c.mine);
  const soldOut = L && L.left <= 0;
  const hex = (c) => '#' + (Array.isArray(c) ? c : [200, 60, 60]).map((v) => Number(v).toString(16).padStart(2, '0')).join('');
  show(html`<p><a href="#/catalog">&lt; Catalog</a></p>
    <div class="hero"><div class="card square"><div class="pic" id="item3d">${itemIcon(a)}</div>${L ? html`<span class="limited-tag">LIMITED</span>` : ''}
      ${drawable3d(a) ? html`<div class="small muted" style="text-align:center">Drag to turn</div>` : ''}</div>
      <div><h1>${a.name}</h1><p>${KINDS[a.kind]} by <a href="#/user/${a.creator}">${a.creatorName}</a>${verified(a.creatorVerified)}</p>
        <p>${a.price > 0 ? bolts(a.price) : 'Free'} · <span class="muted">${a.sales || 0} sold</span></p>
        ${L ? html`<p class="limited-line">${soldOut ? html`<b class="error">Sold out</b>` : html`<b>${L.left}</b> of ${L.stock} left`}
          ${L.resellers ? html` · ${L.resellers} for resale from ${bolts(L.lowest)}` : ''}</p>` : ''}
        ${owned ? html`<p class="ok"><b>You own this${mineCopies.length ? ' (#' + mineCopies.map((c) => c.serial).join(', #') + ')' : ''}.</b></p><p class="small muted">Wear it from the Avatar page in the Guts&amp;Bolts app.</p>`
          : soldOut ? html`<button class="btn big" disabled>Sold out</button>`
          : html`<button class="btn green big" data-act="buy" data-id="${a.id}">${a.price > 0 ? 'Buy' : 'Get it'}</button>`}
        ${canEdit ? html` <button class="btn" data-act="toggle" data-target="#itemEdit">Edit item</button>` : ''}
        <p style="white-space:pre-wrap">${a.description}</p></div></div>
    ${canEdit ? html`<div class="box" id="itemEdit" hidden><h2 class="boxhead">Edit item</h2>
      <form class="form" data-form="itemEdit"><input type="hidden" name="id" value="${a.id}">
        <label>Name</label><input type="text" name="name" maxlength="50" value="${a.name}" required>
        <label>Description</label><textarea name="description" maxlength="1000">${a.description || ''}</textarea>
        <label>Price (Bolts)</label><input type="number" name="price" min="0" value="${a.price || 0}" style="max-width:140px">
        ${drawable3d(a) ? html`<label>Colour</label><input type="color" name="color" value="${hex(a.meta && a.meta.color)}">` : ''}
        ${a.kind === 'face' ? html`<label>New picture <span class="muted small">(optional, a .png face)</span></label><input type="file" name="picture" accept="image/png">`
          : a.kind === 'tshirt' ? html`<label>New picture <span class="muted small">(optional, a .png or .jpg)</span></label><input type="file" name="picture" accept="image/png,image/jpeg">`
          : !drawable3d(a) ? html`<p class="small muted">To change how it looks or where it sits, open it in Studio's Accessory window and upload it again.</p>`
          : a.kind === 'hat' ? html`<label>Shape</label><select name="style">${[[1, 'Top Hat'], [2, 'Cap'], [3, 'Crown']].map(([v, l]) => html`<option value="${v}" ${Number(a.meta && a.meta.style) === v ? 'selected' : ''}>${l}</option>`)}</select>`
          : html`<label>New picture <span class="muted small">(optional, from the <a href="templates/${a.kind}_template.png" download>template</a>)</span></label><input type="file" name="picture" accept="image/png">`}
        <p><button class="btn green">Save</button> <span id="itemEditMsg"></span></p></form>
      ${me.official && a.creator === me.id && canBeLimited(a) ? html`<form class="form" data-form="itemLimited"><input type="hidden" name="id" value="${a.id}">
        <label>${L ? 'Change the stock' : 'Make it Limited'} <span class="muted small">(only Guts can do this)</span></label>
        <input type="number" name="stock" min="1" value="${L ? L.stock : 100}" style="max-width:140px">
        <p class="small muted">A Limited has a fixed number of numbered copies. When they're sold out, people buy and sell them from each other, and trade them.</p>
        <p><button class="btn gold">${L ? 'Save stock' : 'Make Limited'}</button></p></form>` : ''}</div>` : ''}
    ${L ? html`<h2>Resellers</h2>${forSale.length ? html`<table class="stats resellers"><tr><th>Seller</th><th>Copy</th><th>Price</th><th></th></tr>
        ${forSale.map((c) => html`<tr><td><a href="#/user/${c.owner}">${c.ownerName}</a></td><td>#${c.serial}</td><td>${bolts(c.price)}</td>
          <td><button class="btn green small" data-act="resaleBuy" data-id="${a.id}" data-serial="${c.serial}" data-price="${c.price}">Buy</button></td></tr>`)}</table>`
        : html`<p class="muted">Nobody is selling a copy right now.</p>`}
      ${mineCopies.length ? html`<h2>Your copies</h2>${mineCopies.map((c) => html`<form class="row" data-form="resaleList"><input type="hidden" name="id" value="${a.id}">
          <input type="hidden" name="serial" value="${c.serial}"><b>#${c.serial}</b>
          <input type="number" name="price" min="0" value="${c.price || ''}" placeholder="Price in Bolts" style="max-width:150px">
          <button class="btn small ${c.price ? '' : 'blue'}">${c.price ? 'Change price' : 'Sell'}</button>
          ${c.price ? html`<span class="muted small">On sale for ${c.price}. Set 0 to take it off sale.</span>` : html`<span class="muted small">You get ${70}% when it sells.</span>`}</form>`)}` : ''}` : ''}`);
  if (drawable3d(a)) mountAvatar($('#item3d'), MANNEQUIN, [a], { width: 300, height: 340 }).catch(() => {});   // a turnable 3D view
  else loadThumbs();
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
  const tabs = [['games', 'My Games'], ['model', 'Models'], ['decal', 'Decals'], ['audio', 'Audio'], ['hat', 'Hats'], ['accessory', 'Accessories'],
    ['shirt', 'Shirts'], ['tshirt', 'T-Shirts'], ['pants', 'Pants'], ...(signedIn() && me.official ? [['face', 'Faces']] : []), ['plugin', 'Plugins'], ['library', 'Library']];
  const head = html`<h1>Create</h1><div class="tabs">${tabs.map(([k, l]) => html`<a class="btn ${tab === k ? 'blue' : ''}" href="#/create/${k}">${l}</a>`)}</div>`;
  if (tab === 'library') { await libraryPage(head); return; }
  if (tab === 'model') { await myModelsPage(head); return; }
  if (tab === 'accessory') { await myAccessoriesPage(head); return; }
  const r = signedIn() ? await pageCall('list', { creator: me.id, limit: 100 }) : { ok: true, assets: [] };
  const mine = r.ok ? r.assets : [];
  const kind = tab === 'games' ? 'game' : tab;
  const list = mine.filter((a) => a.kind === kind);
  const costs = !signedIn() ? html`Make games in <b>Studio</b> and publish them here, and upload decals, audio, clothes and plugins.
      <a href="#/signup">Sign up</a> or <a href="#/login">log in</a> to start.`
    : me.verified ? html`<b>You're Verified!</b> Uploading is free, with no daily limit, and you can sell what you make.`
    : html`Uploading costs a few Bolts (decals 5, clothes 10, audio 20, plugins 20; games are free).
      <span class="muted">${me.uploadsLeft} uploads left today.</span>`;
  const clothing = ['hat', 'shirt', 'pants'].includes(kind), face = kind === 'face', tee = kind === 'tshirt';
  const accept = { decal: '.png,.jpg,.jpeg', audio: '.mp3,.wav,.ogg,.flac', plugin: '.lua', game: '.gbscene' }[kind] || '';
  const form = html`<h2>${kind === 'game' ? 'Publish a game' : 'Upload a new ' + KINDS[kind]}</h2>
    <form class="form" data-form="upload"><input type="hidden" name="kind" value="${kind}">
      <label>Name</label><input type="text" name="name" maxlength="50" required>
      <label>Description</label><textarea name="description" maxlength="1000"></textarea>
      ${clothing ? html`${kind === 'hat' ? html`<label>Style</label><select name="style"><option value="1">Top Hat</option>
          <option value="2" selected>Cap</option><option value="3">Crown</option></select>` : ''}
        <label>Colour</label><input type="color" name="color" value="#e63333">
        ${kind !== 'hat' ? html`<label>Picture <span class="muted small">(optional)</span></label>
          <input type="file" name="picture" accept="image/png">
          <p class="small muted">A 585 x 559 .png painted on the
            <a href="templates/${kind}_template.png" download>${kind} template</a>. See-through bits show the colour above.</p>` : ''}`
      : tee ? html`<label>Picture</label><input type="file" name="file" accept="image/png,image/jpeg" required>
        <p class="small muted">Any .png or .jpg (up to 1024 x 1024). It's worn flat on the front of the torso, over your shirt.
          Square pictures fit best; see-through bits show the shirt underneath.</p>`
      : face ? html`<label>Picture</label><input type="file" name="file" accept="image/png" required>
        <p class="small muted">A square .png of the face, see-through around the eyes and mouth (like 256 x 256). It's drawn on the front of the head.</p>`
      : html`<label>File</label><input type="file" name="file" accept="${accept}" required>
        <p class="small muted">${{ decal: 'A .png or .jpg picture (up to 4 MB).', audio: 'An .mp3, .wav, .ogg or .flac file (up to 6 MB).',
          plugin: 'A Lua plugin for Studio.', game: 'A .gbscene file saved from Studio (or use File > Publish in Studio).' }[kind]}</p>`}
      ${['decal', 'audio'].includes(kind) ? html`<p class="small muted">${kind === 'decal' ? 'Decals' : 'Sounds'} are always free: anyone can use them in their games.</p>`
        : (me.verified || face) && kind !== 'game' ? html`<label>Price (Bolts)</label><input type="number" name="price" min="0" value="0">` : ''}
      ${kind === 'decal' || face || tee ? html`<img class="thumb" id="preview" alt="Preview" hidden style="width:120px;height:120px;margin-top:10px">` : ''}
      <p><button class="btn green">${FEES[kind] && !me.verified ? 'Upload for ' + FEES[kind] + ' Bolts' : 'Upload (free)'}</button>
        <span id="uploadMsg"></span></p></form>`;
  // Hats are for Verified creators; shirts and pants are open to everyone.
  const hatLocked = kind === 'hat' && signedIn() && !me.verified;
  const shownForm = hatLocked ? html`<h2>Upload a new Hat</h2><p class="error">Only Verified creators can make hats.</p>
    <p class="muted">You can still make <a href="#/create/shirt">shirts</a> and <a href="#/create/pants">pants</a>!</p>`
    : face && !(signedIn() && me.official) ? html`<p class="error">Only Guts can make faces.</p>`
    : kind === 'hat' ? html`${form}<p class="small muted">Want a hat that's your own shape? Build it in <b>Studio</b> and use
      <b>Avatar &gt; Accessories</b> to place it on the head and upload it.</p>` : form;
  const row = (a) => html`<div>
      ${a.kind === 'game' ? gameIcon(a, 48) : ''}
      ${a.kind === 'decal' ? html`<img class="thumb" data-decal="${a.id}" alt="">` : ''}
      ${clothing || face ? html`<div class="thumb" style="display:flex;align-items:center;justify-content:center;background:#fff">${itemIcon(a)}</div>` : ''}
      <div class="grow"><b>${a.name}</b><br><span class="small muted">
        ${a.kind === 'game' ? html`${a.plays} plays · ${ACCESS_NAMES[a.access || 'public']}` : html`${a.price > 0 ? bolts(a.price) : 'free'} · ${a.sales} sold`}
        ${a.kind === 'decal' || a.kind === 'audio' ? html` · ID gb:${a.id}` : ''}</span></div>
      ${a.kind === 'decal' || a.kind === 'audio' ? html`<button class="btn small" data-act="copyId" data-id="${a.id}">Copy ID</button>` : ''}
      ${a.kind === 'game' ? html`<a class="btn small" href="#/game/${a.id}">View</a>
        <a class="btn small blue" href="#/configure/${a.id}">Configure</a>` : ''}
      <button class="btn small red" data-act="deleteAsset" data-id="${a.id}" data-name="${a.name}">Delete</button></div>`;
  show(html`${head}<div class="box">${costs}</div>
    ${kind === 'game' ? html`<h2>My published games</h2>` : html`<h2>My ${KINDS[kind]}${kind === 'pants' ? '' : 's'}</h2>`}
    ${list.length ? html`<div class="list">${list.map(row)}</div>`
      : html`<p class="muted">${signedIn() ? 'Nothing yet.' : 'Log in to see what you\'ve made.'}</p>`}
    ${shownForm}`);
  view.querySelectorAll('img[data-decal]').forEach(decalPicture);
  loadThumbs();
  const file = view.querySelector('input[type=file]'), prev = $('#preview');
  if (file && prev) file.addEventListener('change', () => { if (file.files[0]) { prev.src = URL.createObjectURL(file.files[0]); prev.hidden = false; } });
};

// Configure a game: name, description, who can play, thumbnail, icon, new version.
pages.configure = async (id) => {
  if (!signedIn()) { show(html`<h1>Configure game</h1>${needSignIn('change your games')}`); return; }
  const r = await pageCall('list', { creator: me.id, kind: 'game', limit: 100 });
  const g = r.ok && r.assets.find((a) => a.id === id);
  if (!g) { show(html`<h1>Configure game</h1><p class="error">${r.ok ? 'That isn\'t one of your games.' : r.error}</p>`); return; }
  const access = g.access || 'public';
  const choice = (v, label, note) => html`<label class="choice"><input type="radio" name="access" value="${v}" ${access === v ? 'checked' : ''}>
    <b>${label}</b> <span class="muted small">${note}</span></label>`;
  show(html`<p><a href="#/create/games">&lt; My Games</a></p>
    <h1>Configure: ${g.name}</h1>
    <form class="configure" data-form="configure"><input type="hidden" name="id" value="${g.id}">
      <div class="box"><h2 class="boxhead">Basic settings</h2>
        <label>Name</label><input type="text" name="name" maxlength="50" value="${g.name}" required>
        <label>Description</label><textarea name="description" maxlength="1000" rows="5">${g.description || ''}</textarea></div>
      <div class="box"><h2 class="boxhead">Genres and players</h2>
        <p class="small muted">Pick up to 3 genres so people can find your game.</p>
        <div class="genre-picks">${GENRES.map((gn) => html`<label class="choice"><input type="checkbox" name="genre" value="${gn}" ${(g.genres || []).includes(gn) ? 'checked' : ''}> ${gn}</label>`)}</div>
        <label>Players per server</label><input type="number" name="maxPlayers" min="2" max="30" value="${g.maxPlayers || 12}" style="max-width:100px"></div>
      <div class="box"><h2 class="boxhead">Who can play</h2>
        ${choice('public', 'Public', 'Everyone can find and play it.')}
        ${choice('friends', 'Friends only', 'Only your friends can see and play it.')}
        ${choice('private', 'Private', 'Only you can see and play it.')}</div>
      <div class="box"><h2 class="boxhead">Pictures</h2>
        <div class="pics">
          <div><label>Thumbnail <span class="muted small">(shown on the game's page and in lists)</span></label>
            <div class="thumb-prev">${gamePic(g, 'pic')}</div>
            <input type="file" name="thumb" accept="image/png,image/jpeg" data-preview="thumb">
            <p class="small muted">Any picture; it's cropped to 16:9.</p></div>
          <div><label>Icon <span class="muted small">(a small square)</span></label>
            <div class="icon-prev">${gameIcon(g, 128)}</div>
            <input type="file" name="icon" accept="image/png,image/jpeg" data-preview="icon">
            <p class="small muted">Any picture; it's cropped to a square.</p></div>
        </div></div>
      <div class="box"><h2 class="boxhead">Update the game</h2>
        <label>New version <span class="muted small">(optional)</span></label>
        <input type="file" name="file" accept=".gbscene">
        <p class="small muted">A .gbscene saved from Studio. Players get it the next time they join.</p></div>
      <p><button class="btn green big">Save</button> <a class="btn" href="#/game/${g.id}">View game</a>
        <button type="button" class="btn red" data-act="deleteAsset" data-id="${g.id}" data-name="${g.name}" style="float:right">Delete game</button>
        <span id="configMsg"></span></p>
    </form>
    <div class="box"><h2 class="boxhead">Badges</h2>
      <p class="small muted">Make badges players earn in your game. Give one from a script:
        <code>game:GetService("BadgeService"):AwardBadge(player, "badge-id")</code>
        (it only works in online servers of this game).</p>
      ${(g.badges || []).length ? html`<div class="list">${g.badges.map((b) => html`<div>${gameBadgeIcon(b, 40)}
          <span class="grow"><b>${b.name}</b> <span class="small muted">${b.description || ''}</span><br>
            <code class="small">${b.id}</code> · <span class="small muted">won ${b.awarded || 0} times</span></span>
          <button class="btn small" data-act="copyText" data-text="${b.id}">Copy ID</button>
          <button class="btn small red" data-act="deleteBadge" data-game="${g.id}" data-badge="${b.id}" data-name="${b.name}">Delete</button></div>`)}</div>`
        : html`<p class="muted">No badges yet.</p>`}
      <form class="form" data-form="newBadge"><input type="hidden" name="game" value="${g.id}">
        <label>Badge name</label><input type="text" name="name" maxlength="40" required>
        <label>Description <span class="muted small">(how to get it)</span></label><input type="text" name="description" maxlength="300">
        <label>Colour</label><input type="color" name="color" value="#f0b428">
        <p><button class="btn green">Make badge</button></p></form></div>`);
  // Show a picked picture straight away.
  view.querySelectorAll('input[data-preview]').forEach((inp) => inp.addEventListener('change', () => {
    const file = inp.files[0];
    if (!file) return;
    const box = view.querySelector(inp.dataset.preview === 'icon' ? '.icon-prev' : '.thumb-prev');
    box.innerHTML = html`<img src="${URL.createObjectURL(file)}" alt="">`.s;
  }));
};

// Trades: offers of your limited copies for someone else's.
pages.trades = async () => {
  if (!signedIn()) { show(html`<h1>Trades</h1>${needSignIn('trade Limited items')}`); return; }
  const r = await pageCall('trade.list', {});
  const list = (items) => items.map((i) => html`<a href="#/item/${i.id}">${i.name} #${i.serial}</a>`).reduce((acc, x, k) => html`${acc}${k ? ', ' : ''}${x}`, html``);
  const row = (tr) => {
    const incoming = tr.to === me.id;
    return html`<div><span class="grow">${incoming ? html`<b><a href="#/user/${tr.from}">${tr.fromName}</a></b> offers ${list(tr.give)} for your ${list(tr.get)}`
        : html`You offered ${list(tr.give)} to <b><a href="#/user/${tr.to}">${tr.toName}</a></b> for their ${list(tr.get)}`}
      <br><span class="small muted">${ago(tr.created)} · ${tr.status}</span></span>
      ${tr.status === 'open' ? (incoming ? html`<button class="btn green small" data-act="tradeAnswer" data-op="trade.accept" data-id="${tr.id}">Accept</button>
          <button class="btn small" data-act="tradeAnswer" data-op="trade.decline" data-id="${tr.id}">Decline</button>`
        : html`<button class="btn small" data-act="tradeAnswer" data-op="trade.cancel" data-id="${tr.id}">Cancel</button>`) : ''}</div>`;
  };
  const open = r.ok ? r.trades.filter((x) => x.status === 'open') : [], done = r.ok ? r.trades.filter((x) => x.status !== 'open') : [];
  show(html`<h1>Trades</h1>
    <p class="muted">Trade your Limited items with other players. Open someone's profile and press <b>Trade</b> to make an offer.</p>
    <h2>Your Limiteds</h2>${r.ok && r.items.length ? html`<div class="row">${r.items.map((i) => html`<a class="chip" href="#/item/${i.id}">${i.name} #${i.serial}</a>`)}</div>`
      : html`<p class="muted">You don't have any Limited items yet.</p>`}
    <h2>Open</h2>${open.length ? html`<div class="list">${open.map(row)}</div>` : html`<p class="muted">No open trades.</p>`}
    <h2>History</h2>${done.length ? html`<div class="list">${done.slice(0, 30).map(row)}</div>` : html`<p class="muted">Nothing yet.</p>`}`);
};

pages.trade = async (id) => {
  if (!signedIn()) { show(html`<h1>Trade</h1>${needSignIn('trade Limited items')}`); return; }
  const [theirs, mine] = await Promise.all([pageCall('trade.inventory', { user: id }), pageCall('trade.inventory', { user: me.id })]);
  if (!theirs.ok) { show(html`<h1>Trade</h1><p class="error">${theirs.error}</p>`); return; }
  const pickList = (items, side) => items.length ? html`<div class="trade-pick">${items.map((i) => html`<label class="choice">
      <input type="checkbox" name="${side}" value="${i.id}|${i.serial}"> ${i.name} <b>#${i.serial}</b></label>`)}</div>`
    : html`<p class="muted">No Limited items.</p>`;
  show(html`<p><a href="#/user/${theirs.user.userId}">&lt; ${theirs.user.name}</a></p><h1>Trade with ${theirs.user.name}</h1>
    <form class="trade" data-form="tradeSend"><input type="hidden" name="to" value="${theirs.user.id}">
      <div class="trade-cols"><div class="box"><h2 class="boxhead">You give</h2>${pickList(mine.ok ? mine.items : [], 'give')}</div>
        <div class="box"><h2 class="boxhead">You get</h2>${pickList(theirs.items, 'get')}</div></div>
      <p class="small muted">Up to 4 on each side. They can accept or decline; the swap only happens if you both still have everything.</p>
      <p><button class="btn green">Send trade offer</button> <span id="tradeMsg"></span></p></form>`);
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
  const friendBtn = f === 'self' ? '' : !signedIn() ? html`<button class="btn green small" data-act="friend" data-op="friends.add" data-user="${u.id}">Add friend</button>`
    : f === 'friends' ? html`<button class="btn small" data-act="friend" data-op="friends.remove" data-user="${u.id}">Unfriend</button>`
      : f === 'sent' ? html`<button class="btn small" data-act="friend" data-op="friends.cancel" data-user="${u.id}">Cancel request</button>`
        : f === 'received' ? html`<button class="btn green small" data-act="friend" data-op="friends.accept" data-user="${u.id}">Accept friend request</button>`
          : html`<button class="btn green small" data-act="friend" data-op="friends.add" data-user="${u.id}">Add friend</button>`;
  const games = r.creations.filter((a) => a.kind === 'game'), items = r.creations.filter((a) => WEARABLE.includes(a.kind));
  // What they wear (older servers don't say: look it up in the catalog).
  let worn = r.wearing;
  if (!worn) {
    const ids = u.avatar && Array.isArray(u.avatar.wearing) ? u.avatar.wearing : [];
    worn = ids.length ? ((await pageCall('list', { kind: 'clothing', limit: 100 })).assets || []).filter((a) => ids.includes(a.id)) : [];
  }
  const friends = r.friends || [];
  const badgeNames = { admin: 'Administrator', verified: 'Verified', staff: 'Staff', tester: 'Tester', bughunter: 'Bug Hunter', featured: 'Featured Creator' };
  const online = r.online === undefined ? null : r.online;
  show(html`<div class="profile-head">
      <h1>${u.username}${verified(u.verified)}</h1>
      ${online === null ? '' : html`<span class="presence ${online ? 'on' : ''}">${online ? '[ Online ]' : '[ Offline ]'}</span>`}
      <span class="grow"></span>${friendBtn}${f === 'self' ? html` <a class="btn small" href="#/avatar">Edit avatar</a>`
        : signedIn() ? html` <a class="btn small" href="#/trade/${u.id}">Trade</a>` : ''}</div>
    ${(u.pastNames || []).length ? html`<p class="small muted past-names">Past usernames: ${u.pastNames.join(', ')}</p>` : ''}
    <div class="profile">
      <div class="profile-left">
        <div class="box avatar-box"><div id="profileAvatar">${avatarSvg(u.avatar, 200, worn)}</div>
          <div class="small muted">Drag to turn</div></div>
        <div class="box"><h2 class="boxhead">Currently Wearing</h2>
          ${worn.length ? html`<div class="wearing">${worn.map((it) => html`<a class="card square" href="#/item/${it.id}" title="${it.name}">
              <div class="pic">${itemIcon(it)}</div><div class="name small">${it.name}</div></a>`)}</div>`
            : html`<p class="muted small">Nothing from the catalog${u.avatar && u.avatar.hat ? ' (just a hat)' : ''}.</p>`}</div>
        <div class="box"><h2 class="boxhead">Statistics</h2>
          <table class="stats"><tr><td>Joined</td><td>${new Date(u.created * 1000).toLocaleDateString()} (${ago(u.created)})</td></tr>
            <tr><td>User number</td><td>#${u.userId}</td></tr>
            <tr><td>Friends</td><td>${r.friendCount}</td></tr>
            ${r.placeVisits !== undefined ? html`<tr><td>Place visits</td><td>${r.placeVisits}</td></tr>` : ''}
            <tr><td>Games made</td><td>${games.length}</td></tr></table>
          ${u.official ? html`<p><b>Guts&amp;Bolts staff</b></p>` : ''}${u.banned ? html`<p class="error">Banned${u.banReason ? ': ' + (BAN_REASONS.find((r) => r[0] === u.banReason) || ['', ''])[1] : ''}</p>` : ''}</div>
        ${(u.badges || []).length ? html`<div class="box"><h2 class="boxhead">Guts&amp;Bolts Badges</h2>
          <p class="small muted">Given by Guts&amp;Bolts staff.</p><div class="row">
          ${u.badges.map((b) => html`<span class="badge-pill">${badgeNames[b] || b}</span>`)}</div></div>` : ''}
        <div class="box"><h2 class="boxhead">Game Badges (${(r.gameBadges || []).length})</h2>
          ${(r.gameBadges || []).length ? html`<div class="badge-grid">${r.gameBadges.slice(0, 24).map((b) => html`<a class="game-badge" href="#/game/${b.game}"
              title="${b.name}${b.description ? ': ' + b.description : ''} (${b.gameName})">${gameBadgeIcon(b, 48)}<span>${b.name}</span></a>`)}</div>`
            : html`<p class="muted small">No badges from games yet.</p>`}</div>
      </div>
      <div class="profile-right">
        <div class="box"><h2 class="boxhead">Friends (${r.friendCount})${f === 'self' ? html` <a class="small" href="#/friends" style="float:right">See all</a>` : ''}</h2>
          ${friends.length ? html`<div class="friends-grid">${friends.map((p) => html`<a class="friend" href="#/user/${p.userId}">
              <div class="friend-pic" data-friend-avatar="${p.id}">${avatarSvg(p.avatar, 60)}</div>
              <div class="small"><span class="dot ${p.online ? 'on' : ''}"></span>${p.username || p.name}</div></a>`)}</div>`
            : html`<p class="muted small">${f === 'self' ? 'No friends yet. Find people on the People page!' : 'No friends yet.'}</p>`}</div>
        <div class="box"><h2 class="boxhead">Games</h2>
          ${games.length ? html`<div class="grid">${games.map(gameCard)}</div>` : html`<p class="muted small">None yet.</p>`}</div>
        ${items.length ? html`<div class="box"><h2 class="boxhead">Creations</h2><div class="grid">${items.map(itemCard)}</div></div>` : ''}
        <div class="box"><h2 class="boxhead">Groups</h2>
          ${r.groups.length ? html`<div class="list">${r.groups.map((g) => html`<div><a class="grow" href="#/group/${g.id}">${g.name}</a>
            <span class="muted small">${g.role}</span></div>`)}</div>` : html`<p class="muted small">None yet.</p>`}</div>
      </div>
    </div>`);
  // The 3D avatar (the flat one stays if the browser can't do 3D).
  mountAvatar($('#profileAvatar'), u.avatar, worn, { width: 220 }).catch(() => {});
  for (const p of friends) {
    avatarPicture(p.avatar, [], 60).then((url) => {
      const box = view.querySelector(`[data-friend-avatar="${p.id}"]`);
      if (url && box) box.innerHTML = html`<img src="${url}" alt="" width="60" height="75">`.s;
    }).catch(() => {});
  }
};

pages.friends = async () => {
  const r = signedIn() ? await pageCall('friends.list', {}) : { ok: true, friends: [], incoming: [], outgoing: [] };
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
      : signedIn() ? html`<p class="muted">No friends yet. Find people on the <a href="#/people">People</a> page!</p>`
        : html`<p class="muted">Your friends show up here once you <a href="#/login">log in</a>.
          Meanwhile, look around the <a href="#/people">People</a> page!</p>`}
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
      <p><button class="btn green">Make it${me.verified ? ' (free)' : ' for 50 Bolts'}</button></p></form>` : html`<form class="form" data-form="groupCreate">
      <label>Name</label><input type="text" name="name" maxlength="40">
      <p><button class="btn green">Make a group</button></p></form>`}`);
};

pages.group = async (id) => {
  const r = await pageCall('groups.get', { id });
  if (!r.ok) { show(html`<h1>Group not found</h1><p class="muted">${r.error}</p>`); return; }
  const g = r.group, role = r.myRole, manage = role === 'Owner' || role === 'Admin';
  const color = '#' + Number(g.color).toString(16).padStart(6, '0');
  const join = !signedIn() ? html`<button class="btn green" data-act="group" data-op="groups.join" data-id="${g.id}">${g.open ? 'Join' : 'Ask to join'}</button>` : role ? (role === 'Owner' ? '' : html`<button class="btn small" data-act="group" data-op="groups.leave" data-id="${g.id}">Leave</button>`)
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
  const r = signedIn() ? await pageCall('list', { kind: 'clothing', limit: 100 }) : { ok: true, assets: [] };
  const owned = (r.ok ? r.assets : []).filter((a) => (me.owned || []).includes(a.id));
  if (!avatarDraft) avatarDraft = Object.assign(defaultAvatar(), JSON.parse(JSON.stringify(me.avatar || {})));
  const a = avatarDraft;
  const wornItems = owned.filter((it) => a.wearing.includes(it.id));
  show(html`<h1>Avatar</h1>
    <div class="row top">
      <div class="box" style="text-align:center;margin-right:16px"><div id="avatarPreview">${avatarSvg(a, 180, wornItems)}</div>
        <div class="small muted">Drag to turn</div>
        <p><button class="btn green" data-act="saveAvatar">Save</button>
          <button class="btn" data-act="resetAvatar">Undo changes</button></p>
        <p class="small muted" style="max-width:200px">${signedIn() ? 'Your avatar is the same in the app and on the website.'
          : 'Try things on! Log in to save your avatar.'}</p></div>
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
  let avatar3d = null;
  const preview = () => {
    if (avatar3d) avatar3d.set(a, wornItems);
    else $('#avatarPreview').innerHTML = avatarSvg(a, 180, wornItems).s;
  };
  mountAvatar($('#avatarPreview'), a, wornItems, { width: 240 }).then((c) => { avatar3d = c; }).catch(() => {});
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
  const r = signedIn() ? await pageCall('bolts.history', {}) : { ok: true, history: [] };
  show(html`<h1>Bolts</h1>
    <div class="box row"><div class="grow" style="font-size:24px">${bolts(signedIn() ? me.bolts : 0)}</div>
      ${!signedIn() || me.canDaily ? html`<button class="btn green" data-act="daily">Claim today's 25 Bolts</button>` : html`<span class="muted">Daily Bolts claimed. Come back tomorrow!</span>`}</div>
    <p class="muted">Earn Bolts every day, by playing games in the app (5 every 5 minutes, up to 50 a day) and by selling things you make.</p>
    <h2>Redeem a code</h2><form class="row" data-form="redeem"><input type="text" name="code" placeholder="BOLTS-..." style="max-width:360px">
      <button class="btn blue">Redeem</button></form>
    <h2>History</h2>
    <div class="list">${r.ok && r.history.length ? r.history.slice().reverse().map((h) => html`<div><span class="grow">${h.reason}</span>
      <span class="${h.amount >= 0 ? 'ok' : 'error'}">${h.amount >= 0 ? '+' : ''}${h.amount}</span><span class="small muted">${ago(h.time)}</span></div>`)
      : html`<p class="muted">${signedIn() ? 'Nothing yet.' : 'Log in to see your Bolts. New accounts start with 100!'}</p>`}</div>`);
};

// Forgot password: a code goes to the account's email, then pick a new password.
pages.forgot = async () => {
  if (signedIn()) { show(html`<h1>Forgot password</h1><p>You're logged in as ${me.username}. You can change your password in
    <a href="#/settings">Account Settings</a>.</p>`); return; }
  show(html`<div class="form" style="margin:0 auto">
    <h1>Forgot your password?</h1>
    <p class="muted">If your account has an email, we'll send it a code.</p>
    <form data-form="forgot"><label>Username</label><input type="text" name="username" autocomplete="username" required maxlength="20">
      <p><button class="btn blue">Email me a code</button></p><p id="forgotMsg"></p></form>
    <form data-form="reset" id="resetForm" hidden><input type="hidden" name="username">
      <label>Code from your email</label><input type="text" name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="6" required>
      <label>New password</label><input type="password" name="password" autocomplete="new-password" required>
      <label>New password again</label><input type="password" name="password2" autocomplete="new-password" required>
      <p><button class="btn green big" style="width:100%">Set new password</button></p><p class="error" id="resetMsg"></p>
      <p class="small muted">This logs your other devices out. Log in on them again with the new password.</p></form>
    <p class="small muted">No email on your account? Log in on a device where you're still logged in (like the Guts&amp;Bolts app)
      and add one in Account Settings, so this works next time.</p></div>`);
};

// Account Settings: email, two-step verification, password.
pages.settings = async () => {
  if (!signedIn()) { show(html`<h1>Account Settings</h1>${needSignIn('change your account settings')}`); return; }
  const noPw = !me.hasPassword;
  show(html`<h1>Account Settings</h1>
    <div class="settings">
      <div class="box"><h2 class="boxhead">Account</h2>
        <table class="stats"><tr><td>Username</td><td><b>${me.username}</b></td></tr><tr><td>User number</td><td>#${me.userId}</td></tr>
          <tr><td>Password</td><td>${noPw ? html`<span class="error">Not set yet</span> (set one in the app: Avatar &gt; Your account)` : 'Set'}</td></tr>
          ${(me.pastNames || []).length ? html`<tr><td>Past usernames</td><td>${me.pastNames.join(', ')}</td></tr>` : ''}
          <tr><td>Joined</td><td>${me.created ? new Date(me.created * 1000).toLocaleDateString() : '?'}</td></tr></table>
        ${me.official ? html`<form class="form" data-form="joinDate"><label>Join date <span class="muted small">(Guts only)</span></label>
          <input type="date" name="date" required value="${me.created ? new Date(me.created * 1000).toISOString().slice(0, 10) : ''}" style="max-width:180px">
          <p><button class="btn blue">Save join date</button> <span id="joinMsg"></span></p></form>` : ''}
        <form class="form" data-form="rename"><label>Change your username <span class="muted small">(1,000 Bolts)</span></label>
          <input type="text" name="username" maxlength="20" required placeholder="3-20 letters or numbers" autocomplete="off">
          <p class="small muted">Your old username stays on your profile under "Past usernames", and nobody else can ever take it.
            You can switch back to it later (for the same price).</p>
          <p><button class="btn blue">Change username</button> <span id="renameMsg"></span></p></form></div>
      <div class="box"><h2 class="boxhead">Email</h2>
        ${me.canMail ? '' : html`<p class="box gold small">Email isn't switched on for this server yet, so codes can't be sent.</p>`}
        <p>${me.email ? html`Your email: <b>${me.email}</b> <span class="ok">(confirmed)</span>` : html`<span class="muted">No email yet.</span>
          Add one so you can reset your password and turn on two-step verification.`}</p>
        <form class="form" data-form="setEmail"><label>${me.email ? 'Change email' : 'Add an email'}</label>
          <input type="email" name="email" autocomplete="email" required maxlength="254" placeholder="you@example.com">
          ${me.twoStep ? html`<label>Password</label><input type="password" name="password" autocomplete="current-password" required>` : ''}
          <p><button class="btn blue">Send a code</button> <span id="emailMsg"></span></p></form>
        <form class="form" data-form="verifyEmail" ${me.emailPending ? '' : 'hidden'} id="verifyForm">
          <label>Code we sent to <span id="pendingTo">${me.emailPending}</span></label>
          <input type="text" name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="6" required>
          <p><button class="btn green">Confirm email</button> <span id="verifyMsg"></span></p></form>
        ${me.email ? html`<form class="form" data-form="removeEmail"><label>Remove your email</label>
          ${noPw ? '' : html`<input type="password" name="password" placeholder="Your password" autocomplete="current-password" required>`}
          <p><button class="btn red small">Remove email</button></p></form>` : ''}</div>
      <div class="box"><h2 class="boxhead">Two-step verification</h2>
        <p>${me.twoStep ? html`<b class="ok">On.</b> Logging in on a new device needs your password <i>and</i> a code we email you.`
          : html`<b>Off.</b> Turn it on so a stolen password isn't enough to get into your account: logging in also needs a code
            from your email.`}</p>
        ${noPw ? html`<p class="muted small">Set a password first.</p>` : !me.email && !me.twoStep ? html`<p class="muted small">Add and confirm an email first.</p>`
          : html`<form class="form" data-form="twoStep"><input type="hidden" name="on" value="${me.twoStep ? '' : '1'}">
            <input type="password" name="password" placeholder="Your password" autocomplete="current-password" required>
            <p><button class="btn ${me.twoStep ? '' : 'green'}">${me.twoStep ? 'Turn off' : 'Turn on'}</button> <span id="twoStepMsg"></span></p></form>`}</div>
      ${noPw ? '' : html`<div class="box"><h2 class="boxhead">Change password</h2>
        <form class="form" data-form="changePassword">
          <label>Current password</label><input type="password" name="current" autocomplete="current-password" required>
          <label>New password</label><input type="password" name="password" autocomplete="new-password" required>
          <label>New password again</label><input type="password" name="password2" autocomplete="new-password" required>
          <p><button class="btn blue">Change password</button> <span id="pwMsg"></span></p></form></div>`}
    </div>`);
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
      ${signup ? html`<label>Password again</label><input type="password" name="password2" autocomplete="new-password" required>`
        : html`<div id="codeBox" hidden><label>Code from your email</label>
          <input type="text" name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="6" placeholder="6 digits"></div>
          <p class="small"><a href="#/forgot">Forgot your password?</a></p>`}
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
  async deleteUpdate(d) {
    if (!confirm('Delete this update?')) return;
    const r = await call('updates.delete', { id: d.id });
    toast(r.ok ? 'Deleted.' : r.error);
    if (r.ok) render();
  },
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
  // Play: games run in the Guts&Bolts app, so the website opens it with a
  // gutsandbolts:// link. Visitors pick a guest character first, like the old sites.
  play(d) {
    if (!signedIn()) { guestPicker(d.id, d.name); return; }
    launchGame(d.id, d.name, '');
  },
  pickGuest(d) { launchGame(d.id, d.name, d.guest); },
  joinServer(d) {
    if (!signedIn()) { guestPicker(d.id, d.name); return; }
    launchGame(d.id, d.name, '', d.server);
  },
  async shareServer(d) {
    const link = location.origin + location.pathname + '#/game/' + encodeURIComponent(d.id) + '?server=' + encodeURIComponent(d.server);
    try { await navigator.clipboard.writeText(link); toast('Link copied! Friends who open it can join this server.'); }
    catch { prompt('Copy this link:', link); }
  },
  serverPage(d) { serverPage = Number(d.to) || 1; render(); },
  toggle(d) { const el = $(d.target); if (el) el.hidden = !el.hidden; },
  async modelAccess(d) {
    const r = await call('model.access', { id: d.id, access: d.access });
    if (r.ok && r.me) setMe(r.me);
    toast(r.ok ? (d.access === 'public' ? 'It\'s public now.' : 'It\'s private now.') : r.error);
    render();
  },
  async resaleBuy(d) {
    if (!signedIn()) { loginPopup('buy items'); return; }
    if (!confirm('Buy copy #' + d.serial + ' for ' + d.price + ' Bolts?')) return;
    const r = await call('resale.buy', { id: d.id, serial: Number(d.serial) });
    if (r.ok) setMe(r.me);
    toast(r.ok ? 'It\'s yours!' : r.error);
    render();
  },
  async tradeAnswer(d) {
    const r = await call(d.op, { id: d.id });
    if (r.ok && r.me) setMe(r.me);
    toast(r.ok ? ({ 'trade.accept': 'Trade done!', 'trade.decline': 'Declined.', 'trade.cancel': 'Cancelled.' }[d.op]) : r.error);
    render();
  },
  async vote(d) {
    if (!signedIn()) { loginPopup('vote on games'); return; }
    const r = await call('game.vote', { id: d.id, vote: Number(d.vote) });
    if (!r.ok) toast(r.error);
    render();
  },
  async copyText(d) {
    try { await navigator.clipboard.writeText(d.text); toast('Copied ' + d.text); } catch { prompt('Copy this:', d.text); }
  },
  async deleteBadge(d) {
    if (!confirm('Delete the badge "' + d.name + '"? Players who earned it lose it from their profile.')) return;
    const r = await call('gamebadge.delete', { game: d.game, badge: d.badge });
    toast(r.ok ? 'Deleted.' : r.error);
    render();
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
    const kindOf = (id) => id.slice(0, id.lastIndexOf('-'));   // item ids are "<kind>-<hex>"
    if (w.includes(d.id)) w.splice(w.indexOf(d.id), 1);
    else {
      // One of each kind: a new T-shirt replaces the old one.
      for (let i = w.length - 1; i >= 0; --i) if (kindOf(w[i]) === kindOf(d.id)) w.splice(i, 1);
      if (w.length < 12) w.push(d.id);
    }
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
      if (d.on) {   // pick why first (the banned player sees it)
        popup(html`<h1 class="popup-title">Ban this account</h1>
          <p>Why are they being banned? They'll see this reason.</p>
          <p><select id="banReason">${BAN_REASONS.map(([k, t]) => html`<option value="${k}">${t}</option>`)}</select></p>
          <p><input id="banNote" maxlength="200" placeholder="Note for them (optional)"></p>
          <p><button class="btn red" data-act="doBan" data-id="${d.id}">Ban</button>
             <button class="btn" data-act="closeModal">Cancel</button></p>`);
        return;
      }
      r = await call('admin.ban', { to: d.id, on: false });
    }
    toast(r && r.ok ? 'Done.' : (r && r.error) || 'That didn\'t work.');
    render();
  },
  async doBan(d) {
    const reason = document.getElementById('banReason').value;
    const note = document.getElementById('banNote').value;
    const r = await call('admin.ban', { to: d.id, on: true, reason, note });
    document.querySelectorAll('.modal').forEach((m) => m.remove());
    toast(r && r.ok ? 'Banned.' : (r && r.error) || 'That didn\'t work.');
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
  async postUpdate(f) {
    const r = await call('updates.post', { name: f.name.value, version: f.version.value, tag: f.tag.value,
      summary: f.summary.value, items: f.items.value.split('\n').map((x) => x.trim()).filter(Boolean) });
    toast(r.ok ? 'Posted "' + r.update.name + '"!' : r.error);
    if (r.ok) render();
  },
  async forgot(f) {
    const msg = $('#forgotMsg');
    msg.className = 'muted'; msg.textContent = 'Sending...';
    const r = await call('account.forgot', { username: f.username.value.trim() });
    msg.className = r.ok ? 'ok' : 'error';
    msg.textContent = r.ok ? r.message : r.error;
    if (r.ok) { const rf = $('#resetForm'); rf.hidden = false; rf.username.value = f.username.value.trim(); rf.code.focus(); }
  },
  async reset(f) {
    const msg = $('#resetMsg');
    if (f.password.value.length < 8) { msg.textContent = 'Your password needs at least 8 characters.'; return; }
    if (f.password.value !== f.password2.value) { msg.textContent = 'The two passwords don\'t match.'; return; }
    msg.className = 'muted'; msg.textContent = 'Setting your new password...';
    const r = await gb.resetPassword(f.username.value, f.code.value.trim(), f.password.value);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    await hello();
    toast('Your password is changed, ' + me.username + '. You\'re logged in.', 6000);
    location.hash = '#/';
  },
  async setEmail(f) {
    const msg = $('#emailMsg');
    msg.className = 'muted'; msg.textContent = 'Sending...';
    const args = { email: f.email.value.trim() };
    if (f.password) args.auth = await gb.passwordProof(me.username, f.password.value);
    const r = await call('account.email', args);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    msg.className = 'ok'; msg.textContent = 'Sent! Check your inbox (and spam).';
    $('#pendingTo').textContent = r.sentTo;
    $('#verifyForm').hidden = false;
    $('#verifyForm').code.focus();
  },
  async verifyEmail(f) {
    const r = await call('account.emailVerify', { code: f.code.value.trim() });
    if (!r.ok) { const m = $('#verifyMsg'); m.className = 'error'; m.textContent = r.error; return; }
    me = r.me; toast('Email confirmed!'); render();
  },
  async removeEmail(f) {
    if (!confirm('Remove your email? You won\'t be able to reset your password, and two-step verification turns off.')) return;
    const r = await call('account.emailRemove', f.password ? { auth: await gb.passwordProof(me.username, f.password.value) } : {});
    if (!r.ok) { toast(r.error); return; }
    me = r.me; toast('Email removed.'); render();
  },
  async twoStep(f) {
    const r = await call('account.twoStep', { on: !!f.on.value, auth: await gb.passwordProof(me.username, f.password.value) });
    if (!r.ok) { const m = $('#twoStepMsg'); m.className = 'error'; m.textContent = r.error; return; }
    me = r.me; toast(me.twoStep ? 'Two-step verification is on.' : 'Two-step verification is off.'); render();
  },
  async changePassword(f) {
    const msg = $('#pwMsg');
    if (f.password.value.length < 8) { msg.className = 'error'; msg.textContent = 'Your new password needs at least 8 characters.'; return; }
    if (f.password.value !== f.password2.value) { msg.className = 'error'; msg.textContent = 'The two new passwords don\'t match.'; return; }
    msg.className = 'muted'; msg.textContent = 'Changing...';
    const r = await gb.changePassword(me.username, f.current.value, f.password.value);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    toast('Password changed.'); render();
  },
  async newBadge(f) {
    const hex = f.color.value.replace('#', '');
    const color = [0, 2, 4].map((i) => parseInt(hex.substr(i, 2), 16));
    const r = await call('gamebadge.create', { game: f.game.value, name: f.name.value, description: f.description.value, color });
    toast(r.ok ? 'Badge made! Copy its ID into your script.' : r.error);
    if (r.ok) render();
  },
  async configure(f) {
    const msg = $('#configMsg'), say = (t, cls = 'muted') => { msg.className = cls; msg.textContent = ' ' + t; };
    const id = f.id.value;
    say('Saving...');
    try {
      const genres = [...f.querySelectorAll('input[name=genre]:checked')].map((x) => x.value);
      if (genres.length > 3) { say('Pick up to 3 genres.', 'error'); return; }
      let r = await call('game.settings', { id, name: f.name.value, description: f.description.value, access: f.access.value,
        genres, maxPlayers: Number(f.maxPlayers.value) || 12 });
      if (!r.ok) { say(r.error, 'error'); return; }
      if (f.thumb.files[0]) {
        r = await call('thumb.set', { id, data: await pictureBase64(f.thumb.files[0], 768, 432) });
        if (!r.ok) { say('Thumbnail: ' + r.error, 'error'); return; }
      }
      if (f.icon.files[0]) {
        r = await call('icon.set', { id, data: await pictureBase64(f.icon.files[0], 256, 256) });
        if (!r.ok) { say('Icon: ' + r.error, 'error'); return; }
      }
      if (f.file.files[0]) {
        say('Uploading the new version...');
        r = await call('update', { id, data: await gb.fileBase64(f.file.files[0]) });
        if (!r.ok) { say('New version: ' + r.error, 'error'); return; }
      }
    } catch (err) { say(String(err.message || err), 'error'); return; }
    toast('Saved!');
    render();
  },
  topSearch(f) { location.hash = '#/games?' + new URLSearchParams({ q: f.q.value }); },
  librarySearch(f) { location.hash = '#/create/library?' + new URLSearchParams({ kind: f.kind.value, q: f.q.value }); },
  gameSearch(f) { location.hash = '#/games?' + new URLSearchParams({ q: f.q.value, sort: f.sort.value, genre: f.genre.value }); },
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
    const r = await gb.logIn(f.username.value.trim(), f.password.value, f.code ? f.code.value.trim() : '');
    if (r.needCode) { $('#codeBox').hidden = false; f.code.focus(); }   // two-step verification: type the emailed code
    if (!r.ok) { msg.className = r.needCode ? 'muted' : 'error'; msg.textContent = r.error; return; }
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
      args.data = f.picture && f.picture.files[0] ? await gb.fileBase64(f.picture.files[0]) : '';
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
  async itemEdit(f) {
    const args = { id: f.id.value, name: f.name.value, description: f.description.value, price: Number(f.price.value) || 0 };
    if (f.color) { const hex = f.color.value.replace('#', ''); args.color = [0, 2, 4].map((i) => parseInt(hex.substr(i, 2), 16)); }
    if (f.style) args.style = Number(f.style.value);
    if (f.picture && f.picture.files[0]) args.data = await gb.fileBase64(f.picture.files[0]);
    const r = await call('item.edit', args);
    if (!r.ok) { const m = $('#itemEditMsg'); m.className = 'error'; m.textContent = ' ' + r.error; return; }
    toast('Saved!');
    render();
  },
  async joinDate(f) {
    const r = await call('account.joinDate', { date: f.date.value });
    if (!r.ok) { const m = $('#joinMsg'); m.className = 'error'; m.textContent = ' ' + r.error; return; }
    toast('Join date saved!');
    await hello();
    render();
  },
  async itemLimited(f) {
    const r = await call('item.limited', { id: f.id.value, stock: Number(f.stock.value) || 0 });
    toast(r.ok ? 'It\'s Limited now!' : r.error);
    render();
  },
  async resaleList(f) {
    const r = await call('resale.list', { id: f.id.value, serial: Number(f.serial.value), price: Number(f.price.value) || 0 });
    toast(r.ok ? (Number(f.price.value) > 0 ? 'On sale!' : 'Taken off sale.') : r.error);
    render();
  },
  async tradeSend(f) {
    const pick = (side) => [...f.querySelectorAll('input[name=' + side + ']:checked')].map((x) => { const [id, serial] = x.value.split('|'); return { id, serial: Number(serial) }; });
    const r = await call('trade.send', { to: f.to.value, give: pick('give'), get: pick('get') });
    if (!r.ok) { const m = $('#tradeMsg'); m.className = 'error'; m.textContent = ' ' + r.error; return; }
    toast('Trade offer sent!');
    location.hash = '#/trades';
  },
  async rename(f) {
    const want = f.username.value.trim();
    if (!confirm('Change your username to "' + want + '" for 1,000 Bolts?')) return;
    const r = await call('account.rename', { username: want });
    const msg = $('#renameMsg');
    if (!r.ok) { msg.className = 'error'; msg.textContent = ' ' + r.error; return; }
    setMe(r.me);
    toast('You\'re now ' + r.me.username + '!');
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
  if (!signedIn() && NEEDS_ACCOUNT[el.dataset.act]) { loginPopup(NEEDS_ACCOUNT[el.dataset.act]); return; }
  if (el.disabled) return;
  el.disabled = true;
  try { await actions[el.dataset.act](el.dataset, el); } finally { el.disabled = false; }
});

document.addEventListener('submit', async (e) => {
  const f = e.target;
  if (!f.dataset.form || !forms[f.dataset.form]) return;
  e.preventDefault();
  if (!signedIn() && NEEDS_ACCOUNT[f.dataset.form]) { loginPopup(NEEDS_ACCOUNT[f.dataset.form]); return; }
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
  checkUpdates();
  setInterval(() => { if (document.visibilityState === 'visible') { checkRequests(); checkUpdates(); } }, 60000);
})();
