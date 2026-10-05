# Next version (not released yet)

## New
- **Asset pages**: every decal, sound, model and plugin in the Library has its
  own page, found by its ID (`#/library/<ID>` on the website, *Create > Library*
  in the Player). Listen to audio, see decals full size and **Copy ID** to paste
  it into a game.
- **Make Gear** (staff, in Studio's File menu): start from a ready-made Classic
  Sword, Rocket Launcher, Speed Coil, Gravity Coil or Bomb, try it, and sell it
  in the catalog. The ready-made gear is in the Toolbox for everyone too.
- **Classic profiles**: an *About me* blurb and a *Right now I'm...* status
  (140 letters) on your profile, a **My Feed** box on the home page with your
  friends' and followed people's statuses, and **Player Badges** you earn on your
  own (Creator, Builder, Architect, Friendly, Collector, Old Timer). On the
  website and in the Player.
- **Messages**: a private inbox on the website (Messages in the top bar, with a
  red count of unread ones). Send a message from anyone's profile, reply, and
  delete. *Settings > Privacy* says who can message you (everyone, friends, no
  one). Guests can't send or get messages; 40 messages a day each.
- **Saved outfits** on the Avatar page: save your whole look (colours, hat and
  everything you wear) and wear it again in one click. Up to 30.
- **In the Player app too**: a Messages page (with the unread count in the
  menu and Send Message on profiles), an Outfits tab on the Avatar page, and
  Continue Playing and Favorites rows on the home page.
- **Gutstober in the Player**: orange and purple, a pumpkin and bats in October;
  turn it off in Settings (*Gutstober theme*).
- **Favourite games**: the star on a game's page. **Continue Playing**: the
  games you played last. Both are on the home page and at *Games > mine*.

# Guts&Bolts 0.6.4: Liquid Assets & Explosive Results (October 1, 2026)

## New
- **Explosions with a shockwave** that races outwards: it breaks joints, flings
  loose parts (light ones further), knocks people over, hurts less with distance
  and shakes the camera as it passes.
- **Fire, smoke and mushroom clouds**: fireballs roll up into billowing smoke,
  ground fires keep burning, and big blasts make a mushroom cloud with a dust ring.
- **Bombs and water**: a crater in the surface, a column of spray and rings of
  waves. Big bombs in big water make a **tsunami** that lifts boats, sweeps
  players and parts along and runs up onto the shore.
- **Scripts**: `Explode(position, radius, power, options)` (Smoke, Fire,
  MushroomCloud, Destroy, JointBreak, Visible, Hurts) gives back what it hit, and
  `Instance.new("Explosion")` works like Roblox's (BlastRadius, BlastPressure,
  DestroyJointRadiusPercent, Hit).
- **Premades**: Time Bomb, Nuke and Depth Charge.
- **Real item pictures**: catalog and inventory pictures are renders of the item
  on a character (gear on its own), never drawn icons, in the Player and on the
  website. Catalog items no longer take uploaded pictures at all (both servers
  refuse them), so a picture can never be swapped for something it isn't.
- **New item pages** on the website, laid out like the 2016 catalog: NEW ribbon,
  LIMITED tag, a buy box with what's left and a live off-sale countdown, and the
  creator's character next to their name. Hats and accessories now show up on characters on the website and on
  profiles in the Player.
- **Gutstober** (website): a Halloween theme for all of October (orange and
  purple, a pumpkin and bats), which you can turn off in *Settings > Site theme*.
- **Timed items**: an item can go off sale at a set time ("Off sale in 30 days",
  then "Off sale"); its creator or staff set the date on the item's page. Pumpkin
  items are Gutstober items and go off sale on November 1st.
- **Friends, Following and Followers lists** on everyone's profile, with blue
  links to each person's profile (website and Player).

## Fixed
- Walking up a slightly taller part is smooth instead of snapping up in one go.
- The Verified Hat didn't show up on the website's 3D characters.

# Guts&Bolts 0.6.3: Who Let Us Cook? (October 1, 2026)

## New
- **Navmesh and pathfinding** (the headline): Studio bakes a navigation mesh of
  every floor a character can stand on (re-baked by itself when anchored parts
  change; *MODEL > Navigation > Navmesh* shows it). NPCs walk around walls, jump up
  ledges, drop down and leap gaps. Scripts: Roblox's `PathfindingService`
  (`CreatePath`, `ComputeAsync`, `GetWaypoints`, `Blocked`, ...) or just
  `humanoid:PathfindTo(target)`.
