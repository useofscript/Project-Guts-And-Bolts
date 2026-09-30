# Next version (not released yet): real liquid

## New

**Real liquid, like a Blender fluid simulation**
- A part called **FluidSource** pours out real liquid made of thousands of
  tiny drops. It flows downhill, piles up, fills dips, splashes off parts of
  every shape (and off the real triangles of Mesh parts), and pushes people
  and floating things along. Attributes: Speed and Rate.
- On computers with OpenGL 4.3 and phones with OpenGL ES 3.1 the physics runs
  on the graphics card (compute shaders): up to about a million drops. Other
  computers (and Macs) run it on the processor, with up to 14,000.
- It's drawn as one smooth, glassy surface: reflections with real Fresnel,
  bending with water's real refractive index (1.333), and deeper water soaks
  up red light first so it looks bluer (Beer's law).
- Things the liquid runs over stay wet (slippery) for a while. Stray drops
  dry up after a few seconds.

**Liquid from scripts**
- New `FluidSystem` (Color, Viscosity, SurfaceTension) and `FluidEmitter`
  (Rate, Velocity, Size, Position, FluidSystem, Enabled) objects, from
  `Instance.new` or Studio's Insert Object. Up to 16 kinds of liquid at once;
  their colours mix where they meet.
- `workspace.MaxFluidParticles` (default 100,000): when it's full, the oldest
  drops are recycled.

**Mega Water Slide (new sample game)**
- A 400-stud tube slide spiralling down an 80-stud tower, with real water
  running down it, a ride timer, a splash pool full of floating toys, a wave
  pool and a fountain.

**Graphics settings**
- New **Graphics API** setting: Auto (the newest OpenGL your graphics card
  has), OpenGL 4.6, OpenGL 4.3, or OpenGL 4.1 "safe mode" (phones: OpenGL ES
  3.2 / 3.1 / 3.0). If the one you pick doesn't work it falls back to the next.
  Settings shows which one is really running, on which graphics card.
- New **Water Quality** setting (Low / Medium / High / Ultra, set by the quality
  presets): how sharp the liquid is drawn, how smooth its surface is, and how
  many drops there can be (25,000 / 60,000 / 100,000 / up to a million).

**Flat faces**
- Faces are flat pictures painted onto the front of the head (like Roblox),
  not little 3D shapes: the classic smiley and catalog faces both hug the
  round head exactly. Old characters and NPCs switch over when a game loads.
  The website's 3D avatars match.

**Better shadows**
- No more light leaking under boxes sitting on the ground: shadows start
  right at an object's base, with no flicker where it touches.
- Sharper shadows close to you (a second, close-up shadow map).

**Publishing puts games on the website**
- Saving a published game in Studio updates it online too.
- The Player's My Games has a Publish button for games that are only on your
  computer. The staff page can put the sample games online as official games.

**Slippery parts**
- Tag a part `Slippery` and people slide on it (water slides, ice).
- Tilted Water parts work: you swim in their real, turned box.

# Guts&Bolts 0.6.0: water, NPCs and Roblox-style controls (September 30, 2026)

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

**Creating and moderation**
- Hats are for Verified creators; shirts and pants can be made by anyone
  signed up (guests can only play).
- Decals and audio are always free, so anyone can use them in their games.
- Staff pick a reason when banning (sexual content, violent extremism,
  harassment, scams, exploiting and more), with an optional note. The banned
  player sees why.

**Phones**
- Popups in the app close when you tap outside them (phones have no Esc key).
- The old server picker popup (which couldn't be closed on phones) is gone.

**3D catalog**
- Hats, shirts and pants in the catalog are shown in 3D, worn by a grey
  mannequin, and an item's page has a 3D view you can drag to turn.

**The Library**
- Studio: **File > Publish Selection to Library** shares objects as a model,
  **public** or **private**. Verified creators can make as many public as they
  like; everyone else 5 a week. Studio takes a picture of it automatically.
- Studio's **Toolbox** now starts with the **Library**: everyone's public
  models, decals and audio, with pictures. Click one to insert it.
- The website's Create page has a **Library** tab (all public models, decals,
  audio and plugins) and a **Models** tab to switch yours public or private.

**A Toolbox like Roblox's**
- Library / Inventory / Recent tabs, a category menu with a search box, and
  a grid of pictures with blue names.
- Every model and asset has a picture. Studio photographs objects on their own
  (on a plain background) when you publish them, and takes one itself for any
  older model that has none. Parts and ready-made objects have pictures too.
- A gold **official** badge marks things made by Guts&Bolts staff (in Studio
  and on the website), so you know they're safe to use.

**Accessories and faces**
- New accessory types: **hair**, **face**, **neck**, **shoulder** and
  **waist**, next to hats. They have their own tabs in the catalog.
- Studio's new **AVATAR > Accessories** window (Verified creators): put it on a
  mannequin, move it into place, **Save position**, and **Upload** straight to
  the catalog with a picture.
- **Faces**: pictures drawn on the front of the head. Only Guts can make them
  (website: Create > Faces).
- Only Guts' own accessories and faces can be made **Limited**; shirts, pants,
  audio, decals and models can't.
- The Guts account can change its join date in Settings.

**Limiteds, resale and trading**
- Item creators (and staff) get an **Edit item** page: name, description,
  price, colour, hat shape or clothing picture.
- Only **Guts** can make an item **Limited**, with a fixed stock. Copies are
  numbered (#1, #2, ...); when it sells out, owners can **resell** their copy
  (they get 70%) and **trade** Limiteds with each other (the new Trades page).

**Username changes**
- Change your username for 1,000 Bolts on the website's Settings page.
- Old usernames show on your profile under "Past usernames" and stay yours:
  nobody else can take them, and you can switch back.

**Clothing templates**
- The character now has a proper clothing layout: shirts and pants can have a
  **picture** painted on the new 585 x 559 templates (`assets/templates/`, also
  downloadable from the website's Create page and saveable from the app).
- See-through parts of the picture show the clothing's colour underneath.
- Worn clothing pictures show in games and in multiplayer.

**Finding games**
- Games can pick up to 3 **genres** (Obby, Horror, Tycoon, Roleplay, Racing
  and more) and a **server size** on their Configure page.
- The Games page has genre buttons, searches descriptions and genres too, and
  sorts by Most played, Playing now, Top rated, Newest or Recently updated.
- Game pages show genres, 👍/👎 votes (after you've played), a like bar, and a
  stats table: playing now, visits, created, updated, server size.

**Game badges**
- Two kinds of badges now: **Guts&Bolts badges** (like Verified, given only
  by staff) and **game badges** that game creators make themselves.
- Make badges on your game's **Configure** page (name, description, colour),
  then award them from a script:
  `game:GetService("BadgeService"):AwardBadge(player, "badge-id")`.
  Everyone in the server sees "X earned the badge ...!" in chat.
- Profiles show both sections; game pages list their badges and how many
  times each has been won.

**Server cards on game pages**
- Every game page lists its running servers like Roblox: the faces of who's
  in each one, "7 of 12 people max", **Join** (that exact server) and
  **Share** (a link to it), with pages when there are lots.

**Always online**
- The Player always connects to the main Guts&Bolts server: no more offline
  or local-network play. Without internet it shows *Can't reach Guts&Bolts*
  with a **Try again** button. Studio's "Play in Guts&BoltsPlayer" puts you in
  an online server too.

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
