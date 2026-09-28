#!/usr/bin/env python3
"""Builds the sample games in games/ (run from the repository root).

The scripts inside are taken from the editor's ready-made objects
(src/editor/Premades.cpp), so the samples always match the Toolbox.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PREMADES = (ROOT / "src/editor/Premades.cpp").read_text()


def premade_script(name):
    return re.search(r"const char\* " + name + r' = R"\((.*?)\)";', PREMADES, re.S).group(1)


class Builder:
    def __init__(self):
        self.next_id = 100

    def node(self, name, kind="Part", shape="Cube", pos=(0, 0, 0), size=(1, 1, 1), rot=(0, 0, 0),
             color=(0.64, 0.64, 0.66), material="Plastic", anchored=True, collide=True,
             transparency=0.0, children=(), **extra):
        self.next_id += 1
        n = {"id": self.next_id, "name": name, "kind": kind, "pos": list(pos), "rot": list(rot),
             "size": list(size)}
        if kind == "Part":
            n.update(shape=shape, color=list(color), material=material, anchored=anchored,
                     canCollide=collide, transparency=transparency, castShadow=True)
        n.update(extra)
        if children:
            n["children"] = list(children)
        return n

    def script(self, source, name="Script"):
        return self.node(name, kind="Script", source=source, enabled=True)

    def light(self, color=(1, 0.85, 0.6), brightness=3, rng=16, kind="Point", angle=60, pos=(0, 0, 0), rot=(0, 0, 0)):
        return self.node("SpotLight" if kind == "Spot" else "PointLight", kind="Light", pos=pos, rot=rot,
                         lightType=kind, color=list(color), brightness=brightness, range=rng,
                         spotAngle=angle, enabled=True)


def save(path, info, world, env, children):
    doc = {
        "format": "GutsAndBolts", "version": 2,
        "info": info, "world": world, "environment": env,
        "workspace": {"id": 1, "name": "Workspace", "kind": "Model", "pos": [0, 0, 0], "rot": [0, 0, 0],
                      "size": [1, 1, 1], "children": children},
    }
    path.write_text(json.dumps(doc, indent=2))
    print("wrote", path.relative_to(ROOT))


GORE_WORLD = {"deathStyle": 1, "gore": 2, "dismemberment": True, "fallDamage": True, "fallDamageSpeed": 20}
CLASSIC_WORLD = {"deathStyle": 0, "gore": 0, "dismemberment": False, "fallDamage": False}

# ---------------------------------------------------------------------------
# 1. Obby of Doom
# ---------------------------------------------------------------------------

def obby():
    b = Builder()
    kids = []
    kill = premade_script("kKillBrick")
    kids.append(b.node("Lava", pos=(0, -0.5, -40), size=(160, 1, 160), color=(1.0, 0.3, 0.05),
                       material="Neon", children=[b.script(kill)]))
    kids.append(b.node("StartPlatform", pos=(0, 3.5, 0), size=(10, 1, 10), color=(0.5, 0.52, 0.56), material="Concrete"))
    kids.append(b.node("SpawnLocation", pos=(0, 4.1, 0), size=(3, 0.2, 3), color=(0.3, 0.8, 0.4), material="Neon"))

    grey = (0.75, 0.77, 0.8)
    for i, z in enumerate((-8, -13, -18)):
        kids.append(b.node(f"Step{i + 1}", pos=(0, 3.5, z), size=(3, 1, 3), color=grey))
        kids.append(b.node("Coin", shape="Cylinder", pos=(0, 5.2, z), size=(0.9, 0.12, 0.9), rot=(90, 0, 0),
                           color=(1, 0.8, 0.15), material="Metal", collide=False,
                           children=[b.script(premade_script("kCoin"))]))

    kids.append(b.node("MovingPlatform", pos=(0, 3.5, -25), size=(4, 0.6, 4), color=(0.6, 0.45, 0.95),
                       children=[b.script(premade_script("kMoving"))]))
    for i, z in enumerate((-31, -36)):
        kids.append(b.node(f"FadingPlatform{i + 1}", pos=(0, 3.5, z), size=(3.5, 0.6, 3.5), color=(0.3, 0.75, 0.95),
                           children=[b.script(premade_script("kDisappearing"))]))
    kids.append(b.node("PadPlatform", pos=(0, 3.5, -42), size=(5, 1, 5), color=grey))
    kids.append(b.node("JumpPad", shape="Cylinder", pos=(0, 4.1, -42), size=(2.5, 0.2, 2.5), color=(0.2, 0.9, 0.3),
                       material="Neon", children=[b.script(premade_script("kJumpPad"))]))

    # High walkway with saw blades to dodge.
    kids.append(b.node("SawWalk", pos=(0, 9.5, -60), size=(5, 1, 22), color=(0.35, 0.36, 0.4), material="Metal"))
    for i, (x, z) in enumerate(((-1.2, -54), (1.2, -60), (-1.2, -66))):
        kids.append(b.node(f"SawBlade{i + 1}", shape="Cylinder", pos=(x, 11.4, z), size=(2.6, 0.12, 2.6),
                           rot=(90, 0, 0), color=(0.8, 0.82, 0.85), material="Metal",
                           children=[b.script(premade_script("kSawBlade"))]))
    kids.append(b.node("Coin", shape="Cylinder", pos=(1.5, 11.2, -57), size=(0.9, 0.12, 0.9), rot=(90, 0, 0),
                       color=(1, 0.8, 0.15), material="Metal", collide=False,
                       children=[b.script(premade_script("kCoin"))]))

    finish_src = '''-- Finish line: touch it to win!
local finish = script.Parent
local won = false

finish.Touched:Connect(function(hit)
    if won or not hit.Parent:FindFirstChild("Humanoid") then return end
    won = true
    Gui.Message("YOU WIN! Time: " .. string.format("%.1f", time()) .. "s", 6)
    Sounds.Play("win")
    for i = 1, 6 do
        Effects.Sparks(finish.Position + Vector3.new(math.random(-3, 3), 1, math.random(-3, 3)), 40)
        wait(0.2)
    end
end)
'''
    kids.append(b.node("Finish", pos=(0, 9.5, -76), size=(8, 1, 8), color=(1.0, 0.8, 0.1), material="Neon",
                       children=[b.script(finish_src)]))
    kids.append(b.node("FinishLight", kind="Light", pos=(0, 13, -76), lightType="Point", color=[1, 0.85, 0.3],
                       brightness=5, range=18, enabled=True))

    timer_src = '''-- Shows a timer while you play.
Gui.Message("Reach the golden platform! Don't touch the lava.", 4)
game:GetService("RunService").Heartbeat:Connect(function()
    Gui.Label("Timer", "Time: " .. string.format("%.1f", time()))
end)
'''
    kids.append(b.script(timer_src, "GameScript"))

    env = {"clockTime": 17.5, "sunAzimuth": 150, "sunElevation": 18, "sunColor": [1, 0.72, 0.45],
           "sunIntensity": 1.8, "skyZenith": [0.2, 0.28, 0.55], "skyHorizon": [0.95, 0.62, 0.4],
           "fogColor": [0.9, 0.6, 0.45], "fogDensity": 0.008, "bloomIntensity": 0.8, "cloudCover": 0.5}
    save(ROOT / "games/Obby of Doom.gbscene",
         {"title": "Obby of Doom", "author": "Guts and Bolts",
          "description": "Jump, dodge the saw blades and don't touch the lava. How fast can you reach the golden platform?"},
         GORE_WORLD, env, kids)

# ---------------------------------------------------------------------------
# 2. Demolition Yard
# ---------------------------------------------------------------------------

def demolition():
    b = Builder()
    kids = [b.node("Baseplate", pos=(0, -0.5, 0), size=(80, 1, 80), color=(0.45, 0.43, 0.4), material="Concrete"),
            b.node("SpawnLocation", pos=(0, 0.1, 12), size=(3, 0.2, 3), color=(0.55, 0.57, 0.6))]
    wood = (0.62, 0.42, 0.22)
    # Crate towers.
    for tx, tz in ((-8, -6), (6, -10), (-3, -16)):
        for y in range(4):
            for dx in (0, 1.05):
                kids.append(b.node("Crate", pos=(tx + dx, 0.5 + y * 1.02, tz), size=(1, 1, 1), color=wood,
                                   material="Wood", anchored=False))
    for x, z in ((-4, -4), (3, -4), (8, -2), (-10, -12), (1, -12), (10, -14)):
        kids.append(b.node("ExplodingBarrel", shape="Cylinder", pos=(x, 0.75, z), size=(1, 1.5, 1),
                           color=(0.8, 0.12, 0.08), material="Metal", anchored=False,
                           children=[b.script(premade_script("kBarrel"))]))
    for x, z in ((0, 4), (-6, 2), (5, 2), (-2, -8), (4, -7)):
        kids.append(b.node("Landmine", shape="Cylinder", pos=(x, 0.05, z), size=(0.9, 0.1, 0.9),
                           color=(0.3, 0.05, 0.05), material="Neon", collide=False,
                           children=[b.script(premade_script("kLandmine"))]))
    kids.append(b.node("JumpPad", shape="Cylinder", pos=(8, 0.1, 8), size=(2.5, 0.2, 2.5), color=(0.2, 0.9, 0.3),
                       material="Neon", children=[b.script(premade_script("kJumpPad").replace("launchSpeed = 20", "launchSpeed = 34"))]))
    kids.append(b.node("Tower", pos=(-14, 6, 6), size=(4, 12, 4), color=(0.4, 0.4, 0.45), material="Concrete"))
    kids.append(b.node("TowerStairs1", pos=(-11.5, 0.5, 6), size=(1, 1, 2), color=(0.4, 0.4, 0.45)))

    nuke_src = '''-- The big red button. Click it...
local button = script.Parent
local used = false

button.Clicked:Connect(function()
    if used then return end
    used = true
    Gui.Message("NUKE INCOMING!", 3)
    for i = 3, 1, -1 do
        Gui.Label("Countdown", "Boom in " .. i .. "...")
        wait(1)
    end
    Gui.Label("Countdown", nil)
    for i = 1, 12 do
        Explode(Vector3.new(math.random(-15, 15), 1, math.random(-18, 8)), 7)
        wait(0.15)
    end
    wait(5)
    used = false
end)
'''
    kids.append(b.node("NukeButton", pos=(0, 0.5, 18), size=(1.5, 1, 1.5), color=(0.9, 0.1, 0.1), material="Neon",
                       children=[b.script(nuke_src)]))
    kids.append(b.script('Gui.Message("Welcome to the Demolition Yard. Click the red button if you dare.", 5)', "Welcome"))
    save(ROOT / "games/Demolition Yard.gbscene",
         {"title": "Demolition Yard", "author": "Guts and Bolts",
          "description": "Exploding barrels, landmines and a very big red button. Ragdoll physics and gore turned all the way up."},
         GORE_WORLD, {"clockTime": 13, "sunAzimuth": 150, "sunElevation": 50, "cloudCover": 0.6}, kids)

# ---------------------------------------------------------------------------
# 3. Night Plaza
# ---------------------------------------------------------------------------

def plaza():
    b = Builder()
    kids = [b.node("Baseplate", pos=(0, -0.5, 0), size=(80, 1, 80), color=(0.28, 0.3, 0.33), material="Concrete"),
            b.node("SpawnLocation", pos=(0, 0.1, 10), size=(3, 0.2, 3), color=(0.55, 0.57, 0.6))]
    # Lamp posts around the plaza.
    for x, z in ((-8, -8), (8, -8), (-8, 8), (8, 8), (0, -14)):
        bulb = b.node("Bulb", shape="Sphere", pos=(0, 0.56, 0), size=(2.4, 0.15, 2.4), color=(1, 0.85, 0.55),
                      material="Neon", children=[b.light(pos=(0, -1, 0))])
        bulb["castShadow"] = False
        kids.append(b.node("LampPost", shape="Cylinder", pos=(x, 2, z), size=(0.25, 4, 0.25), color=(0.15, 0.15, 0.17),
                           material="Metal", children=[bulb]))
    # Disco floor in the middle.
    tiles = [b.node("Tile", pos=(x * 2 - 3, 0.05, z * 2 - 3), size=(1.9, 0.1, 1.9), color=(0.5, 0.2, 0.9), material="Neon")
             for x in range(4) for z in range(4)]
    tiles.append(b.light(color=(1, 0.3, 0.8), brightness=4, rng=20, pos=(0, 5, 0)))
    tiles[-1]["name"] = "PartyLight"
    tiles.append(b.script(premade_script("kDisco")))
    kids.append(b.node("DiscoFloor", kind="Model", children=tiles))
    # A glass shop with a spotlight.
    kids.append(b.node("ShopBack", pos=(0, 2, -20), size=(10, 4, 0.5), color=(0.7, 0.3, 0.3), material="Wood"))
    kids.append(b.node("ShopWindow", pos=(0, 2, -16), size=(10, 4, 0.2), color=(0.6, 0.8, 1.0), material="Glass",
                       transparency=0.7))
    kids.append(b.node("ShopRoof", pos=(0, 4.25, -18), size=(10.5, 0.5, 4.5), color=(0.2, 0.2, 0.22)))
    kids.append(b.light(color=(0.6, 0.8, 1.0), brightness=6, rng=14, kind="Spot", angle=70, pos=(0, 3.8, -18)))
    kids.append(b.node("Sign", pos=(0, 5, -18), size=(6, 1, 0.3), color=(0.2, 1.0, 0.8), material="Neon"))
    for x, z in ((-5, 0), (5, 0), (0, -9)):
        kids.append(b.node("Coin", shape="Cylinder", pos=(x, 1.2, z), size=(0.9, 0.12, 0.9), rot=(90, 0, 0),
                           color=(1, 0.8, 0.15), material="Metal", collide=False,
                           children=[b.script(premade_script("kCoin"))]))
    env = {"clockTime": 22, "sunAzimuth": 205, "sunElevation": 35, "sunColor": [0.55, 0.65, 0.95], "sunIntensity": 0.45,
           "sunSize": 0.7, "ambientColor": [0.16, 0.21, 0.38], "groundAmbient": [0.05, 0.05, 0.08],
           "ambientIntensity": 0.35, "reflections": 0.6, "skyZenith": [0.01, 0.015, 0.05],
           "skyHorizon": [0.05, 0.08, 0.17], "skyGround": [0.02, 0.02, 0.04], "cloudCover": 0.3,
           "cloudColor": [0.35, 0.4, 0.55], "fogColor": [0.05, 0.08, 0.16], "fogDensity": 0.02,
           "exposure": 1.25, "bloomIntensity": 0.9, "vignette": 0.4}
    save(ROOT / "games/Night Plaza.gbscene",
         {"title": "Night Plaza", "author": "Guts and Bolts",
          "description": "A chill night out: street lamps, a disco floor and a few hidden coins."},
         CLASSIC_WORLD, env, kids)


if __name__ == "__main__":
    (ROOT / "games").mkdir(exist_ok=True)
    obby()
    demolition()
    plaza()