- **Gear in the catalog**: staff publish a Tool from Studio as Gear; people buy
  it, equip up to 4, and get it in games whose creator ticked *Allow gear*.
- **2011-style characters**: an Animate script (idle, walk, run, jump, fall,
  climb, sit, tool), emotes (`/e dance`, wave, point, laugh, cheer),
  `PlatformStand`, tripping and flinging.
- **Tools held like Roblox's** (a `RightGrip` weld and the Grip properties), so
  imported Roblox gear such as the LinkedSword works, scripts and all.
- **Roblox hats** import with their real mesh and texture (mesh versions 1 to 7).
- **Studio**: Move / Rotate increments like Roblox Studio, a studs readout by the
  mouse while dragging, free movement with increments off, right-click
  *Insert Object*, and Lighting / StarterPlayer in the Explorer.
- **More Lua**: `TweenService`, `workspace:Raycast` (+ `Ray.new` /
  `FindPartOnRay`), `GetPartBoundsInRadius`, `HttpService` JSON, `Random.new`,
  `math.noise`, `BindableEvent` / `BindableFunction`, `ReplicatedStorage` /
  `ServerStorage`, `Debris`, `Teams`, `GetPropertyChangedSignal`.
- **User numbers** (#5) are the main way to name someone: add friends, search,
  staff tools and profile links all use them.
- **Leaderboard** folds away with Tab or its arrow; click a name to Friend or
  Follow. **Following** is new, and Guts follows everyone.
- **Join** buttons on profiles, and **privacy settings**: who sees you online
  and what you play (everyone, friends, no one), and who can join you.
- **Security**: authenticator app codes (TOTP) for logging in; confirming your
  email gives you the **Verified Hat**.
- **Terms of Service** and an 18+ heads-up on sign-up.
- **Automatic updates**: the Player and Studio update themselves when they open
  and a new version is out.

## Also new since 0.6.2 (Dress Code and Take a Seat)
- **Seats** (like Roblox): walk into a Seat and you sit on it, facing its front,
  riding along if it moves; jump to get up. Insert one from Insert Object (or
  tag any part "Seat"); Roblox Seats and VehicleSeats import working. Scripts:
  `Humanoid.Sit`, `Humanoid.SeatPart`, `seat.Occupant`, `seat.Disabled`,
  `seat:Sit(humanoid)`.
- **Moderation that works**: a banned account sees a ban screen (in the app
  and on the website) with the reason, staff's note and when it ends; bans can
  last 1, 3, 7 or 30 days or forever, and end by themselves. Staff can also send
  **warnings**, shown once until the player says they understand.
- The app's Avatar page has a **Wardrobe**: everything you own, by kind
  (Shirts, Pants, T-Shirts, Faces, Hats, Hair, Accessories). Click to wear,
  click again to take off, or take everything off. No trip to the Catalog
  needed. Colours and the classic hats are on the **Body & Colours** tab.

## Fixed
- Jumps were too low (about half your height). Now about 1.4 times your height,
  like Roblox; `Humanoid.JumpHeight` and `UseJumpPower` work like Roblox's.
- The play camera no longer goes through walls: it comes in front of them.
- Phones: the thumbstick stays in its corner instead of moving to your thumb.
- Your character on the home screen (the app's banner and the website's home
  page) showed up wearing nothing: shirts and pants are now downloaded and
  worn there too.
- Friends' pictures on profiles wear their outfits.

# Guts&Bolts 0.6.2: Hotfixes, Headaches & Hand Grenades (September 30, 2026)

## New

**Import 3D models, and drag files straight into Studio**
- File > Import 3D Model, Picture, Sound... (and the Import button) reads
  .fbx, .obj (+ .mtl), .gltf / .glb, .stl and .ply. Each object becomes a mesh
  part in its material's colour, grouped in a Model; hard edges stay hard and
  curved surfaces stay smooth. Huge or tiny models are resized to about 10
  studs.
- Drag files from your computer onto Studio: models land where you drop them,
  pictures become Decals on the part under the mouse (or a sign on the
  ground), sounds become Sounds, .lua / .luau files become Scripts, Roblox
  models are inserted and games are opened. Several at once line up side by
  side.

**Water parts, like real water**
- Gerstner waves (the `Waves` attribute, or `WaveScale` on a FluidVolume):
  sharp crests, wide troughs, whitecaps on big waves. Swimmers and floating
  things ride exactly the waves you see.
- The water reads how deep it is at every pixel: crystal clear at the shore,
  fading to deep blue in the deep (Beer-Lambert), set by the new `Clarity`
  (0 murky .. 1 clear).
- Things under the water are bent by it (refraction, index 1.333); reflections
  of the sky and the world; the sun's glint; caustics on the bottom; foam where
  it meets the shore; seen from underwater, the surface bends the world above
  and turns into a mirror at low angles.
- Big splashes and waves slapping walls throw real liquid drops that fall back
  and soak in, and leave a ring of foam.

**Rivers, wetness and diving ("Riptide")**
- Flow maps: a water part's `Flow` is bent around rocks and pillars that poke
  through it (it splits around them and speeds up past them, with white water
  on the sides). The ripples are carried along it, and swimmers and floating
  things follow it too.
- Caustics bend with the waves above them; underwater, everything gets them.
- Sun glints use a Cook-Torrance (GGX) highlight: brilliant sparkles on crests.
- Splashes and dripping swimmers leave things wet: darker and shinier, drying
  over about half a minute. Parts the liquid ran over look wet too.
- Underwater: distance blur and a slight colour split; after coming up for
  air, water runs down the screen for a few seconds.

**T-shirts ("Tee Time")**
- A new catalog item: any .png or .jpg worn flat on the front of the torso,
  over your shirt. Make one on the Create page (website or app), wear it from
  the Avatar page; the game and the website's 3D avatars show it.
- What you put on on the website now shows up properly in the game (clothes,
  faces, accessories and T-shirts), and the Avatar page swaps items of the
  same kind instead of stacking them.

**FluidVolume**
- `Instance.new("FluidVolume")` (or Insert Object): a block of water with
  `FlowVelocity`, `Clarity` and `WaveScale`. Any instance also has
  `:ParentTo(parent)`.

## Fixed
- Catalog pictures of shirts, pants and T-shirts show the real clothing on the
  mannequin (on the website and in the app), not just a plain colour.
- Water no longer flickers in stripes where it touches walls (z-fighting).
- The camera no longer slowly looks up by itself in Shift Lock, first person
  or fullscreen.

# Guts&Bolts 0.6.1: The Hydromania Update (September 30, 2026)

Real water you can pour, swim in, dive under and float things on.

## New

**Swimming, for real**
- You float at the surface with your head out, bobbing with the waves.
- Dive: hold **C** (or **Ctrl**), or swim forward while looking down. Look up
  (or hold **Space**) to come back up; Space at the surface hops you out.
  On phones, swimming follows where the camera looks.
- Underwater, everything goes blue and hazy (the further away, the more the
  water hides it, red first), darker the deeper you go, with a gentle wobble.
- Works in Water parts, waves, floods and the new real liquid.

**Floating and sinking (Density)**
- Every part has a **Density** (Properties, or `part.Density` in scripts).
  Water is 1.3: anything lighter floats, anything heavier sinks. Wood (0.7)
  floats half under, Metal (3.0) and Concrete sink. Works in pools and in
  real liquid.

**Render Distance**
- A new **Render Distance** bar in the in-game Settings (and a slider in
  Advanced graphics), from 1 (60 studs, fastest) to 10 (everything). Low /
  Medium / High / Ultra set it to 4 / 6 / 8 / 10: Ultra is the max.
- Things past it fade into the sky and aren't drawn, and water waves, liquid
  taps, lights and effects out there rest until you come closer: less lag and
  fewer FPS drops on big maps.

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

**Better-looking water ("Glass Lagoon")**
- Drops are drawn as flat shapes that follow the surface, so calm water looks
  calm, and you can see the pool floor clearly through it.
- The water mirrors the scene around it (walls, towers, players), not just
  the sky (Water Quality Medium and up).
- Water sits in the shade of things that block the sun, and caustics (wobbly
  lines of sunlight) play on the bottom of pools.
- No more glittery sparkles on little ripples.

**Updates page on the website**
- A new **Updates** tab lists every update, newest first, each with its own
  name. It checks for new updates by itself, and the tab says NEW when
  there's one you haven't seen. The home page shows the latest one.
- Staff can post updates straight from the page.

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
