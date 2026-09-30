// The update log on the website (the "Updates" page). Every update gets its own
// name. Newest first. Add a new one at the top whenever an update ships; staff can
// also post updates from the website (those are kept in the server's database).
//
//   id     short and unique (letters, numbers, dashes)
//   name   the update's name: make it fun and about what changed
//   time   when it came out (seconds since 1970, like everything else on the server)
//   tag    what it's mostly about: Engine, Studio, Website, Player, Server, Fix
//   items  what changed, one short line each, in plain words

export const BUILT_IN_UPDATES = [
  {
    id: 'steady-aim', name: 'Steady Aim', version: '', time: 1790809300, tag: 'Fix',
    summary: 'The camera no longer slowly tilts up by itself in Shift Lock, first person or fullscreen.',
    items: [
      'With the mouse locked, the middle of the screen was often half a pixel, and the pointer can only sit on whole pixels, so every frame looked like a tiny mouse move up',
      'The camera now only moves when you move the mouse',
    ],
  },
  {
    id: 'deep-end', name: 'The Deep End', version: '', time: 1790805900, tag: 'Engine',
    summary: 'Pools, lakes and rivers get real ocean waves, crystal-clear shallows and splashes that throw real water.',
    items: [
      'Gerstner waves: sharp crests and wide troughs, with whitecaps on big ones; floating things ride the exact waves you see',
      'Water fades from crystal clear at the shore to deep blue in the deep, with the new Clarity setting',
      'Things under the water are bent by it (refraction), and sunlight dances on the bottom',
      'Reflections of the world, and foam where water meets the shore',
      'Big splashes and waves slapping walls throw real liquid drops that fall back in',
      'New FluidVolume for scripts: FlowVelocity, Clarity, WaveScale, and :ParentTo()',
    ],
  },
  {
    id: 'crisp-edges', name: 'Crisp Edges', version: '', time: 1790804476, tag: 'Fix',
    summary: 'No more flickering stripes where water touches walls.',
    items: [
      'Pools, rivers and any Water part sitting against a wall or floor no longer z-fight (the grey streaks on the walls)',
      'Looks clean from underwater too',
    ],
  },
  {
    id: 'hydromania', name: 'The Hydromania Update', version: '0.6.1', time: 1790794400, tag: 'Engine',
    summary: 'Swim, dive, float and sink. Water you can really get into, and a Render Distance bar to keep big maps smooth.',
    items: [
      'You float at the surface with your head out, bobbing with the waves',
      'Dive with C (or Ctrl), or swim forward while looking down; look up or hold Space to come back up',
      'Underwater everything goes blue and hazy, darker the deeper you go',
      'Density on every part: water is 1.3, lighter things float and heavier things sink (in pools and in real liquid)',
      'Render Distance bar in Settings, 1 to 10 (Ultra = everything); far water, liquid, lights and effects rest to save FPS',
      'Plus everything from Glass Lagoon and Tidal Engine below: real GPU liquid, reflections, caustics and more',
    ],
  },
  {
    id: 'glass-lagoon', name: 'Glass Lagoon', version: '0.6.1', time: 1790792629, tag: 'Engine',
    summary: 'Water you can see the bottom of. It mirrors the world around it and catches the sunlight.',
    items: [
      'Drops stretch into flat shapes along the surface, so a calm pool really looks calm',
      'The pool floor stays sharp through the water instead of looking shattered',
      'Reflections of the scene: walls, towers and players show up in the water (Water Quality Medium and up)',
      'Water sits in the shade of things that block the sun',
      'Caustics: bright wobbly lines of sunlight dance on the bottom of pools',
      'No more glittery sparkles on little ripples: the sun glint spreads out softly',
      'New Updates page on the website (this one!) that refreshes by itself',
    ],
  },
  {
    id: 'tidal-engine', name: 'Tidal Engine', version: '0.6.1', time: 1790786619, tag: 'Engine',
    summary: 'Real particle water that runs on your graphics card, and the Mega Water Slide to ride it.',
    items: [
      'Liquid simulated on the GPU: hundreds of thousands of drops at once',
      'FluidSystem and FluidEmitter for scripts, several liquids with their own colours, and a 100,000 drop limit that recycles the oldest drops',
      'Graphics API setting (Auto, or pick an OpenGL version) and a Water Quality setting',
      'Shadows fixed: no light leaking under boxes, and sharper shadows close to the camera',
      'Published games live on the server and show up on the website; saving in Studio updates them',
      'Faces are flat pictures painted on the head, not 3D shapes',
    ],
  },
  {
    id: 'bolts-and-bling', name: 'Bolts & Bling', version: '0.6.0', time: 1790768084, tag: 'Website',
    summary: 'Limiteds, trading, accessories and the Library. The biggest shopping update yet.',
    items: [
      'Limited items with numbered copies, resale and trading',
      'Accessories (hair, face, neck, shoulder, waist) made on a mannequin in Studio',
      'The Library: publish models publicly or privately, with pictures',
      '3D pictures in the catalog and a 3D view you can turn on item pages',
      'Game badges, game genres, votes and server cards on game pages',
      'Username changes for 1000 Bolts (your past names are kept)',
      'In-game menu, Shift Lock, player collisions and the Studio Assistant tab',
    ],
  },
  {
    id: 'eyes-on-the-prize', name: 'Eyes on the Prize', version: '0.5.3', time: 1790727423, tag: 'Player',
    summary: 'Look through your character\'s eyes, and watch other players move smoothly.',
    items: [
      'Scroll in to first person: the camera sits in the head and your character fades away',
      'Character hitboxes shaped like the classic ones, and walking that matches your speed',
      'Other players glide smoothly instead of jumping around',
      'Name tags above heads and /w for private whispers',
    ],
  },
  {
    id: 'making-waves', name: 'Making Waves', version: '0.5.3', time: 1790724793, tag: 'Engine',
    summary: 'Water that moves: waves, floating, splashes and water that pours and fills things up.',
    items: [
      'Waves, swell and currents; things float, and you splash when you jump in',
      'WaterSource pours water that spreads out, fills pools and overflows',
    ],
  },
  {
    id: 'monkey-bars-and-zombies', name: 'Monkey Bars & Zombies', version: '0.5.3', time: 1790723278, tag: 'Engine',
    summary: 'Climb, swim, build game menus, and get chased.',
    items: [
      'Climb trusses and ladders, and swim in water',
      'NPCs and zombies that find their way to you (PathfindingService)',
      'Game UI objects: ScreenGui, Frame, TextLabel, TextButton, ImageLabel',
    ],
  },
  {
    id: 'brickyard-2011', name: 'Brickyard 2011', version: '0.5.3', time: 1790700896, tag: 'Website',
    summary: 'The website and the Player go back to the good old days.',
    items: [
      'The website gets its 2011 look',
      'A "Connecting to server" screen like the old days',
      'Choose Your Character for guests, a new default face',
      'Configure your games on the website: name, pictures, who can play',
      'Email, forgot password and two-step verification',
      'Phones: the Android app fits the screen properly',
    ],
  },
  {
    id: 'puppet-strings', name: 'Puppet Strings', version: '0.5.2', time: 1790687355, tag: 'Studio',
    summary: 'Animate anything, and show off your avatar in 3D.',
    items: [
      'Animation Editor in Studio, and AnimationTracks for scripts',
      '3D avatars on the website, and profiles with what you\'re wearing',
      'Guests can play without an account',
      'Studio: F zooms to what you picked, rotate snap, and a grid that follows the snap',
    ],
  },
  {
    id: 'handshake-hotfix', name: 'Handshake Hotfix', version: '0.5.1', time: 1790682884, tag: 'Fix',
    summary: 'Fixed "SSL - Internal error" when connecting to the server.',
    items: [
      'Secure connections work again on every computer',
      'Footstep sounds while running',
    ],
  },
  {
    id: 'cloud-nine', name: 'Cloud Nine', version: '0.5.0', time: 1790650741, tag: 'Server',
    summary: 'Guts&Bolts moves into the cloud: always online, no one\'s PC needed.',
    items: [
      'The Guts&Bolts server runs on Cloudflare',
      'Sign up, log in and use the site in any browser',
      'Create page: My Games, decals, audio, clothes and plugins',
    ],
  },
  {
    id: 'stickers-and-skyline', name: 'Stickers & Skyline', version: '0.4.x', time: 1790645224, tag: 'Website',
    summary: 'Pictures on parts, and a real website to download from.',
    items: [
      'Decals: stick a picture on any side of a part',
      'The Guts&Bolts website with screenshots and downloads',
    ],
  },
  {
    id: 'toolbelt-and-trophies', name: 'Toolbelt & Trophies', version: '0.4.x', time: 1790643266, tag: 'Engine',
    summary: 'Tools to hold, scores to keep and places to save your progress.',
    items: [
      'Tools and a hotbar',
      'Leaderboards (leaderstats), checkpoints and DataStoreService',
      'Recorded jump, spawn and respawn sounds',
    ],
  },
];
