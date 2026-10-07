// A real website server (worker/server.js) running in plain Node for the tests:
// an in-memory SQLite database stands in for Cloudflare's, and people are made
// signed-up directly (no keys or passwords needed).
import { DatabaseSync } from 'node:sqlite';
import { GbServerObject } from '../server.js';

export const STAFF = 'a'.repeat(64);   // the staff account ("Guts", user #1)

// Cloudflare's ctx.storage.sql, on node:sqlite.
function fakeSql() {
  const db = new DatabaseSync(':memory:');
  return {
    exec(query, ...bind) {
      if (!bind.length && query.split(';').filter((s) => s.trim()).length > 1) { db.exec(query); return []; }
      const st = db.prepare(query);
      if (/^\s*select/i.test(query)) return st.all(...bind);
      st.run(...bind);
      return [];
    },
  };
}

export function makeServer() {
  const s = new GbServerObject({ storage: { sql: fakeSql() } }, { OFFICIAL: STAFF });
  s.signUp = (name, id) => signUp(s, name, id);
  s.call = (me, op, args = {}) => { try { return s.op(op, me, args); } finally { s.flush(); } };
  s.addGame = (creator, name, extra = {}) => addGame(s, creator, name, extra);
  s.staff = signUp(s, 'Guts', STAFF);
  return s;
}

let nextKey = 1;
// Someone with a signed-up account (like account.signup, without the password bits).
export function signUp(s, name, id) {
  const me = s.user(id || (nextKey++).toString(16).padStart(64, 'b'));
  if (s.isOfficial(me)) { me.username = 'Guts'; me.name = 'Guts'; me.userId = 1; }
  else {
    me.userId = s.nextUserId++;
    me.username = name;
    me.name = name;
    s.takenNames.add(name.toLowerCase());
  }
  s.add(me, 100, 'Welcome to Guts&Bolts!', 'welcome');
  s.saveUser(me);
  s.flush();
  return me;
}

// A published game (what Studio's Publish makes).
export function addGame(s, creator, name, extra = {}) {
  const id = String(s.nextAssetNum + 1000);
  const a = Object.assign({ id, num: s.nextAssetNum++, kind: 'game', name, description: '', creator: creator.id, created: Math.floor(Date.now() / 1000),
    updated: Math.floor(Date.now() / 1000), price: 0, sales: 0, plays: 0, meta: {}, access: 'public' }, extra);
  s.assets.set(id, a);
  s.saveAsset(a);
  s.flush();
  return a;
}

// rot13: the filter's word list is kept scrambled so the code doesn't spell them out.
export const rot13 = (w) => w.replace(/[a-z]/gi, (c) => String.fromCharCode((c <= 'Z' ? 65 : 97) + (c.toLowerCase().charCodeAt(0) - 97 + 13) % 26));
