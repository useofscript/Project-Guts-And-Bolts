// The Guts&Bolts server, running on Cloudflare (a Durable Object), so it stays
// online without anyone's computer. It is a JavaScript copy of the C++ server
// in src/server (same requests, same answers, same rules), with its data in the
// Durable Object's own SQLite database instead of JSON files.
//
// Two ways in:
//   POST /api   one signed request -> one answer (the website, and the apps' requests)
//   GET  /ws    a WebSocket, used like the C++ server's TCP connections: the
//               multiplayer relay (relay.host / relay.join / accept, then a pipe
//               that passes game messages straight through).
//
// If you change a rule here, change it in src/server too (and the other way round).
import { DurableObject } from 'cloudflare:workers';
import wasmModule from '../website/app/gbcrypto.wasm';

// --- rules (src/online/Protocol.h) ---------------------------------------------
const kMaxClockSkew = 600;
const kDailyUploadsUnverified = 5;
const kCreatorSharePercent = 70;
const KINDS = ['hat', 'shirt', 'pants', 'audio', 'plugin', 'game', 'decal'];
const FEE = { hat: 10, shirt: 10, pants: 10, audio: 20, plugin: 20, game: 0, decal: 5 };
const MAX_SIZE = { audio: 6 << 20, game: 24 << 20, plugin: 512 << 10, decal: 4 << 20 };
const maxSize = (k) => MAX_SIZE[k] || 64 * 1024;
const isClothing = (k) => k === 'hat' || k === 'shirt' || k === 'pants';
const kOnlineFor = 150;                       // ServerFriends.cpp
const kMaxFriends = 200, kMaxRequests = 100;
const kGroupFee = 50, kMaxOwned = 5, kMaxJoined = 50, kWallSize = 200, kPostCooldown = 10;
const kMaxWrongPasswords = 5, kLockoutSeconds = 600;
const kDefaultMax = 12, kMostPlayers = 30, kHostedEach = 3, kJoinWait = 15, kHostSilence = 90, kPipeSilence = 120;
const kStaffName = 'Guts';
const LOOK_ONLY = new Set(['list', 'profile', 'users.search', 'groups.list', 'groups.get', 'servers.list', 'stats', 'thumb.get']);

// --- helpers ---------------------------------------------------------------------
const now = () => Math.floor(Date.now() / 1000);
const utcDay = (t) => new Date(t * 1000).toISOString().slice(0, 10);
const lower = (s) => String(s).replace(/[A-Z]/g, (c) => c.toLowerCase());
const upper = (s) => String(s).replace(/[a-z]/g, (c) => c.toUpperCase());
const isHex = (s, min, max) => typeof s === 'string' && s.length >= min && s.length <= max && /^[0-9a-fA-F]*$/.test(s);
const fail = (error) => ({ ok: false, error });
const okay = (extra) => Object.assign({ ok: true }, extra || {});
const enc = new TextEncoder();
const hexOf = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
const randomHex = (n) => hexOf(crypto.getRandomValues(new Uint8Array(n)));
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const num = (args, k) => (typeof args[k] === 'number' && isFinite(args[k]) ? Math.trunc(args[k]) : 0);
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

// Online::cleanText: drop control characters, cap the length (in UTF-8 bytes), trim.
function cleanText(s, maxLen, allowNewlines = false) {
  let out = '', bytes = 0;
  for (const ch of String(s || '')) {
    const c = ch.codePointAt(0);
    let keep = '';
    if (ch === '\n' && allowNewlines) keep = '\n';
    else if (c >= 32 && c !== 127) keep = ch;
    if (!keep) continue;
    const b = enc.encode(keep).length;
    if (bytes + b > maxLen) break;
    out += keep; bytes += b;
    if (bytes >= maxLen) break;
  }
  return out.replace(/^[ \n]+|[ \n]+$/g, '');
}

function usernameProblem(name, official = false) {
  if (name.length < 3) return 'Usernames need at least 3 characters.';
  if (name.length > 20) return 'Usernames can be at most 20 characters.';
  if (!/^[A-Za-z0-9_]*$/.test(name)) return 'Usernames can only have letters, numbers and _.';
  if ((name.match(/_/g) || []).length > 1) return 'Usernames can have only one _.';
  if (name[0] === '_' || name[name.length - 1] === '_') return 'Usernames can\'t start or end with _.';
  if (/^[0-9]+$/.test(name)) return 'Usernames can\'t be only numbers.';
  const l = lower(name.replace(/_/g, ''));
  if (l === 'guts') return official ? '' : 'That username belongs to Guts&Bolts staff.';
  if (['admin', 'administrator', 'staff', 'moderator', 'mod', 'gutsandbolts', 'gutsbolts', 'official', 'system',
    'server', 'roblox', 'support', 'help'].includes(l)) return 'That username is reserved.';
  return '';
}
const nameIsReserved = (name) => lower(name).replace(/[ _.]/g, '') === lower(kStaffName);

function makeCode() {
  const letters = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  return Array.from(crypto.getRandomValues(new Uint8Array(6)), (b) => letters[b % 32]).join('');
}

