// Staff tools: only staff can use them, and everything they do is written down.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test("regular players can't use staff tools", () => {
  const s = makeServer();
  const bob = s.signUp('bob'), carol = s.signUp('carol');
  for (const op of ['admin.ban', 'admin.warn', 'admin.giveBolts', 'admin.log', 'admin.reports']) {
    const r = s.call(bob, op, { to: String(carol.userId), reason: 'spam', amount: 50 });
    assert.equal(r.ok, false, op + ' should be refused');
  }
  assert.equal(carol.banned, false);
});

test('a warning and a ban show up in the staff log', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  assert.ok(s.call(s.staff, 'admin.warn', { to: String(bob.userId), reason: 'spam' }).ok);
  const ban = s.call(s.staff, 'admin.ban', { to: String(bob.userId), reason: 'spam', days: 3 });
  assert.ok(ban.ok, ban.error);
  assert.equal(bob.banned, true);
  const log = s.call(s.staff, 'admin.log', { user: String(bob.userId) });
  assert.ok(log.ok, log.error);
  assert.ok(log.log.length >= 2, 'both actions are in the log');
});
