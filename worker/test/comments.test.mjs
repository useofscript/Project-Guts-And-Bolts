// Comments under games: posting, the filter, slowing down spammers, deleting,
// turning them off and reporting them.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, rot13 } from './helpers.mjs';
import { FILTER_WORDS } from '../textfilter.js';

test('anyone signed up can comment, and the creator is told', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob');
  const game = s.addGame(alice, 'Arena');
  const r = s.call(bob, 'comments.post', { game: game.id, text: 'This game is so fun!' });
  assert.ok(r.ok, r.error);
  assert.equal(r.comments.length, 1);
  assert.equal(r.comments[0].text, 'This game is so fun!');
  assert.equal(r.comments[0].by.name, 'bob');
  assert.ok((alice.notes || []).some((n) => n.kind === 'comment'), 'alice got a notification');
});

test('bad words become "[ Content Deleted ]"', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob');
  const game = s.addGame(alice, 'Arena');
  const r = s.call(bob, 'comments.post', { game: game.id, text: 'you ' + rot13(FILTER_WORDS[0]) });
  assert.ok(r.ok, r.error);
  assert.equal(r.comments[0].text, '[ Content Deleted ]');
});

test('commenting again straight away is too fast', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob');
  const game = s.addGame(alice, 'Arena');
  assert.ok(s.call(bob, 'comments.post', { game: game.id, text: 'one' }).ok);
  const r = s.call(bob, 'comments.post', { game: game.id, text: 'two' });
  assert.equal(r.ok, false);
  assert.match(r.error, /Slow down/);
});

test('people delete their own comments; others can only report them', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob'), carol = s.signUp('carol');
  const game = s.addGame(alice, 'Arena');
  const id = s.call(bob, 'comments.post', { game: game.id, text: 'hello' }).comments[0].id;
  assert.equal(s.call(carol, 'comments.delete', { game: game.id, id }).ok, false);
  const rep = s.call(carol, 'report.send', { kind: 'comment', id: game.id + ':' + id, reason: 'spam' });
  assert.ok(rep.ok, rep.error);
  const del = s.call(bob, 'comments.delete', { game: game.id, id });
  assert.ok(del.ok, del.error);
  assert.equal(del.comments.length, 0);
});

test('a creator can turn comments off', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob');
  const game = s.addGame(alice, 'Arena');
  assert.ok(s.call(alice, 'game.settings', { id: game.id, comments: false }).ok);
  const r = s.call(bob, 'comments.post', { game: game.id, text: 'hi' });
  assert.equal(r.ok, false);
  assert.match(r.error, /turned off/);
});