// --- crypto (the same Monocypher as the apps, as WebAssembly) ----------------------
let wasm = null;
function crypt() {
  if (!wasm) wasm = new WebAssembly.Instance(wasmModule, {}).exports;
  return wasm;
}
const unhex = (s) => {
  const out = new Uint8Array(s.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(s.substr(i * 2, 2), 16);
  return out;
};
function verify(idHex, message, sigHex) {
  if (!isHex(idHex, 64, 64) || !isHex(sigHex, 128, 128)) return false;
  const w = crypt(), msg = enc.encode(message);
  const m = new Uint8Array(w.memory.buffer, w.buf(), w.bufSize());
  if (msg.length > m.length - 128) return false;
  m.set(unhex(idHex), 0); m.set(unhex(sigHex), 32); m.set(msg, 128);
  return w.check(msg.length) === 0;
}
function hashHex(text) {
  const w = crypt(), data = enc.encode(text);
  const m = new Uint8Array(w.memory.buffer, w.buf(), w.bufSize());
  m.set(data, 128);
  w.hash(data.length);
  return hexOf(new Uint8Array(w.memory.buffer, w.buf() + 64, 32));
}

// nlohmann::json::dump(): keys sorted, no spaces (what signatures are made over).
function canon(v) {
  if (Array.isArray(v)) return '[' + v.map(canon).join(',') + ']';
  if (v && typeof v === 'object') return '{' + Object.keys(v).sort().map((k) => JSON.stringify(k) + ':' + canon(v[k])).join(',') + '}';
  if (typeof v === 'number') return Number.isInteger(v) ? String(v) : JSON.stringify(v);
  return JSON.stringify(v === undefined ? null : v);
}
const requestText = (op, account, time, nonce, args) => 'gb-req:' + op + '\n' + account + '\n' + time + '\n' + nonce + '\n' + canon(args);
const grantMessage = (key, accountId) => 'gb-badge:' + key + ':' + accountId;

function grantValid(official, key, accountId, sig) {
  if (!official || typeof sig !== 'string') return false;
  if (!sig.startsWith('s:')) return verify(official, grantMessage(key, accountId), sig);
  if (key !== 'verified') return false;   // the only badge Staff members can give
  const a = sig.indexOf(':', 2), b = a < 0 ? -1 : sig.indexOf(':', a + 1);
  if (b < 0) return false;
  const staffId = sig.slice(2, a), staffSig = sig.slice(a + 1, b), grantSig = sig.slice(b + 1);
  return verify(official, grantMessage('staff', staffId), staffSig) && verify(staffId, grantMessage(key, accountId), grantSig);
}

// base64 <-> bytes (uploads)
function b64ToBytes(s) {
  const bin = atob(String(s).replace(/[\r\n ]/g, ''));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
function bytesToB64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}
const CHUNK = 1 << 20;   // files are stored in 1 MB pieces (SQLite rows are at most 2 MB)

// --- the server -------------------------------------------------------------------
export class GbServerObject extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.env = env;
    this.sql = ctx.storage.sql;
    this.sql.exec(`CREATE TABLE IF NOT EXISTS users (id TEXT PRIMARY KEY, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS assets (id TEXT PRIMARY KEY, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS groups (id TEXT PRIMARY KEY, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS files (id TEXT NOT NULL, part INTEGER NOT NULL, data BLOB NOT NULL, PRIMARY KEY (id, part));
      CREATE TABLE IF NOT EXISTS nonces (key TEXT PRIMARY KEY, time INTEGER NOT NULL);`);
    this.official = lower(env.OFFICIAL || '');
    this.name = env.SERVER_NAME || 'Guts&Bolts';
    this.users = new Map(); this.assets = new Map(); this.groups = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM users')) this.users.set(r.id, JSON.parse(r.data));
    for (const r of this.sql.exec('SELECT id, data FROM assets')) this.assets.set(r.id, JSON.parse(r.data));
    for (const r of this.sql.exec('SELECT id, data FROM groups')) this.groups.set(r.id, JSON.parse(r.data));
    const ids = this.getMeta('ids', { next: 2, taken: [] });
    this.nextUserId = Math.max(2, ids.next || 2);
    this.takenNames = new Set(ids.taken || []);
    for (const u of this.users.values()) {
      this.claimOfficial(u);
      if (u.userId > 0) { this.nextUserId = Math.max(this.nextUserId, u.userId + 1); this.takenNames.add(lower(u.username)); }
    }
    this.takenNames.add('guts');
    this.failedLogins = new Map();
    this.lastPost = new Map();
    this.dirty = { users: new Set(), assets: new Set(), groups: new Set(), ids: false };
    // The relay (in memory: a restart closes every game, like the C++ server).
    this.conns = new Map();      // conn id -> { ws, mode, account, session, ticket, peer, since, lastActive, closing }
    this.sessions = new Map();   // session id -> { id, game, title, host, code, priv, max, created, control, players:Set }
    this.sweepTimer = null;
  }

  getMeta(key, fallback) {
    for (const r of this.sql.exec('SELECT value FROM meta WHERE key = ?', key)) return JSON.parse(r.value);
    return fallback;
  }

  // Save what a request changed (called once at the end of each request).
  flush() {
    for (const id of this.dirty.users) {
      const u = this.users.get(id);
      if (u) this.sql.exec('INSERT OR REPLACE INTO users (id, data) VALUES (?, ?)', id, JSON.stringify(u));
    }
    for (const id of this.dirty.assets) {
      const a = this.assets.get(id);
      if (a) this.sql.exec('INSERT OR REPLACE INTO assets (id, data) VALUES (?, ?)', id, JSON.stringify(a));
      else this.sql.exec('DELETE FROM assets WHERE id = ?', id);
    }
    for (const id of this.dirty.groups) {
      const g = this.groups.get(id);
      if (g) this.sql.exec('INSERT OR REPLACE INTO groups (id, data) VALUES (?, ?)', id, JSON.stringify(g));
      else this.sql.exec('DELETE FROM groups WHERE id = ?', id);
    }
    if (this.dirty.ids) {
      this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'ids',
        JSON.stringify({ next: this.nextUserId, taken: [...this.takenNames] }));
    }
    this.dirty = { users: new Set(), assets: new Set(), groups: new Set(), ids: false };
  }
  saveUser(...us) { for (const u of us) if (u) this.dirty.users.add(u.id); }
  saveAsset(a) { this.dirty.assets.add(a.id); }
  saveGroup(g) { this.dirty.groups.add(g.id); }

  writeFile(id, bytes) {
    this.sql.exec('DELETE FROM files WHERE id = ?', id);
    for (let i = 0, part = 0; i < bytes.length || part === 0; i += CHUNK, part++)
      this.sql.exec('INSERT INTO files (id, part, data) VALUES (?, ?, ?)', id, part, bytes.subarray(i, i + CHUNK));
    return true;
  }
  readFile(id) {
    const parts = [...this.sql.exec('SELECT data FROM files WHERE id = ? ORDER BY part', id)].map((r) => new Uint8Array(r.data));
    if (!parts.length) return null;
    const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
    let at = 0;
    for (const p of parts) { out.set(p, at); at += p.length; }
    return out;
  }

  // --- accounts ---
  isOfficial(u) { return !!this.official && u.id === this.official; }
  isStaff(u) { return this.isOfficial(u) || grantValid(this.official, 'staff', u.id, (u.grants || {}).staff); }
  isVerified(u) { return this.isOfficial(u) || grantValid(this.official, 'verified', u.id, (u.grants || {}).verified); }
  claimOfficial(u) {
    if (!this.isOfficial(u) || u.userId === 1) return;
    u.userId = 1; u.username = kStaffName;
    this.takenNames.add(lower(kStaffName));
    this.saveUser(u);
  }
  user(id) {
    let u = this.users.get(id);
    if (u) return u;
    u = { id, name: 'Player', created: now(), lastSeen: 0, ledger: [], grants: {}, owned: [], uploadDay: '', uploadsToday: 0,
      playDay: '', playEarned: 0, lastPlay: 0, banned: false, friends: [], friendIn: [], friendOut: [],
      username: '', userId: 0, pwSalt: '', pwHash: '', keyBlob: '' };
    this.users.set(id, u);
    this.claimOfficial(u);
    this.saveUser(u);
    return u;
  }
  findUser(id) { return this.users.get(lower(id || '')) || null; }
  findUsername(name) {
    const want = lower(name);
    for (const u of this.users.values()) if (u.userId > 0 && lower(u.username) === want) return u;
    return null;
  }
  findUserId(n) { if (n <= 0) return null; for (const u of this.users.values()) if (u.userId === n) return u; return null; }
  balance(u) { return u.ledger.reduce((s, e) => s + e[0], 0); }
  hasRef(u, ref) { return u.ledger.some((e) => e[3] === ref); }
  add(u, amount, reason, ref) {
    u.ledger.push([amount, reason, now(), ref]);
    if (u.ledger.length > 2000) {
      const cut = u.ledger.length - 1500;
      const old = u.ledger.slice(0, cut).reduce((s, e) => s + e[0], 0);
      u.ledger = [[old, 'Earlier history', 0, 'carry'], ...u.ledger.slice(cut)];
    }
    this.saveUser(u);
  }
  isOnline(u) { return now() - u.lastSeen <= kOnlineFor || !!this.sessionOf(u.id); }
  badgesOf(u) {
    const b = [];
    if (this.isOfficial(u)) b.push('admin');
    for (const [key, sig] of Object.entries(u.grants || {})) if (grantValid(this.official, key, u.id, sig)) b.push(key);
    return b;
  }

  publicUser(u) {
    return { id: u.id, name: u.name, username: u.username, userId: u.userId, verified: this.isVerified(u),
      staff: this.isStaff(u), official: this.isOfficial(u), created: u.created, banned: u.banned };
  }
  meJson(u) {
    const today = utcDay(now());
    return Object.assign(this.publicUser(u), {
      bolts: this.balance(u), hasPassword: !!u.keyBlob, grants: Object.entries(u.grants || {}),
      canDaily: !this.hasRef(u, 'daily:' + today),
      playEarnedToday: u.playDay === today ? u.playEarned : 0,
      uploadsLeft: this.isVerified(u) ? -1 : kDailyUploadsUnverified - (u.uploadDay === today ? u.uploadsToday : 0),
      owned: [...u.owned].sort(),
      avatar: u.avatar || null,
    });
  }
  publicAsset(a) {
    const c = this.users.get(a.creator);
    return { id: a.id, kind: a.kind, name: a.name, description: a.description, creator: a.creator, price: a.price,
      created: a.created, sales: a.sales, plays: a.plays, size: a.size, meta: a.meta || {},
      creatorName: c ? c.name : '?', creatorVerified: !!c && this.isVerified(c), thumb: a.thumb || 0 };
  }
  publicGroup(g) {
    const o = this.users.get(g.owner);
    return { id: g.id, name: g.name, description: g.description, owner: g.owner, ownerName: o ? o.name : '?',
      ownerVerified: !!o && this.isVerified(o), members: Object.keys(g.members).length, color: g.color, open: g.open,
      created: g.created, shout: (g.shout && g.shout.text) || '' };
  }
  groupsOf(id) { return [...this.groups.values()].filter((g) => g.members[id]); }

  // --- checking a request (GbServer::checkRequest) ---
  checkRequest(req) {
    const opName = typeof req.op === 'string' ? req.op : '';
    const account = lower(typeof req.account === 'string' ? req.account : '');
    const nonce = typeof req.nonce === 'string' ? req.nonce : '';
    const time = typeof req.time === 'number' ? req.time : 0;
    const args = req.args && typeof req.args === 'object' && !Array.isArray(req.args) ? req.args : {};
    if (!opName || opName.length > 40) return { bad: fail('Unknown request.') };
    if (!isHex(account, 64, 64)) return { bad: fail('Missing account.') };
    if (!isHex(nonce, 8, 64)) return { bad: fail('Missing nonce.') };
    const t = now();
    if (Math.abs(t - time) > kMaxClockSkew)
      return { bad: fail('Your computer\'s clock is too far off from the server\'s. Fix the date and time and try again.') };
    if (!verify(account, requestText(opName, account, time, nonce, args), typeof req.sig === 'string' ? req.sig : ''))
      return { bad: fail('That request wasn\'t signed by its account.') };
    const key = account + nonce;
    try {
      this.sql.exec('INSERT INTO nonces (key, time) VALUES (?, ?)', key, t);
    } catch {
      return { bad: fail('That request was already sent once.') };
    }
    if (Math.random() < 0.01) this.sql.exec('DELETE FROM nonces WHERE time < ?', t - 2 * kMaxClockSkew);
    const me = this.user(account);
    me.lastSeen = t;
    this.saveUser(me);
    if (me.banned && opName !== 'hello') return { bad: fail('This account has been banned from this server.') };
    if (me.userId === 0 && opName !== 'hello' && opName !== 'ping' && !opName.startsWith('account.') && !LOOK_ONLY.has(opName))
      return { bad: fail('Sign up or log in first.') };
    return { me, args, opName };
  }

  handle(req) {
    try {
      const c = this.checkRequest(req);
      if (c.bad) return c.bad;
      return this.op(c.opName, c.me, c.args);
    } catch (e) {
      return fail('The server hit a problem: ' + (e && e.message || e));
    } finally {
      this.flush();
    }
  }

  // --- the requests (GbServer::op) ---
  op(name, me, args) {
    const t = now(), today = utcDay(t);

    if (name === 'hello') {
      let n = cleanText(str(args, 'name'), 20);
      if (me.userId > 0) n = me.username;
      if (n) {
        if (nameIsReserved(n) && !this.isOfficial(me)) n = 'Player';
        me.name = n;
      }
      if (Array.isArray(args.grants))
        for (const g of args.grants)
          if (Array.isArray(g) && g.length === 2 && typeof g[0] === 'string' && typeof g[1] === 'string' &&
              grantValid(this.official, g[0], me.id, g[1])) me.grants[g[0]] = g[1];
      this.saveUser(me);
      return okay({ me: this.meJson(me), server: { name: this.name, protocol: 1, official: this.official } });
    }
    if (name === 'profile') {
      const want = str(args, 'id');
      const u = want && want.length < 12 && /^[0-9]+$/.test(want) ? this.findUserId(Number(want)) : this.findUser(want);
      if (!u || u.userId === 0) return fail('There\'s no account with that ID on this server.');
      const user = Object.assign(this.publicUser(u), { badges: this.badgesOf(u), avatar: u.avatar || null });
      const creations = [...this.assets.values()].filter((a) => a.creator === u.id).map((a) => this.publicAsset(a));
      const groups = this.groupsOf(u.id).map((g) => Object.assign(this.publicGroup(g), { role: g.members[u.id] }));
      const friendship = u.id === me.id ? 'self' : me.friends.includes(u.id) ? 'friends'
        : me.friendOut.includes(u.id) ? 'sent' : me.friendIn.includes(u.id) ? 'received' : 'none';
      // Like a Roblox profile: what they're wearing, some friends, visits to their games.
      const wornIds = u.avatar && Array.isArray(u.avatar.wearing) ? u.avatar.wearing : [];
      const wearing = wornIds.map((id) => this.assets.get(id)).filter(Boolean).map((a) => this.publicAsset(a));
      const friends = u.friends.slice(0, 9).map((id) => this.users.get(id)).filter(Boolean)
        .map((f) => Object.assign(this.publicUser(f), { avatar: f.avatar || null, online: this.isOnline(f) }));
      const placeVisits = creations.filter((a) => a.kind === 'game').reduce((n, a) => n + (a.plays || 0), 0);
      return okay({ user, creations, groups, friendCount: u.friends.length, friendship, wearing, friends,
        online: this.isOnline(u), placeVisits });
    }
    if (name === 'users.search') {
      let q = lower(cleanText(str(args, 'query'), 64));
      if (q[0] === '#') q = q.slice(1);
      if (q[0] === '@') q = q.slice(1);
      const wantId = q && q.length < 12 && /^[0-9]+$/.test(q) ? Number(q) : -1;
      const found = [...this.users.values()].filter((u) => !u.banned && u.userId > 0 &&
        (!q || u.userId === wantId || lower(u.name).includes(q) || lower(u.username).includes(q)));
      found.sort((a, b) => {
        const ea = lower(a.username) === q || a.userId === wantId, eb = lower(b.username) === q || b.userId === wantId;
        if (ea !== eb) return ea ? -1 : 1;
        const va = this.isVerified(a), vb = this.isVerified(b);
        if (va !== vb) return va ? -1 : 1;
        return b.lastSeen - a.lastSeen;
      });
      return okay({ users: found.slice(0, 50).map((u) => this.publicUser(u)) });
    }
    if (name.startsWith('account.')) return this.accountOp(name, me, args);
    if (name.startsWith('groups.')) return this.groupOp(name, me, args);
    if (name.startsWith('friends.')) return this.friendOp(name, me, args);
    if (name.startsWith('servers.')) return this.serverOp(name, me, args);
    if (name === 'ping') return okay();

    if (name === 'avatar.set') {
      // Your look, shared by the website and the apps. Colours are 0-255 whole numbers.
      const a = args.avatar && typeof args.avatar === 'object' ? args.avatar : null;
      if (!a) return fail('That avatar looks wrong.');
      const rgb = (v, allowNone) => {
        if (!Array.isArray(v) || v.length !== 3 || !v.every((x) => Number.isInteger(x))) return null;
        if (allowNone && v.every((x) => x < 0)) return [-1, -1, -1];
        return v.map((x) => clamp(x, 0, 255));
      };
      const colors = {};
      for (const part of ['head', 'torso', 'leftArm', 'rightArm', 'leftLeg', 'rightLeg']) {
        colors[part] = rgb(a[part]);
        if (!colors[part]) return fail('That avatar looks wrong.');
      }
      const hat = Number.isInteger(a.hat) ? clamp(a.hat, 0, 3) : 0;
      const hatColor = rgb(a.hatColor, true) || [-1, -1, -1];
      const wearing = Array.isArray(a.wearing) ? a.wearing.filter((w) => typeof w === 'string' && me.owned.includes(w)).slice(0, 8) : [];
      me.avatar = Object.assign(colors, { hat, hatColor, wearing, updated: t });
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'thumb.set') {
      // A picture of a game (Studio sends one when publishing): a PNG or JPG, up to 400 KB.
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('You can only change pictures of your own things.');
      let data;
      try { data = b64ToBytes(str(args, 'data')); } catch { return fail('The picture got scrambled. Try again.'); }
      const png = data.length > 8 && [0x89, 0x50, 0x4e, 0x47].every((v, i) => data[i] === v);
      const jpg = data.length > 3 && data[0] === 0xff && data[1] === 0xd8;
      if (!png && !jpg) return fail('Pictures must be .png or .jpg.');
      if (data.length > 400 * 1024) return fail('That picture is too big (the most is 400 KB).');
      this.writeFile('thumb:' + a.id, data);
      a.thumb = t;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a) });
    }
    if (name === 'thumb.get') {
      const a = this.assets.get(str(args, 'id'));
      const data = a && a.thumb ? this.readFile('thumb:' + a.id) : null;
      return data ? okay({ data: bytesToB64(data) }) : fail('No picture.');
    }
    if (name === 'bolts.history') {
      const h = me.ledger.slice(-100).map((e) => ({ amount: e[0], reason: e[1], time: e[2] }));
      return okay({ history: h, me: this.meJson(me) });
    }
    if (name === 'bolts.daily') {
      if (this.hasRef(me, 'daily:' + today)) return fail('You already got today\'s Bolts. Come back tomorrow!');
      this.add(me, 25, 'Daily reward', 'daily:' + today);
      return okay({ me: this.meJson(me), got: 25 });
    }
    if (name === 'bolts.play') {
      if (t - me.lastPlay < 280) return fail('Keep playing!');
      if (me.playDay !== today) { me.playDay = today; me.playEarned = 0; }
      me.lastPlay = t;
      this.saveUser(me);
      if (me.playEarned >= 50) return fail('That\'s all the Bolts from playing for today.');
      me.playEarned += 5;
      this.add(me, 5, 'Playing games', 'play:' + today);
      return okay({ me: this.meJson(me), got: 5 });
    }
    if (name === 'bolts.redeem') {
      const code = str(args, 'code').replace(/\s/g, '');
      const a = code.indexOf('-'), b = a < 0 ? -1 : code.indexOf('-', a + 1), c = b < 0 ? -1 : code.indexOf('-', b + 1);
      if (!code.startsWith('BOLTS-') || c < 0) return fail('That doesn\'t look like a Bolts code.');
      const amount = parseInt(code.slice(a + 1, b), 10) || 0;
      const cn = code.slice(b + 1, c);
      if (amount <= 0 || !verify(this.official, 'gb-bolts:' + amount + ':' + me.id + ':' + cn, code.slice(c + 1)))
        return fail('That code isn\'t valid for your account.');
      if (this.hasRef(me, 'code:' + cn)) return fail('You already used that code.');
      this.add(me, amount, 'Bolts from Guts&Bolts staff', 'code:' + cn);
      return okay({ me: this.meJson(me), got: amount });
    }

    if (name.startsWith('admin.')) {
      if (!this.isStaff(me)) return fail('Only staff can do that.');
      let to = this.findUser(str(args, 'to'));
      if (!to && name !== 'admin.find' && isHex(lower(str(args, 'to')), 64, 64)) to = this.user(lower(str(args, 'to')));
      if (name === 'admin.find') {
        const q = lower(cleanText(str(args, 'query'), 64));
        const list = [];
        for (const u of this.users.values()) {
          if (list.length >= 40) break;
          if (!q || lower(u.name).includes(q) || u.id.startsWith(q)) list.push(this.publicUser(u));
        }
        return okay({ users: list });
      }
      if (!to) return fail('There\'s no account with that ID on this server.');
      if (name === 'admin.grant') {
        const key = str(args, 'key'), sig = str(args, 'sig');
        if (!grantValid(this.official, key, to.id, sig)) return fail('That badge signature isn\'t valid.');
        to.grants[key] = sig;
        this.saveUser(to);
        return okay({ user: this.publicUser(to) });
      }
      if (name === 'admin.revoke') {
        const key = str(args, 'key');
        if (key !== 'verified' && !this.isOfficial(me)) return fail('Only the official account can take that badge away.');
        delete to.grants[key];
        this.saveUser(to);
        return okay({ user: this.publicUser(to) });
      }
      if (!this.isOfficial(me)) return fail('Only the official Guts account can do that.');
      if (name === 'admin.giveBolts') {
        const amount = num(args, 'amount');
        if (amount === 0 || Math.abs(amount) > 10000000) return fail('Pick an amount between 1 and 10,000,000.');
        const why = cleanText(str(args, 'reason'), 80);
        this.add(to, amount, why || (amount > 0 ? 'Bolts from Guts&Bolts staff' : 'Taken by staff'), 'gift:' + randomHex(6));
        return okay({ user: this.publicUser(to), bolts: this.balance(to) });
      }
      if (name === 'admin.ban') {
        if (this.isOfficial(to)) return fail('You can\'t ban yourself.');
        to.banned = args.on === undefined ? true : !!args.on;
        this.saveUser(to);
        return okay({ user: this.publicUser(to) });
      }
      return fail('Unknown staff request.');
    }

    if (name === 'upload') {
      const kind = str(args, 'kind');
      if (!KINDS.includes(kind)) return fail('You can\'t upload that kind of thing.');
      const title = cleanText(str(args, 'name'), 50);
      if (!title) return fail('Give it a name.');
      const desc = cleanText(str(args, 'description'), 1000, true);
      const verified = this.isVerified(me);
      let price = clamp(num(args, 'price'), 0, 1000000);
      if (kind === 'game') price = 0;
      if (price > 0 && !verified) return fail('Only Verified creators can sell things. Upload it for free, or get Verified!');
      if (!verified) {
        if (me.uploadDay !== today) { me.uploadDay = today; me.uploadsToday = 0; }
        if (me.uploadsToday >= kDailyUploadsUnverified)
          return fail('You\'ve uploaded ' + kDailyUploadsUnverified + ' things today. Come back tomorrow (Verified creators have no limit).');
      }
      let data;
      try { data = b64ToBytes(str(args, 'data')); } catch { return fail('The upload got scrambled. Try again.'); }
      if (data.length > maxSize(kind)) return fail('That\'s too big (the most is ' + Math.floor(maxSize(kind) / 1024) + ' KB).');
      const meta = args.meta && typeof args.meta === 'object' && !Array.isArray(args.meta) ? args.meta : {};
      if (JSON.stringify(meta).length > 4096) return fail('Too much extra information.');
      if (kind === 'audio') {
        const ext = lower(typeof meta.ext === 'string' ? meta.ext : '');
        if (!['mp3', 'wav', 'ogg', 'flac'].includes(ext)) return fail('Audio must be .mp3, .wav, .ogg or .flac.');
        if (!data.length) return fail('That audio file is empty.');
      }
      if (kind === 'decal') {
        const png = data.length > 8 && [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a].every((v, i) => data[i] === v);
        const jpg = data.length > 3 && data[0] === 0xff && data[1] === 0xd8 && data[2] === 0xff;
        if (!png && !jpg) return fail('Decals must be .png or .jpg pictures.');
        meta.ext = png ? 'png' : 'jpg';
      }
      if (kind === 'game' && !isJson(data)) return fail('That isn\'t a Guts&Bolts game file.');
      if (kind === 'plugin' && !data.length) return fail('That plugin is empty.');
      const fee = verified ? 0 : FEE[kind];
      if (fee > 0 && this.balance(me) < fee)
        return fail('Uploading costs ' + fee + ' Bolts, and you have ' + this.balance(me) + '. (It\'s free for Verified creators.)');
      const a = { id: kind + '-' + randomHex(5), kind, name: title, description: desc, creator: me.id, price, created: t,
        sales: 0, plays: 0, size: data.length, meta };
      this.writeFile(a.id, data);
      this.assets.set(a.id, a);
      this.saveAsset(a);
      if (!me.owned.includes(a.id)) me.owned.push(a.id);
      if (!verified) me.uploadsToday++;
      if (fee > 0) this.add(me, -fee, 'Upload fee: ' + title, 'upload:' + a.id);
      this.saveUser(me);
      return okay({ asset: this.publicAsset(a), me: this.meJson(me), fee });
    }
    if (name === 'update') {
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      if (a.creator !== me.id) return fail('You can only update your own things.');
      let data;
      try { data = b64ToBytes(str(args, 'data')); } catch { return fail('The upload got scrambled. Try again.'); }
      if (data.length > maxSize(a.kind)) return fail('That\'s too big.');
      if (a.kind === 'game' && !isJson(data)) return fail('That isn\'t a Guts&Bolts game file.');
      if (!isClothing(a.kind)) this.writeFile(a.id, data);
      const title = cleanText(str(args, 'name'), 50);
      if (title) a.name = title;
      if ('description' in args) a.description = cleanText(str(args, 'description'), 1000, true);
      if ('price' in args && a.kind !== 'game') {
        const price = clamp(num(args, 'price'), 0, 1000000);
        if (price > 0 && !this.isVerified(me)) return fail('Only Verified creators can sell things.');
        a.price = price;
      }
      if (!isClothing(a.kind)) a.size = data.length;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a) });
    }
    if (name === 'list') {
      const kind = str(args, 'kind'), q = lower(cleanText(str(args, 'query'), 64)), creator = lower(str(args, 'creator'));
      const sort = str(args, 'sort');
      const found = [...this.assets.values()].filter((a) =>
        (!kind || a.kind === kind || (kind === 'clothing' && isClothing(a.kind))) &&
        (!creator || a.creator === creator) && (!q || lower(a.name).includes(q)));
      found.sort((x, y) => (sort === 'popular' ? (y.plays + y.sales) - (x.plays + x.sales) : y.created - x.created));
      const offset = Math.max(0, num(args, 'offset'));
      const limit = 'limit' in args ? clamp(num(args, 'limit'), 1, 100) : 60;
      return okay({ assets: found.slice(offset, offset + limit).map((a) => this.publicAsset(a)), total: found.length });
    }
    if (name === 'get') {
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      const mine = me.owned.includes(a.id) || a.creator === me.id;
      if (a.price > 0 && !mine && !this.isStaff(me) && (a.kind === 'plugin' || a.kind === 'audio')) return fail('Buy it first.');
      const data = this.readFile(a.id);
      if (!data) return fail('The server lost that file.');
      if (a.kind === 'game' && a.creator !== me.id) { a.plays++; this.saveAsset(a); }
      return okay({ asset: this.publicAsset(a), data: bytesToB64(data) });
    }
    if (name === 'buy') {
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      if (me.owned.includes(a.id)) return okay({ me: this.meJson(me), already: true });
      if (a.price > 0) {
        if (this.balance(me) < a.price) return fail('You need ' + (a.price - this.balance(me)) + ' more Bolts for that.');
        this.add(me, -a.price, 'Bought ' + a.name, 'buy:' + a.id);
        const seller = this.findUser(a.creator);
        if (seller && seller !== me) {
          const share = Math.floor(a.price * kCreatorSharePercent / 100);
          if (share > 0) this.add(seller, share, 'Sold ' + a.name, 'sale:' + a.id + ':' + randomHex(4));
        }
      }
      a.sales++;
      me.owned.push(a.id);
      this.saveAsset(a); this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'delete') {
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('You can only delete your own things.');
      this.sql.exec('DELETE FROM files WHERE id = ? OR id = ?', a.id, 'thumb:' + a.id);
      this.assets.delete(a.id);
      this.dirty.assets.add(a.id);
      return okay();
    }
    if (name === 'stats') return okay({ users: this.users.size, assets: this.assets.size, name: this.name });
    return fail('The server doesn\'t know how to do "' + name + '". It might need updating.');
  }

  accountOp(name, me, args) {
    const username = cleanText(str(args, 'username'), 30);
    if (name === 'account.check') {
      let problem = usernameProblem(username, this.isOfficial(me));
      if (!problem && this.takenNames.has(lower(username)) && !(this.isOfficial(me) && lower(username) === 'guts'))
        problem = 'That username is taken.';
      return okay({ available: !problem, problem });
    }
    if (name === 'account.signup') {
      const salt = lower(str(args, 'salt')), auth = lower(str(args, 'auth')), blob = lower(str(args, 'key'));
      if (!isHex(salt, 32, 32) || !isHex(auth, 64, 64) || !isHex(blob, 208, 208))
        return fail('The sign-up form was missing something. Update the app and try again.');
      const official = this.isOfficial(me);
      if (me.userId > 0 && !(official && !me.keyBlob)) return fail('This device is already signed up as ' + me.username + '.');
      if (official) {
        if (lower(username) !== 'guts') return fail('The staff account\'s username is Guts.');
        me.name = me.username;
      } else {
        const problem = usernameProblem(username);
        if (problem) return fail(problem);
        if (this.takenNames.has(lower(username))) return fail('That username is taken. Try another one.');
        me.userId = this.nextUserId++;
        me.username = username;
        this.takenNames.add(lower(username));
        me.name = username;
      }
      me.pwSalt = salt; me.pwHash = hashHex(auth); me.keyBlob = blob;
      this.dirty.ids = true;
      if (!this.hasRef(me, 'welcome')) this.add(me, 100, 'Welcome to Guts&Bolts!', 'welcome');
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    const u = this.findUsername(username);
    if (name === 'account.salt') {
      if (!u || !u.keyBlob) return fail('There\'s no account with that username.');
      return okay({ salt: u.pwSalt });
    }
    if (name === 'account.login') {
      if (!u || !u.keyBlob) return fail('There\'s no account with that username.');
      if (u.banned) return fail('That account has been banned from this server.');
      const t = now(), key = lower(username);
      const fails = (this.failedLogins.get(key) || []).filter((x) => t - x <= kLockoutSeconds);
      this.failedLogins.set(key, fails);
      if (fails.length >= kMaxWrongPasswords) return fail('Too many wrong passwords. Wait 10 minutes and try again.');
      const auth = lower(str(args, 'auth'));
      if (!isHex(auth, 64, 64) || hashHex(auth) !== u.pwHash) { fails.push(t); return fail('Wrong password.'); }
      this.failedLogins.delete(key);
      return okay({ account: u.id, key: u.keyBlob, username: u.username, name: u.name });
    }
    return fail('Unknown request.');
  }

  friendOp(name, me, args) {
    const person = (u) => ({ id: u.id, name: u.name, verified: this.isVerified(u) });
    if (name === 'friends.list') {
      const friends = [];
      for (const id of me.friends) {
        const u = this.findUser(id);
        if (!u) continue;
        const f = Object.assign(person(u), { online: this.isOnline(u) });
        const s = this.sessionOf(u.id);
        if (s) f.playing = { session: s.id, title: s.title, private: s.priv, full: s.players.size + 1 >= s.max };
        friends.push(f);
      }
      return okay({ friends, incoming: me.friendIn.map((id) => this.findUser(id)).filter(Boolean).map(person),
        outgoing: me.friendOut.map((id) => this.findUser(id)).filter(Boolean).map(person) });
    }
    const them = this.findUser(str(args, 'user'));
    if (!them) return fail('There\'s no account with that ID on this server.');
    if (them.id === me.id) return fail('You can\'t be friends with yourself (but we like you).');
    const drop = (list, id) => { const i = list.indexOf(id); if (i >= 0) list.splice(i, 1); };
    const addTo = (list, id) => { if (!list.includes(id)) list.push(id); };
    const clearRequests = () => { drop(me.friendIn, them.id); drop(me.friendOut, them.id); drop(them.friendIn, me.id); drop(them.friendOut, me.id); };
    const becomeFriends = () => {
      if (me.friends.length >= kMaxFriends || them.friends.length >= kMaxFriends)
        return fail('One of you already has ' + kMaxFriends + ' friends.');
      clearRequests();
      addTo(me.friends, them.id); addTo(them.friends, me.id);
      this.saveUser(me, them);
      return okay({ status: 'friends' });
    };
    if (name === 'friends.add') {
      if (me.friends.includes(them.id)) return okay({ status: 'friends' });
      if (me.friendIn.includes(them.id)) return becomeFriends();
      if (them.banned) return fail('You can\'t add that account.');
      if (me.friendOut.length >= kMaxRequests) return fail('You have too many friend requests waiting. Cancel some first.');
      if (them.friendIn.length >= kMaxRequests) return fail(them.name + ' has too many friend requests waiting.');
      addTo(me.friendOut, them.id); addTo(them.friendIn, me.id);
      this.saveUser(me, them);
      return okay({ status: 'sent' });
    }
    if (name === 'friends.accept') {
      if (!me.friendIn.includes(them.id)) return fail('That friend request isn\'t there any more.');
      return becomeFriends();
    }
    if (name === 'friends.decline' || name === 'friends.cancel' || name === 'friends.remove') {
      clearRequests();
      if (name === 'friends.remove') { drop(me.friends, them.id); drop(them.friends, me.id); }
      this.saveUser(me, them);
      return okay({ status: 'none' });
    }
    return fail('Unknown request.');
  }

  groupOp(name, me, args) {
    const t = now();
    const userJson = (id) => { const u = this.users.get(id); return { id, name: u ? u.name : '?', verified: !!u && this.isVerified(u) }; };
    if (name === 'groups.list') {
      const q = lower(cleanText(str(args, 'query'), 64));
      const found = [...this.groups.values()].filter((g) => !q || lower(g.name).includes(q));
      found.sort((a, b) => {
        const ma = Object.keys(a.members).length, mb = Object.keys(b.members).length;
        return ma !== mb ? mb - ma : b.created - a.created;
      });
      return okay({ groups: found.slice(0, 60).map((g) => this.publicGroup(g)) });
    }
    if (name === 'groups.mine') {
      return okay({ groups: this.groupsOf(me.id).map((g) => Object.assign(this.publicGroup(g), { role: g.members[me.id] })) });
    }
    if (name === 'groups.create') {
      const title = cleanText(str(args, 'name'), 40);
      if (title.length < 3) return fail('Group names need at least 3 letters.');
      if (nameIsReserved(title) && !this.isOfficial(me)) return fail('That name belongs to Guts&Bolts.');
      for (const g of this.groups.values()) if (lower(g.name) === lower(title)) return fail('There\'s already a group called that.');
      const owned = [...this.groups.values()].filter((g) => g.owner === me.id).length;
      if (owned >= kMaxOwned) return fail('You already own ' + kMaxOwned + ' groups.');
      if (this.groupsOf(me.id).length >= kMaxJoined) return fail('You\'re in too many groups. Leave one first.');
      const fee = this.isVerified(me) ? 0 : kGroupFee;
      if (this.balance(me) < fee) return fail('Making a group costs ' + fee + ' Bolts (free for Verified people).');
      const g = { id: 'g-' + randomHex(5), name: title, description: cleanText(str(args, 'description'), 1000, true), owner: me.id,
        created: t, color: clamp(typeof args.color === 'number' ? Math.trunc(args.color) : 0x3a7bd5, 0, 0xffffff),
        open: args.open === undefined ? true : !!args.open, members: { [me.id]: 'Owner' }, requests: [],
        shout: { by: '', text: '', time: 0 }, wall: [] };
      this.groups.set(g.id, g);
      if (fee > 0) this.add(me, -fee, 'Made the group ' + title, 'group:' + g.id);
      this.saveGroup(g);
      return okay({ group: this.publicGroup(g), me: this.meJson(me) });
    }
    const g = this.groups.get(str(args, 'id'));
    if (!g) return fail('That group doesn\'t exist (any more).');
    const roleOf = (id) => g.members[id] || '';
    const myRole = roleOf(me.id);
    const canManage = myRole === 'Owner' || myRole === 'Admin';
    const dropReq = (id) => { const i = g.requests.indexOf(id); if (i >= 0) { g.requests.splice(i, 1); return true; } return false; };

    if (name === 'groups.get') {
      const rank = (r) => (r === 'Owner' ? 0 : r === 'Admin' ? 1 : 2);
      const members = Object.entries(g.members).sort((a, b) => rank(a[1]) - rank(b[1])).slice(0, 500)
        .map(([id, role]) => Object.assign(userJson(id), { role }));
      const shoutInfo = g.shout && g.shout.text ? Object.assign(userJson(g.shout.by), { text: g.shout.text, time: g.shout.time }) : {};
      const wall = g.wall.slice(-60).map((p) => Object.assign(userJson(p.by), { text: p.text, time: p.time }));
      const r = okay({ group: this.publicGroup(g), myRole, requested: g.requests.includes(me.id), memberList: members, shoutInfo, wall });
      if (canManage) r.requests = g.requests.map(userJson);
      return r;
    }
    if (name === 'groups.join') {
      if (myRole) return okay();
      if (this.groupsOf(me.id).length >= kMaxJoined) return fail('You\'re in too many groups. Leave one first.');
      if (!g.open) {
        if (!g.requests.includes(me.id)) g.requests.push(me.id);
        this.saveGroup(g);
        return okay({ requested: true });
      }
      g.members[me.id] = 'Member';
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.leave') {
      if (myRole === 'Owner') return fail('Owners can\'t leave. Give the group to someone else first, or delete it.');
      delete g.members[me.id];
      dropReq(me.id);
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.post') {
      if (!myRole) return fail('Join the group to post on its wall.');
      const text = cleanText(str(args, 'text'), 300, true);
      if (!text) return fail('Write something first.');
      if (t - (this.lastPost.get(me.id) || 0) < kPostCooldown) return fail('Slow down a little - wait a few seconds between posts.');
      this.lastPost.set(me.id, t);
      g.wall.push({ by: me.id, text, time: t });
      if (g.wall.length > kWallSize) g.wall.shift();
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.deletePost') {
      const time = num(args, 'time'), by = str(args, 'by');
      const i = g.wall.findIndex((p) => p.time === time && p.by === by);
      if (i < 0) return fail('That post is already gone.');
      if (g.wall[i].by !== me.id && !canManage && !this.isStaff(me)) return fail('You can only delete your own posts.');
      g.wall.splice(i, 1);
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.shout') {
      if (!canManage) return fail('Only the group\'s owner and admins can shout.');
      g.shout = { by: me.id, text: cleanText(str(args, 'text'), 200), time: t };
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.edit') {
      if (!canManage) return fail('Only the group\'s owner and admins can change it.');
      if ('description' in args) g.description = cleanText(str(args, 'description'), 1000, true);
      if ('color' in args) g.color = clamp(num(args, 'color'), 0, 0xffffff);
      if ('open' in args) {
        g.open = !!args.open;
        if (g.open) { for (const id of g.requests) if (!g.members[id]) g.members[id] = 'Member'; g.requests = []; }
      }
      this.saveGroup(g);
      return okay({ group: this.publicGroup(g) });
    }
    if (name === 'groups.request') {
      if (!canManage) return fail('Only the group\'s owner and admins can let people in.');
      const who = lower(str(args, 'user'));
      if (!dropReq(who)) return fail('They\'re not waiting any more.');
      if (args.accept) g.members[who] = 'Member';
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.member') {
      const who = lower(str(args, 'user')), action = str(args, 'action');
      const theirRole = roleOf(who);
      if (!theirRole) return fail('They\'re not in this group.');
      if (who === me.id) return fail('You can\'t do that to yourself.');
      if (action === 'kick') {
        if (!(myRole === 'Owner' || (myRole === 'Admin' && theirRole === 'Member')) && !this.isStaff(me)) return fail('You can\'t remove them.');
        if (theirRole === 'Owner') return fail('The owner can\'t be removed.');
        delete g.members[who];
      } else if (action === 'admin' || action === 'member') {
        if (myRole !== 'Owner') return fail('Only the owner can change roles.');
        g.members[who] = action === 'admin' ? 'Admin' : 'Member';
      } else if (action === 'owner') {
        if (myRole !== 'Owner') return fail('Only the owner can give the group away.');
        g.members[who] = 'Owner'; g.members[me.id] = 'Admin'; g.owner = who;
      } else {
        return fail('Unknown member action.');
      }
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.delete') {
      if (myRole !== 'Owner' && !this.isStaff(me)) return fail('Only the owner can delete the group.');
      this.groups.delete(g.id);
      this.dirty.groups.add(g.id);
      return okay();
    }
    return fail('The server doesn\'t know how to do "' + name + '". It might need updating.');
  }

  // --- the relay (ServerRelay.cpp) ---
  sessionJson(s) {
    const h = this.users.get(s.host);
    return { id: s.id, game: s.game, title: s.title, players: s.players.size + 1, max: s.max, private: s.priv,
      hostName: h ? h.name : '?', hostVerified: !!h && this.isVerified(h) };
  }
  sessionOf(userId) {
    for (const s of this.sessions.values()) {
      if (s.host === userId) return s;
      for (const p of s.players) { const c = this.conns.get(p); if (c && c.account === userId) return s; }
    }
    return null;
  }
  serverOp(name, me, args) {
    const game = cleanText(typeof args.game === 'string' ? args.game : '', 80);
    if (name === 'servers.play') {
      let best = null;
      for (const s of this.sessions.values()) {
        if (s.priv || s.game !== game || s.host === me.id || s.players.size + 1 >= s.max) continue;
        if (!best || s.players.size > best.players.size) best = s;
      }
      return best ? okay({ join: best.id }) : okay({ host: true });
    }
    if (name === 'servers.list') {
      const list = [];
      for (const s of this.sessions.values()) {
        if (game && s.game !== game) continue;
        const friendHost = me.friends.includes(s.host);
        if (s.priv && !friendHost && s.host !== me.id) continue;
        let friends = friendHost ? 1 : 0;
        for (const p of s.players) { const c = this.conns.get(p); if (c && me.friends.includes(c.account)) friends++; }
        list.push(Object.assign(this.sessionJson(s), { friends }));
        if (list.length >= 100) break;
      }
      return okay({ servers: list });
    }
    return fail('Unknown request.');
  }

  sendTo(c, message) {
    try { c.ws.send(typeof message === 'string' ? message : JSON.stringify(message)); } catch { /* already gone */ }
  }

  relayRequest(c, req) {
    const reply = (r) => { r.t = 'relay'; this.sendTo(c, r); };
    const close = () => { c.closing = true; this.drop(c.id); };
    let checked;
    try { checked = this.checkRequest(req); } finally { this.flush(); }
    if (checked.bad) { reply(checked.bad); close(); return; }
    const { me, args, opName: op } = checked;
    const t = now();
    if (op === 'relay.host') {
      let hosting = 0;
      for (const s of this.sessions.values()) if (s.host === me.id) hosting++;
      if (hosting >= kHostedEach) { reply(fail('You\'re already running ' + kHostedEach + ' servers.')); close(); return; }
      const s = { id: 's-' + randomHex(6), game: cleanText(typeof args.game === 'string' ? args.game : '', 80),
        title: cleanText(typeof args.title === 'string' ? args.title : '', 60) || 'A game', host: me.id, code: '',
        priv: !!args.private, max: clamp(Number.isInteger(args.max) ? args.max : kDefaultMax, 2, kMostPlayers), created: t,
        control: c.id, players: new Set() };
      if (s.priv) {
        do s.code = makeCode(); while ([...this.sessions.values()].some((x) => x.code === s.code));
      }
      c.mode = 'host'; c.account = me.id; c.session = s.id;
      this.sessions.set(s.id, s);
      reply(okay({ session: s.id, code: s.code, private: s.priv }));
      return;
    }
    if (op === 'relay.join') {
      let s = null;
      const code = upper(cleanText(typeof args.code === 'string' ? args.code : '', 12));
      if (code) {
        for (const x of this.sessions.values()) if (x.code === code) s = x;
        if (!s) { reply(fail('There\'s no private server with that code (it may have closed).')); close(); return; }
      } else {
        s = this.sessions.get(typeof args.session === 'string' ? args.session : '') || null;
      }
      if (!s) { reply(fail('That server has closed.')); close(); return; }
      if (s.host === me.id) { reply(fail('That\'s your own server.')); close(); return; }
      if (s.priv && !code && !me.friends.includes(s.host)) {
        reply(fail('That\'s a private server. You need to be the host\'s friend, or have its code.'));
        close();
        return;
      }
      let waiting = 0;
      for (const o of this.conns.values()) if (o.mode === 'pending' && o.session === s.id) waiting++;
      if (s.players.size + 1 + waiting >= s.max) { reply(fail('That server is full.')); close(); return; }
      c.mode = 'pending'; c.account = me.id; c.session = s.id; c.ticket = randomHex(16); c.since = t;
      const host = this.conns.get(s.control);
      if (host) this.sendTo(host, { t: 'incoming', ticket: c.ticket, account: me.id, name: me.name });
      this.scheduleSweep();
      return;
    }
    reply(fail('Unknown request.'));
    close();
  }

  relayAccept(c, ticket) {
    let joiner = null;
    if (typeof ticket === 'string' && ticket.length >= 16)
      for (const o of this.conns.values()) if (o.mode === 'pending' && o.ticket === ticket) joiner = o;
    const s = joiner && this.sessions.get(joiner.session);
    if (!joiner || !s) { this.drop(c.id); return; }
    joiner.mode = c.mode = 'pipe';
    joiner.peer = c.id; joiner.ticket = '';
    c.peer = joiner.id; c.session = s.id; c.account = s.host;
    s.players.add(joiner.id);
    this.sendTo(joiner, { t: 'relay', ok: true, title: s.title });
  }

  // A connection went away (or is being closed): take its pipe partner with it,
  // and a host's control line takes its whole server (GbServer::dropClients).
  drop(id) {
    const dead = new Set([id]);
    for (let grew = true; grew;) {
      grew = false;
      for (const d of [...dead]) {
        const c = this.conns.get(d);
        if (c && c.peer && !dead.has(c.peer)) { dead.add(c.peer); grew = true; }
      }
      for (const [sid, s] of this.sessions) {
        if (!dead.has(s.control)) continue;
        for (const p of s.players) if (!dead.has(p)) { dead.add(p); grew = true; }
        this.sessions.delete(sid);
      }
    }
    for (const s of this.sessions.values()) for (const d of dead) s.players.delete(d);
    for (const d of dead) {
      const c = this.conns.get(d);
      if (!c) continue;
      this.conns.delete(d);
      try { c.ws.close(1000, 'bye'); } catch { /* already closed */ }
    }
  }

  scheduleSweep() {
    if (this.sweepTimer || !this.conns.size) return;
    this.sweepTimer = setTimeout(() => { this.sweepTimer = null; this.sweep(); }, 5000);
  }
  sweep() {
    const t = now();
    for (const c of [...this.conns.values()]) {
      if (!this.conns.has(c.id)) continue;
      if (c.mode === 'pending') {
        const gone = !this.sessions.has(c.session);
        if (gone || t - c.since > kJoinWait) {
          this.sendTo(c, { t: 'relay', ok: false, error: gone ? 'That server just closed.' : 'The server\'s host didn\'t answer. Try another server.' });
          this.drop(c.id);
        }
        continue;
      }
      const quiet = t - c.lastActive;
      if ((c.mode === 'host' && quiet > kHostSilence) || (c.mode === 'pipe' && quiet > kPipeSilence) ||
          (c.mode === 'request' && quiet > 120)) this.drop(c.id);
    }
    this.scheduleSweep();
  }

  onMessage(c, data) {
    c.lastActive = now();
    if (c.mode === 'pipe') {   // game traffic: pass it on untouched
      const peer = this.conns.get(c.peer);
      if (peer) { peer.lastActive = c.lastActive; try { peer.ws.send(data); } catch { this.drop(c.id); } }
      return;
    }
    if (c.mode === 'host' || c.mode === 'pending') return;   // pings keep it alive
    const text = typeof data === 'string' ? data : new TextDecoder().decode(new Uint8Array(data));
    let req;
    try { req = JSON.parse(text); } catch { req = null; }
    if (!req || typeof req !== 'object' || Array.isArray(req)) { this.sendTo(c, fail('That wasn\'t a proper request.')); return; }
    if (req.t === 'accept') { this.relayAccept(c, req.ticket); return; }
    if (typeof req.op === 'string' && req.op.startsWith('relay.')) { this.relayRequest(c, req); return; }
    const reply = this.handle(req);
    if ('id' in req) reply.id = req.id;
    this.sendTo(c, reply);
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/ws') {
      if (request.headers.get('Upgrade') !== 'websocket') return new Response('Expected a WebSocket.', { status: 426 });
      const pair = new WebSocketPair();
      const ws = pair[1];
      ws.accept();
      const c = { id: randomHex(8), ws, mode: 'request', account: '', session: '', ticket: '', peer: '', since: now(), lastActive: now() };
      this.conns.set(c.id, c);
      // Messages are handled one at a time, in order (some arrive as Blobs, which take a moment to read).
      let queue = Promise.resolve();
      ws.addEventListener('message', (e) => {
        queue = queue.then(async () => {
          if (!this.conns.has(c.id)) return;
          let data = e.data;
          if (typeof data !== 'string' && typeof data.arrayBuffer === 'function' && !(data instanceof ArrayBuffer)) data = await data.arrayBuffer();
          this.onMessage(c, data);
        }).catch(() => this.drop(c.id));
      });
      const gone = () => { if (this.conns.has(c.id)) this.drop(c.id); };
      ws.addEventListener('close', gone);
      ws.addEventListener('error', gone);
      this.scheduleSweep();
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    // GET /thumb/<asset id>: a game's picture (public, so pages can show it directly).
    if (url.pathname.startsWith('/thumb/')) {
      const id = decodeURIComponent(url.pathname.slice(7));
      const a = this.assets.get(id);
      const data = a && a.thumb ? this.readFile('thumb:' + id) : null;
      if (!data) return new Response('No picture.', { status: 404 });
      const jpg = data[0] === 0xff && data[1] === 0xd8;
      return new Response(data, { headers: { 'content-type': jpg ? 'image/jpeg' : 'image/png', 'cache-control': 'public, max-age=86400' } });
    }
    // POST /api: one request, one answer.
    let req;
    try { req = JSON.parse(await request.text()); } catch { req = null; }
    if (!req || typeof req !== 'object' || Array.isArray(req)) return json(fail('That wasn\'t a proper request.'), 400);
    if (typeof req.op === 'string' && req.op.startsWith('relay.')) return json(fail('Multiplayer needs the Guts&Bolts app.'));
    return json(this.handle(req));
  }
}

function isJson(bytes) {
  try { JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)); return true; } catch { return false; }
}

export function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
}
