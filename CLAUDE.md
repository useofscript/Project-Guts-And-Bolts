# Notes for working on Guts&Bolts

- **Every update goes in the update log.** When a change ships, add an entry at the
  top of `worker/updates.js` (the website's Updates page) with its own cool,
  specific name about what changed (like "Glass Lagoon" for the water update), a
  one-line summary and a few plain-words lines of what changed.
- Say "Library", never "Marketplace".
- Keep the Guts&Bolts branding; don't copy Roblox's name or logos.
- Thumbnails are real renders or 2D pictures, never AI-generated images.
- Never put anyone's personal email address in the code or config.
- The server exists twice: `worker/server.js` (Cloudflare, the main one) and
  `src/server` (C++). A rule changed in one should be changed in the other.
