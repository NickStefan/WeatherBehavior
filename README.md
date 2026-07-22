# WeatherBehavior

An SKSE plugin that equips weather-appropriate clothing (hoods, cloaks, robes, warm gear)
on NPCs and followers — a native, fully configurable take on *Wet and Cold*. It assumes
nothing: you build the rules yourself in-game from the clothing your load order actually has.

## How it works

You define **rules**. Each rule targets **All NPCs** or **Followers only** and activates
under conditions you pick:

- **Weather** — Pleasant / Cloudy / Rainy / Snowy (none checked = any)
- **Season** — Winter / Spring / Summer / Autumn, derived from the in-game month (none = any)
- **Region** — one or more regions from your load order (none = any)

When a rule is active, the items it lists are force-equipped on matching NPCs. When the
conditions clear the items are unequipped again, and any copies the mod added are removed —
NPC inventories are left as they were.

Everything is configured through the **SKSE Menu Framework** UI (section *Weather Behavior*).
The item picker enumerates the wearable armor/clothing in your load order (with a search box,
a wearable-slots filter, and an "in my inventory only" filter) so you only ever pick real items.

Settings are saved to `Data/SKSE/Plugins/WeatherBehavior.json`.

## Performance

There is no per-frame work. A background thread sleeps and, every *N* seconds (default 5,
configurable 1–30), compares a small signature of the environment (weather class + season +
region). Only when that signature *changes* does it run a single bounded pass over the loaded
NPCs on the game thread. Idle cost is effectively zero.

## Requirements

- SKSE64 (SE/AE) or SKSEVR — the plugin is built multi-runtime.
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) (for the config UI).
- Address Library for SKSE Plugins.

## Building

Requires Visual Studio 2022, CMake 3.21+, and vcpkg (`VCPKG_ROOT` set). CommonLibSSE-NG is a
submodule under `extern/`.

```
git submodule update --init --recursive
cmake --preset release
cmake --build build/release --config Release --target WeatherBehavior
```

Set the `SKYRIM_MODS_FOLDER` environment variable to have the built DLL copied into a mod
folder automatically.

## Install

Copy `WeatherBehavior.dll` to `Data/SKSE/Plugins/`. Open the SKSE Menu Framework menu in game,
go to *Weather Behavior → Configuration*, add a rule, pick your items, and press **Save & Apply**.
