// The Guts&Bolts website's helper on Cloudflare.
//
// Web pages can't open the kind of connection the Guts&Bolts server speaks
// (plain TCP with length-prefixed messages), so the site sends each request
// here, to /api, and this Worker passes it on to the server and brings the
// answer back. Requests are already signed in the browser with the player's
// own key, so the Worker can't change them and never sees a password.
//
// The server's address is the GB_SERVER setting (wrangler.jsonc, or the
// Worker's Settings > Variables in the Cloudflare dashboard).
import { connect } from 'cloudflare:sockets';

const MAX_REQUEST = 40 * 1024 * 1024;   // a whole game, base64
const MAX_REPLY   = 48 * 1024 * 1024;
const TIMEOUT_MS  = 60 * 1000;

function reply(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { 'content-type': 'application/json', 'cache-control': 'no-store' },
  });
}

function splitAddress(address) {
  const a = (address || '').trim();
  const colon = a.lastIndexOf(':');
  if (colon > 0 && a.indexOf(':') === colon) return { hostname: a.slice(0, colon), port: Number(a.slice(colon + 1)) || 7780 };
  return { hostname: a, port: 7780 };
}

// Send one message to the server and read one message back.
async function ask(address, text) {
  const body = new TextEncoder().encode(text);
  const frame = new Uint8Array(4 + body.length);
  new DataView(frame.buffer).setUint32(0, body.length);
  frame.set(body, 4);

  const socket = connect(splitAddress(address));
  const timer = new Promise((_, fail) => setTimeout(() => fail(new Error('timeout')), TIMEOUT_MS));
  try {
    const work = (async () => {
      const writer = socket.writable.getWriter();
      await writer.write(frame);
      writer.releaseLock();
      const reader = socket.readable.getReader();
      let got = new Uint8Array(0), need = -1;
      for (;;) {
        const { value, done } = await reader.read();
        if (done) throw new Error('closed');
        const joined = new Uint8Array(got.length + value.length);
        joined.set(got); joined.set(value, got.length);
        got = joined;
        if (need < 0 && got.length >= 4) {
          need = new DataView(got.buffer).getUint32(0);
          if (need > MAX_REPLY) throw new Error('too big');
        }
        if (need >= 0 && got.length >= 4 + need) return new TextDecoder().decode(got.subarray(4, 4 + need));
      }
    })();
    return await Promise.race([work, timer]);
  } finally {
    socket.close().catch(() => {});
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === '/api/info') {
      return reply({ ok: true, server: env.GB_SERVER || '', time: Math.floor(Date.now() / 1000) });
    }
    if (url.pathname === '/api') {
      if (request.method !== 'POST') return reply({ ok: false, error: 'Use POST.' }, 405);
      if (!env.GB_SERVER) return reply({ ok: false, error: 'The website isn\'t connected to a Guts&Bolts server yet (set GB_SERVER).' });
      const len = Number(request.headers.get('content-length') || 0);
      if (len > MAX_REQUEST) return reply({ ok: false, error: 'That\'s too big to send.' }, 413);
      const text = await request.text();
      if (text.length > MAX_REQUEST) return reply({ ok: false, error: 'That\'s too big to send.' }, 413);
      try {
        JSON.parse(text);   // only pass on proper requests
      } catch {
        return reply({ ok: false, error: 'That wasn\'t a proper request.' }, 400);
      }
      try {
        const answer = await ask(env.GB_SERVER, text);
        return new Response(answer, { headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
      } catch (e) {
        return reply({ ok: false, error: 'Couldn\'t reach the Guts&Bolts server (' + (e && e.message || 'error') + '). Is it running?' });
      }
    }
    return env.ASSETS.fetch(request);
  },
};
