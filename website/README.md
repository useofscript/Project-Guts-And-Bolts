# Guts&Bolts website

A one-page site for the game, made to be hosted for free on **Cloudflare Pages**.
It's plain HTML and pictures, so there is nothing to build.

## Put it online (one time)

1. Make a free account at <https://dash.cloudflare.com/sign-up>.
2. In the dashboard: **Workers & Pages** → **Create** → **Pages** → **Connect to Git**.
3. Pick the `Project-Guts-And-Bolts` repository.
4. Settings:
   - **Production branch:** `main`
   - **Framework preset:** None
   - **Build command:** *(leave empty)*
   - **Build output directory:** `website`
5. Click **Save and Deploy**. After about a minute you get an address like
   `guts-and-bolts.pages.dev`.

From then on, every change to `website/` merged into `main` goes live on its own.

## Change it

- Text: edit `index.html`.
- Pictures: replace the files in `img/` (keep the same names, or update `index.html`).
