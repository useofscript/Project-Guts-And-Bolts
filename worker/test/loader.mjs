// Lets the worker's code load in plain Node for the tests: Cloudflare's
// "cloudflare:workers" module and the files the worker bundles (the crypto
// WebAssembly, the example games, pictures) are swapped for stand-ins.
export async function resolve(spec, ctx, next) {
  if (spec === 'cloudflare:workers')
    return { url: 'data:text/javascript,export class DurableObject { constructor(ctx, env) { this.ctx = ctx; this.env = env; } }', shortCircuit: true };
  if (/\.(wasm|gbscene|png|jpg|bin)$/.test(spec)) return { url: 'data:text/javascript,export default null', shortCircuit: true };
  return next(spec, ctx);
}
