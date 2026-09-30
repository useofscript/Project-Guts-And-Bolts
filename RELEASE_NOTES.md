# Guts&Bolts 0.6.0: water, NPCs and Roblox-style controls

## New

**Game UI, like Roblox's**
- New objects: **ScreenGui**, **Frame**, **TextLabel**, **TextButton**,
  **ImageLabel**, **ImageButton**, **UICorner** and **UIStroke**. Games can have
  menus, shop buttons, coin counters and title screens.
- Build them in Studio: insert them, then click, drag and resize them right in
  the viewport. Or make them in a script with `UDim2`, `Vector2` and
  `button.MouseButton1Click`.
- They work in multiplayer, and in Roblox files (import and export).

**Character physics**
- The character's hitbox is now shaped like Roblox's R6 body (2 x 1 studs,
  turning with you), and each body part fires `Touched` on its own.
- The walk animation speeds up and slows down with how fast you really move.
- Loose parts knock you back; spinning platforms carry and turn you; jumping
  off a moving platform keeps its speed.
- Scripts: `math.atan2`, `math.pow` and `math.log10` work like in Roblox.
- The camera follows your head. Scroll all the way in for first person: your
  character fades out as the camera gets close, and the mouse looks around.

**In-game menu and Shift Lock, like Roblox**
- Esc opens a new menu with Players, Settings and Help tabs, and Reset / Leave
  / Resume buttons along the bottom (R and L keys work too, and ask first).
- New settings: Shift Lock Switch, camera sensitivity, invert camera, volume
  and fullscreen, next to graphics quality, FPS, gore and touch controls.
- Shift Lock: press Shift and the camera sits over your right shoulder while
  your character faces wherever you look. Works in Studio's play test too.

**Player collisions**
- Players bump into each other (and can stand on each other's heads). Games
  can turn it off in Game Settings or with `workspace.PlayerCollisions`.

**Assistant and MCP (AI in Studio)**
- New Assistant tab: chat with Claude (your own API key) and it builds,
  scripts, playtests and takes screenshots in your game. Everything is undoable.
- MCP support: let Claude Code, Claude Desktop and other AI tools on your
  computer work in Studio (HTTP at 127.0.0.1:44755/mcp, or `GutsAndBolts --mcp`).

**Damage and blood settings**
- Game Settings has a new Damage & Blood tab: fall damage on/off, safe fall
  speed, fall damage strength, gore, and blood color, amount and how long it stays.
- Blood is now liquid: streaking drops, pools that spread and merge, drips down walls.
- Scripts: `workspace.BloodColor`, `BloodAmount`, `SafeFallSpeed`,
  `FallDamageScale`, and `Effects.Blood(pos, amount, direction)`.

**Smooth multiplayer**
- Other players move smoothly instead of jittering: their movement is shown a
  tenth of a second behind, gliding between updates, and it adapts to bumpy
  connections.

**Names and whispers**
- Players' (and NPCs') names float above their heads, with a health bar when hurt.
- Private chat: `/w PlayerName message` or `/whisper PlayerName message`.

**Real water**
- Water now has moving waves and ripples that bounce off the pool's sides.
- Loose parts float or sink by material (wood floats, metal sinks), tip over
  realistically and bob on the waves.
- Splashes with spray and a sound when things fall in; swimmers leave a wake.
- New water attributes: `Waves` (ocean swell) and `Flow` (currents that carry
  you and floating things along).
- New **WaterSource** (Insert menu): pours water that flows downhill, fills
  pits and pools, and spills over walls. Swim in it and float things on it.

**NPCs and zombies**
- Any character-shaped Model is now an NPC: it walks, jumps, and dies like a
  player. Scripts steer it with `humanoid:MoveTo()`, `MoveToFinished`,
  `humanoid:Move()` and `humanoid.Jump`.
- New **PathfindingService**: works out a route around walls and up ledges.
- New ready-made **Zombie** in the Toolbox: it chases the nearest player and bites.
- Studio's Command Bar runs inside the game while you're playing, and can use `wait()`.

**Climbing and swimming**
- Walk into a **TrussPart** or a ladder to climb it. Parts called **Water** are
  swimmable. Both are in the Insert menu.

**The website looks like it's from 2011**
- A dark blue top bar with the logo, a game search box and your account. Below it
  is a shiny tab row and a gray row with Avatar, Friends, Groups and Bolts.
