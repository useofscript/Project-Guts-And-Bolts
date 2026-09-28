# Guts&Bolts 0.4.0 — now on Android (experimental)

The first release with the **Guts&Bolts Player for Android**, plus the
computer versions of the Player and Studio.

## Downloads

| File | What it is |
| --- | --- |
| `GutsAndBoltsPlayer-Android.apk` | The Player for Android phones and tablets (Android 7.0 or newer) |
| `GutsAndBolts-Windows.zip` | Studio + Player for Windows |
| `GutsAndBolts-macOS.zip` | Studio + Player for Mac |
| `GutsAndBolts-Linux.tar.gz` | Studio + Player for Linux |

On a computer you can also use the installer from the repository
(`Install.bat` / `Install.command` / `install.sh`), which builds everything for
your machine.

### Installing the APK

1. Download `GutsAndBoltsPlayer-Android.apk` on your phone.
2. Open it. Android will ask to allow installing apps from your browser or
   files app: allow it, then tap **Install**.
3. Open **Guts&Bolts** from your home screen. It plays in landscape.

This is an experimental build signed with a test key, so Android may warn
that it's from an unknown developer.

## On your phone

- A thumbstick appears under your left thumb, and there's a jump button on the right.
- Drag anywhere else to look around, pinch to zoom, and tap things to click them.
- Hold your phone upright to browse the site in portrait; games switch to
  landscape by themselves when you join.
- The chat and menu buttons are at the top. The Back button opens the menu or
  goes back a page.
- Swipe to scroll through games, the catalog and your avatar.
- All the sample games are built in. **Host** and **Join a Friend** work over
  Wi-Fi with friends on the same network, phones and computers alike.
- The phone uses lighter graphics settings by default. You can change them in Settings.

## Also new

- **Roblox files:** open Roblox places (`.rbxl` / `.rbxlx`) and insert Roblox
  models (`.rbxm` / `.rbxmx`) in Studio, or drop a `.rbxl` into the `games`
  folder to play it in the Player. Parts, models, scripts (Luau is converted),
  lights, sounds, constraints, attributes and tags come across. Studio can also
  export your game back to Roblox (File > Export to Roblox).
- Accounts with the official **Guts** staff account, Administrator badge and
  signed official badges.
- The Catalog (empty for now; only staff can add items).
- Studio: multi-select, Expand / Collapse Selected, Group / Ungroup, rename
  and more shortcuts (F1 lists them).
- Real physics with ropes, rods, springs, hinges and motors.

## Known limits

- The Android app has been tested with the same code running in "phone mode"
  on Linux (OpenGL ES 3, multi-touch), not yet on many real phones. If
  something looks wrong on your device, please open an issue.
- Studio (making games) is on computers only.
