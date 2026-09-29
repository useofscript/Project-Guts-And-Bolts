# Guts&Bolts website

Hosted on **Cloudflare Workers** (free), from this repo. It has two parts:

- **The front page** (`index.html`, `img/`): what Guts&Bolts is, with screenshots
  and download links.
- **The site** (`app/`): the same pages as the Player app, in a browser. People
  can sign up and log in, browse games, buy from the catalog, upload decals,
  audio, clothes, plugins and games, rename their games, and use friends,
  people, groups and Bolts.

## How the site talks to your Guts&Bolts server

Browsers can't open the kind of connection the Guts&Bolts server uses, so the
site sends its requests to `/api`. A small Cloudflare Worker
(`worker/index.js` in the repo root) passes them on to the server and brings
the answers back.

The server's address is the **`GB_SERVER`** setting in `wrangler.jsonc` (for
example `rails-volley.tun.ply.gg:61229`). To change it, edit that file, or set
it in the Cloudflare dashboard under the Worker's **Settings → Variables**.

**Your password never leaves the browser.** Every request is signed in the page
with the account's own key, just like the apps do (the crypto is the same
Monocypher library, compiled to WebAssembly: `app/gbcrypto.wasm`). The Worker
only carries sealed requests and can't change them.

For the site to work:

1. The Guts&Bolts server must be running (`gnb-server status`) and up to date
   (`gnb-server update`). Older servers don't know decals, and don't let
   visitors look around before they sign in.
2. The address in `GB_SERVER` must reach it. With playit.gg, that's a **TCP**
   tunnel to local port **7780**.

## Put it online (one time)

1. Make a free account at <https://dash.cloudflare.com/sign-up>.
2. In the dashboard: **Workers & Pages → Create → Import a repository**, and
   pick `Project-Guts-And-Bolts`.
3. Leave the build command empty. The deploy command is `npx wrangler deploy`.
4. Deploy. You get an address like `project-guts-and-bolts.<you>.workers.dev`.

From then on, every change merged into `main` goes live on its own.

## Change it

- Front page text: `index.html`. Pictures: `img/`.
- Site pages: `app/app.js` (one function per page), look: `app/app.css`.
- The crypto (`app/gbcrypto.wasm`) is built by `tools/webcrypto/build.sh`. You only
  need to rebuild it if `tools/webcrypto/gbcrypto.c` changes.

## Try it on your computer

```sh
npx wrangler dev --var GB_SERVER:127.0.0.1:7780
```

Then open <http://localhost:8787/app/>.
