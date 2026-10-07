// The update log (worker/updates.js) must load and every entry must be filled in.
// (A broken bracket here once stopped the whole website server from starting.)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';

test('the update log loads and has updates', () => {
  assert.ok(Array.isArray(BUILT_IN_UPDATES));
  assert.ok(BUILT_IN_UPDATES.length > 10);
});

test('every update has an id, a name, a time, a tag, a summary and what changed', () => {
  const tags = new Set(['Engine', 'Studio', 'Website', 'Player', 'Server', 'Fix', 'Release']);
  const ids = new Set();
  for (const u of BUILT_IN_UPDATES) {
    const where = `update "${u.id}"`;
    assert.match(u.id, /^[a-z0-9-]+$/, `${where}: id is letters, numbers and dashes`);
    assert.ok(!ids.has(u.id), `${where}: the id is used twice`);
    ids.add(u.id);
    assert.ok(typeof u.name === 'string' && u.name.trim(), `${where}: needs a name`);
    assert.ok(Number.isInteger(u.time) && u.time > 1.6e9, `${where}: time is seconds since 1970`);
    assert.ok(tags.has(u.tag), `${where}: tag "${u.tag}" isn't one of ${[...tags].join(', ')}`);
    assert.ok(typeof u.summary === 'string' && u.summary.trim(), `${where}: needs a summary`);
    assert.ok(Array.isArray(u.items) && u.items.length > 0, `${where}: needs a list of what changed`);
    for (const line of u.items) assert.ok(typeof line === 'string' && line.trim(), `${where}: an empty line in items`);
  }
});

test('says "Library", never "Marketplace"', () => {
  // (Roblox's MarketplaceService is a script name, so that one's fine.)
  for (const u of BUILT_IN_UPDATES)
    assert.ok(!/marketplace(?!service)/i.test(JSON.stringify(u)), `update "${u.id}" says Marketplace`);
});
