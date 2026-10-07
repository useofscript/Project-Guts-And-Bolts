// Bolts (the currency): the welcome gift, the daily reward, and buying things.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('new players start with 100 Bolts and get the daily reward once a day', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  assert.equal(s.balance(bob), 100);
  const r = s.call(bob, 'bolts.daily');
  assert.ok(r.ok, r.error);
  assert.equal(s.balance(bob), 125);
  assert.equal(s.call(bob, 'bolts.daily').ok, false);
  assert.equal(s.balance(bob), 125);
});

test('buying an item takes the Bolts, gives the item and pays the creator a share', () => {
  const s = makeServer();
  const maker = s.signUp('maker'), buyer = s.signUp('buyer');
  const hat = s.addGame(maker, 'Cool Hat', { kind: 'hat', price: 40 });
  const r = s.call(buyer, 'buy', { id: hat.id });
  assert.ok(r.ok, r.error);
  assert.equal(s.balance(buyer), 60);
  assert.ok(buyer.owned.includes(hat.id));
  assert.ok(s.balance(maker) > 100, 'the creator got some Bolts');
  // Buying it again doesn't charge twice.
  assert.equal(s.call(buyer, 'buy', { id: hat.id }).already, true);
  assert.equal(s.balance(buyer), 60);
});

test("you can't buy what you can't afford", () => {
  const s = makeServer();
  const maker = s.signUp('maker2'), poor = s.signUp('poor');
  const pricey = s.addGame(maker, 'Golden Crown', { kind: 'hat', price: 5000 });
  const r = s.call(poor, 'buy', { id: pricey.id });
  assert.equal(r.ok, false);
  assert.match(r.error, /more Bolts/);
  assert.equal(s.balance(poor), 100);
  assert.ok(!poor.owned.includes(pricey.id));
});