- The page is white on a gray background. Boxes have title bars, and the
  buttons are glossy.
- New home page: your 3D avatar, Bolts and online friends on the left, and
  "Best of Guts&Bolts" plus new catalog items on the right.
- The front page matches.

**The website is open to everyone**
- Visitors can see every page. When they try to buy something, claim Bolts,
  save an avatar, add a friend or upload, a "You need to log in" popup appears.
- **Play** on a game's page opens the Guts&Bolts app on that game. Visitors
  first pick **Play As Boy** (black cap) or **Play As Girl** and play as a guest.
- Game cards show visits and how many people are playing right now.

**Configure your games on the website**
- Create > My Games > **Configure**: name, description, who can play
  (**Public**, **Friends only** or **Private**), a thumbnail, a square icon, and
  uploading a new version.

**Email, forgot password and two-step verification**
- **Settings** on the website: add an email, change your password, and turn on
  two-step verification (logging in on a new device also needs a code from your
  email).
- **Forgot your password?** emails you a code to set a new one.
- The server needs an email service switched on for this; see the README.

**Fixed:** logging in as an account that never set a password (like Guts) said
"no account with that username". Now it explains how to set one, and the app's
home page reminds you.

**Choose Your Character (guests)**
- "Play as Guest" now asks you to pick **Play As Boy** (black cap) or **Play As
  Girl** (pink ponytail). The "Have an Account?" link takes you to Log In.
- New hairstyle, **Ponytail**, on the Avatar page (app and website).

**A new face**
- Everyone has the classic smile now: two small oval eyes and a round smile.
  Characters saved with the old block face get the new one automatically.

**Connecting to server screen**
- Pressing Play shows the game's icon and name (and who made it), a spinning
  circle with "Connecting to server...", and the Guts&Bolts logo under it,
  until the game is ready. Then it fades away.

## Fixed (Android)
- The app now sizes things using Android's own screen density, so buttons are
  the size you'd expect.
- On a phone held sideways, the header is slimmer and the margins are thinner,
  so there's more room for the page.
- Upright phones: long text wraps. The search box, catalog tabs and the Display
  name box fit on the screen.
- Settings fits on the screen (it scrolls) and matches the site's look.
- Messages in games wrap instead of running off the screen.
- The version number in Settings was stuck on 0.4.0.

# Guts&Bolts 0.5.2: animations, guests and 3D avatars

**Fixed:** "couldn't make a secure connection ... (SSL - Internal error)" when
connecting to the online server. The apps now connect with TLS 1.2, which every
server supports.

## New

**Animations, like Roblox**
- **AVATAR tab > Rig Builder** adds a dummy character. **Animation Editor**
  opens a timeline: move the playhead, then click a body part and turn it with
  the Rotate tool (it bends at the shoulder, hip or neck). That makes a keyframe.
- Drag keyframes to change their timing. Right-click one to change its easing
  (Linear, Constant, Cubic, Elastic, Bounce), give it a name, copy it or delete
  it. You can also loop an animation and set its priority.
- Scripts play them: `humanoid:LoadAnimation(anim):Play()`, or
  `model:LoadAnimation(anim)` for any rig. Tracks have Stop, speed, weight,
  and the Stopped, DidLoop and KeyframeReached events. Animations play on top
  of walking.

**Studio**
- **F** zooms the camera to whatever you've selected (parts, models, tools).
- **Snap to Grid**: move in steps of any number of studs (the grid follows),
  and turn in steps of any number of degrees. Each can be on or off.
- **Collisions**: parts you move stop flush against other parts instead of
  going through them.

**Guests**
- No account? Press **Play as Guest** and play any game, alone or with others.
  Guests can't chat ("Sign up to chat with other users!"), make friends or get
  Bolts.

**Avatars and profiles**
- The website shows your avatar in **3D** (drag to turn), on the Avatar page
  and on profiles.
- Profiles, on the website and in the app, look like a classic Roblox
  profile:
  - the 3D avatar and whether they're online;
  - **Currently Wearing**;
  - statistics (joined, friends, place visits);
  - badges, friends, games and groups.
- **Footstep sounds** while you run.

# Guts&Bolts 0.5.1

**Fixed:** the apps sometimes couldn't connect to the online server, with "SSL -
Internal error". When the app sent several requests at once, their secure
connections could get in each other's way. Now they take turns.

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
