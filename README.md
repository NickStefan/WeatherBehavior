# WeatherBehavior

An SKSE plugin that equips weather-appropriate clothing (hoods, cloaks, robes, warm gear)
on NPCs and followers — a native, fully configurable take on *Wet and Cold*. It assumes
nothing: you build the rules yourself in-game from the clothing your load order actually has.

## How it works

You define **rules**. Each rule targets **All NPCs** or **Followers only** and activates
under conditions you pick:

- **Weather** — Pleasant / Cloudy / Rainy / Snowy (none checked = any)
- **Season** — Winter / Spring / Summer / Autumn, derived from the in-game month (none = any)

When a rule is active, one item from its list is picked at random per NPC (the pick is
deterministic per NPC, so it stays stable and a crowd shows variety). When the conditions
clear the items are unequipped again, and any copies the mod added are removed — NPC
inventories are left as they were.

The mod only fills free equipment slots. NPCs whose own clothing already covers a slot —
a hooded mage robe, a guard helmet — are left alone; their gear is never displaced. Only
humanoid NPCs are touched: creatures, children, and dead or disabled actors are skipped.

Everything is configured through the **SKSE Menu Framework** UI (section *Weather Behavior*).
The item picker enumerates the wearable armor/clothing in your load order (with a search box
and an "in my inventory only" filter) so you only ever pick real items. Clothing is tracked
solely by its **Editor ID**, so presets stay readable and portable across load orders. Only
items that expose an Editor ID at runtime are listed.

Settings are saved to `Data/SKSE/Plugins/WeatherBehavior.json`.

## Performance

There is no per-frame work. A background thread sleeps and, every *N* seconds (default 5,
`pollSeconds` in the settings file), schedules a single bounded pass over the loaded NPCs
on the game thread. With no active rules the pass returns immediately, and per NPC nothing
is touched unless something actually needs to change. Idle cost is effectively zero.

## Requirements

- SKSE64 (SE/AE) or SKSEVR — the plugin is built multi-runtime.
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) (for the config UI).
- Address Library for SKSE Plugins.
- [powerofthree's Tweaks](https://www.nexusmods.com/skyrimspecialedition/mods/51073) — the base
  game does not keep Editor IDs in memory for armor; this restores them so clothing can be picked
  and resolved by Editor ID.

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
go to *Weather Behavior → Configuration*, add a rule, pick your items, and press **Save**.
