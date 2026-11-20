# Paldea Native

**Pokémon Scarlet and Violet (Switch, update 4.0.0) running natively on Android.** Not emulation: the game's own ARM64 code runs on the phone, and its graphics (NVN) run on Vulkan. Built on the engine of [Galar Native](https://github.com/simipoB/Galar-Native-Android) (Sword and Shield).

> [!WARNING]
> **Beta, work in progress.** On a Galaxy S25+ (Adreno 830) Violet and Scarlet boot and play: the opening and several minutes of play at a steady 30 fps. Releases are **pre-releases** until the project leaves beta; much of the newest work (listed below) has not been run on many phones yet. Reports with the app's bug report form help most.

> [!NOTE]
> **No game files, code, shaders or keys are included.** You need your own dump (base game + update 4.0.0) and your console's `prod.keys`.

## How to install

1. Install `PaldeaNative.apk` from [Releases](https://github.com/simipoB/Paldea-Native-Android/releases) (or [build it](#building)).
2. Choose your **game files** (`.nsp` / `.xci`: base game, update 4.0.0, any DLC) and **keys**. Keys stay on the phone.
3. **Set up the game.** Setup detects Scarlet or Violet from your files. One app runs both, each with its own setup, saves, settings and mods, its own name and icon on its home screen shortcut.

- Official releases are signed with `SHA-256: a5f49b44b6181758d82504f3beb0851c92093f454c792b49688728130673a070`, the same key as Galar Native's. Any other build says "Unofficial build".

## Requirements

- Android 12+, arm64, Vulkan 1.0+ (older and cheaper phones' own drivers included: anything missing takes a fallback).
- About 11 GB free per game: the game data (9.3 GB) and its translated shaders, compressed (0.85 GB).
- 8 GB of RAM or more; 12 GB recommended. Scarlet and Violet are heavier than Sword and Shield: **Auto** settings pick one tier lower for them.

## Settings for every phone

All in the in-game panel (☰):

- **3D render scale** slider: a smaller 3D picture for more speed; it applies while you play, and the HUD and text stay sharp.
- **Graphics Auto** holds a steady 30 fps: when the phone falls short it steps through the game's own lighter settings (TV mode, handheld mode, a smaller 3D picture), and as a last resort a shorter draw distance from the next start.
- **Heat guard**: when the phone gets hot, the 3D picture is lightened until it cools down.
- **Frame rate**: Auto, 30, 60 or **Dynamic** (60 where the phone can, 30 where it can't, at the full picture), switched while you play.
- **Draw distance** slider (50–200 %): how far objects load and trees and grass reach.
- **Resolution**: the game's own, 720p, 1080p or 1440p.
- **Graphics Lowest** and **Shadows** (Normal, Low, Off) for weaker phones.
- **Widescreen** at your screen's own shape.
- **Camera sensitivity** slider.
- **Driver checks** at each start: a few quick tests on your phone's graphics driver; anything it gets wrong takes a safe path instead.

## What it shares with Galar Native

Everything that is not game-specific comes from the same engine and app:

- **In-game settings panel** (☰, or − and + held together on a controller): picture, upscaling (FSR 1, SGSR, Sharp), graphics, frame rate, textures, rumble, performance overlay, controller remapping, touch controls, mods, saves, online room, graphics driver, language.
- **Auto settings** from the phone's CPU, GPU and memory tiers, and the display's refresh rates.
- **Mali, Xclipse and PowerVR:** the game's BC textures converted to ETC2 / EAC / ASTC once and kept on disk (the size they have on the Switch, not 4–8 times it).
- **Graphics drivers:** the phone's own, Turnip, PanVK or any driver package.
- **Mods:** RomFS and `.pchtxt`, load order, conflict warnings, notes; their shaders translated before the game starts, and shaders the game builds while it runs translated in the background.
- **Setup that fits the phone:** workers sized to the memory the phone has free (4 GB phones included), the shader pack compressed.
- **Languages:** English, Español, Português, Français, 日本語.

## What is specific to Scarlet and Violet

- **Trinity engine data.** Almost everything is in `arc/data.trpfs`, every file Oodle-compressed. Oodle's decoder is not open source, so setup decodes with the game's own decoder (`dmoodle`: the engine running the game's `OodleLZ_Decompress`), and only the files that hold shaders leave it. Violet has 35,937 shaders.
- **Memory.** The game reserves 12 GB of address space for its allocator and the SDK 1 GB more for its own bookkeeping, more than a phone process can map; both are cut (4 GB, 62 MB) as for Pokémon Legends: Z-A, which uses the same engine.
- **Scarlet and Violet share one engine build and one shader chain**, as Sword and Shield do.

## Building

```sh
GAME=01008F6008C5E000 scripts/build_apk_pc.sh   # → PaldeaNative.apk (Scarlet + Violet)
```

## Credits

The engine and tools are Galar Native's: see its README. Pokémon Legends: Z-A's port in the same engine showed the way through Trinity's packs and its allocator.

Pokémon, Pokémon Scarlet and Pokémon Violet are trademarks of Nintendo, Creatures Inc. and GAME FREAK inc. Not affiliated with or endorsed by them.
