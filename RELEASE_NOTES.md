# Guts&Bolts 0.5.0: always online

Guts&Bolts now has its own **online server in the cloud**, so your account,
Bolts, friends, groups and published games are there any time, even when
nobody's computer is on. The apps connect to it by themselves: sign up and
press Play.

There's also a **website**, <https://project-guts-and-bolts.pizzadoe173.workers.dev>,
where you can sign in and do everything the Player's site pages do.

## Downloads

| File | What it is |
| --- | --- |
| `GutsAndBoltsPlayer-Android.apk` | The Player for Android phones and tablets (Android 7.0 or newer) |
| `GutsAndBolts-Windows.zip` | Studio + Player for Windows |
| `GutsAndBolts-macOS.zip` | Studio + Player for Mac |
| `GutsAndBolts-Linux.tar.gz` | Studio + Player for Linux |

To install the APK, open it on your phone and allow installing from your
browser or files app. It's signed with a test key, so Android may warn that it's
from an unknown developer.

## New

**Online**
- **The official server runs on Cloudflare.**
  - The apps talk to it over secure connections (HTTPS and secure WebSockets),
    and check its certificate.
  - The server window has an **Official server** button, and you can still use
    your own server.
- **Sign up and log in** with a username and password. Your password never
  leaves your device.
- **Friends, people and groups:**
  - profiles, friend requests, and seeing who's online and what they're playing;
  - groups with a wall, shouts and member roles.
- **Multiplayer:** public servers (Play finds one), and private servers for
  friends or anyone with the code. Nobody ever sees anyone else's address.
- **Bolts:** daily Bolts, Bolts for playing, codes, and buying from the catalog.
  Verified creators can sell what they make.

**The website**
- Games (and who's playing), the catalog, people, friends, groups and Bolts.
- **Create:** upload decals, audio, clothes, plugins and games, and rename your
  games.
- Works on phones too.

**Making games**
- **Tools:** swords, bats, anything a character can hold, with a hotbar (keys
  1-9).
- **Leaderboard** (`leaderstats`), **checkpoints**, and **saved player data**
  (`DataStoreService`).
- **Decals:** pictures stuck on any side of a part, from your computer or
  uploaded on the Create page.
- The site's new **Create page**:
  - **My Games**, with Edit name, Open in Studio and Play;
  - upload tabs with a Browse button and previews.
- New jump, spawn and respawn sounds.

**The dev log**
- Updates and releases can post to a Discord channel. See
  `.github/workflows/devlog.yml` for the one-time setup.

## Known limits

- Accounts from home-made servers aren't copied to the official server. Sign up
  again there.
- Cloudflare's free plan gives each request very little computer time, so
  uploading very big games or long songs can fail. Smaller uploads are fine.
- The Android app is tested in "phone mode" on Linux and built for real phones,
  but hasn't been tried on many devices yet. If something looks wrong, please
  open an issue.
