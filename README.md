# wowdecomp

A C++ decompilation of the World of Warcraft 3.3.5a client (`Wow.exe`, build 12340, June 2010).

Join the discord for other clients and to show off some cool stuff you build.

https://discord.gg/DsZMGUD8aT

Every function is reconstructed from the original binary and keeps the address it came from in an
`// OFFSET:` tag, so each one can be compared against the original. The source tree follows the layout
of the original client (`Engine/Source/<Module>`, `WoW/Source/<Module>`, original file names).

x64 is the main target. Win32 still builds and is used to check struct layouts against the 32-bit
original.

## Progress

The percentages below measure how much of the original binary has a counterpart in this source tree. They do
**not** say how much of the game works, and the missing part is not missing gameplay: the client builds into a
complete executable and the game is 100% playable. Bugs could still be hiding, though; a ported function can behave differently
from the original.

Measured against the 12340 binary on 2026-10-15. A function counts as ported when its address is
tagged in the source.

| | Ported | Total | Share of the original |
|---|---:|---:|---:|
| Functions | 24,515 | 27,157 | 90.3% |
| Code bytes | 5,270,186 | 5,656,573 | 93.2% |

The total leaves out 658 functions that IDA identifies as compiler library code and 132 import thunks.
Most of the untagged remainder is code the build gets elsewhere: compiler-generated code (static
initializers, template instances) that our compiler emits on its own, C runtime helpers, and third-party
libraries that are linked from their upstream sources instead of being decompiled (FreeType, zlib, PCRE,
parts of FMOD Ex).

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the **Desktop development with C++** workload (MSVC v143 and a Windows SDK)
- CMake 3.10 or newer (the one that ships with Visual Studio works)
- For Win32 builds only: [NASM](https://www.nasm.us/), on `PATH` or passed as `-DNASM_EXECUTABLE=<path>`, and
  `DivxDecoder.dll` from your 3.3.5a client copied to `ThirdParty/DivxDecoder/` (the build copies it next to the
  executables; the x64 build uses its own decoder instead)
- A World of Warcraft **3.3.5a (12340)** client. No game data is included in this repository.

## Building

From a Developer PowerShell or any shell with `cmake` on `PATH`:

```powershell
git clone https://github.com/SatyPardus/wowdecomp.git
cd wowdecomp

# x64 (main target)
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release --target WowClient --parallel

# Win32 (optional, needs NASM)
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/win32 --config Release --target WowClient --parallel
```

In-source builds are refused, so keep the build directory separate (`build/` above).

The client is written to `bin/Win/`:

| Target | x64 | Win32 |
|---|---|---|
| `WowClient` | `WowClient64.exe` | `WowClient32.exe` |
| `Wow` | `Wow64.exe` | `Wow32.exe` |

`WowClient` is the client with a crash logger: it opens a console window and writes
`WowClient_crash.log` when the client crashes. `Wow` is the same client without it. You can also turn the
logger off in `WowClient` with `-DWOWCLIENT_CRASH_LOG=OFF`.

### Options

| CMake option | Default | Effect |
|---|---|---|
| `WOWDECOMP_DISABLE_SIGNATURE_CHECKS` | `OFF` | Skip the MPQ file and `.toc.sig` signature checks, so modified game data and interface files load |
| `WOWCLIENT_CRASH_LOG` | `ON` | Build `WowClient` with the crash logger |
| `WOWCLIENT_OUTPUT_OVERRIDE` | empty | Name the `WowClient` executable something other than `WowClient64` / `WowClient32` |

For modified game data or custom login UI, configure with
`-DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON` and rebuild. This also preserves loose
`Interface/GlueXML`, `Interface/FrameXML`, and Blizzard addon overrides at startup.
Custom client APIs and data formats may still require additional implementation.
Keep local game installations in `client/`; this directory and MPQ archives are
ignored by Git and must not be committed.

### Experimental Ascension UI compatibility

The client includes an opt-in compatibility layer for the original Ascension UI
scripts. Build with signature checks disabled, then add this line to the local
installation's `WTF/Config.wtf`:

```text
SET ascensionUICompatibility "1"
```

Use the original UI from your local Ascension archives. Loose stock
`Interface/GlueXML` and `Interface/FrameXML` overrides take priority over those
archives and must be kept in a separate installation when comparing the two UIs.
The compatibility layer reads the existing archives without modifying their
models, textures, maps, or scripts. No game assets are distributed here.
The original Ascension creation script already uses `MAX_CLASSES_PER_RACE = 32`
and its own CoA class buttons. An error saying
`Too many classes! Update MAX_CLASSES_PER_RACE` identifies the older stock
WotLK override, which limits the UI to ten classes. Remove that override from
the active installation instead of raising its limit: raising it retains the
stock layout and does not restore Ascension's UI.

Implemented so far:

- Extended class lists without the original ten-class buffer limit, with enough
  Lua stack space for CoA's 32 classes.
- Class specialization descriptions, localized strings, archetype role/category
  lists, and class guide data from the original DBC tables.
- Realm flags and class eligibility from the server's `0x09BC` message, and the
  addon list from `0x094E`. These layouts match the local CoA server compatibility
  implementation; they are not a general guarantee for other server versions.
  `CoA.ClassModel=coa` exposes IDs 12–32, `wcr` exposes the nine classic classes,
  and `hero` exposes only ID 10. Native randomization, selection, and creation
  also honor these flags; the archived UI chooses the corresponding layout.
- Saved custom CVars, settings files under `WTF/Custom`, frame attribute
  enumeration, and Lua error diagnostics in `Logs/AscensionUI.log`.
- Character-selection addon preferences, keyboard state, class-name lookup,
  realm ID/session tracking, and server-provided character order, mail indicators,
  and game-mode data (`0x076F`–`0x0771`). Sort changes use `0x0772`.
  Repeated identical Lua errors are logged once per UI initialization so they
  cannot hide later errors by exhausting the diagnostic limit.
- `GetClassInfo(classID)` backed by the original class records, used by
  Ascension's 21 CoA class buttons and role guide. Class names and icon keys come
  from the archives instead of a stock class-name list.
- Base class roles from `ChrClassesRoles.dbc`, avoiding the original eleven-class
  array overrun. Class 32 no longer overflows the legacy race-restriction mask.
- Class preview equipment from `CharacterCreationClassDetails.dbc` and `Item.dbc`,
  selected by class, race, and sex, including weapon placement. Native dress,
  undress, camera position/reset, and zoom APIs support the original creation
  script without replacing its layout or class buttons.
- `GetSpellData` and `GetSpellDescription` read the effective spell records,
  format descriptions with the core spell parser, and resolve original spell
  icons. These APIs are available in GlueXML before entering the world, including
  the race-passive tooltips that previously stopped creation-screen setup.
- Archetype descriptions, primary spells, weapon/armor lists, artwork, and video
  paths from the original DBC records; role/category/archetype selection and
  equipment previews. The customization arrows use the core appearance cycle.
- Server-backed active/inactive character lists (`0x075E`), activate/deactivate
  requests (`0x072E`/`0x072F`) and result events (`0x075F`/`0x0760`). Malformed
  responses preserve the last valid list. Configuration getters consume the
  server's `0x058D` settings; absent settings return nil.
- Error-tooltip spacing and UTF-8 clipboard export for the original error UI.
- Shared libraries load from their original `Interface/LibraryXML/*.toc` archives
  on demand, with recursion protection and the original taint handling. This
  restores the loader used by LibStub/Ace and dependent addons.
- In-game unit-frame APIs expose casting spell IDs, assigned role keys, scaling
  flags, and absorb/heal-absorb totals from the server's `0x0673` aura amounts.
  Spell charges consume `0x09C2`, `0x09C4`, and `0x09C5` and their original DBC
  categories. Interface events support updates to these displays.
- JSON decoding for saved settings, item-name/quality cache lookup, profession
  skill lookup, camera FoV, core nameplate cast-bar visibility, and protected
  action checks. Missing specialization slots return nil instead of interrupting
  the loot-specialization menu. Log sections identify each new UI context.

This is incomplete. Class-preview spell effects, custom animations/transforms,
premium customization-unlock rules, and many in-game talent/collection APIs still
need implementation. Conditional class roles requiring learned spells and
Ascension-specific spell-description expressions are not yet evaluated. Loading
the scripts does not establish visual or functional
parity with `Ascension.exe`. `ObjectSelectionCircleTexture`, `lodObjectCull`,
`useRaidVideoSettings`, and `autoAcceptTrades` are registered as persistent UI
preferences so settings-panel construction can continue; alternate selection
textures, custom culling/raid profiles, and automatic trade acceptance are not
implemented. Nameplate modes 1 and 2 currently use the core cast-bar visibility;
custom rune resources remain separate from the core Death Knight rune UI.
Settings persistence
accepts simple names only; the archive/content update system is not supported.
The local CoA server does not implement realm migration, so the merge API reports
it unavailable. Archetype availability remains controlled by the archived UI's
realm-name check and the server's `CONFIG_CHARACTER_CREATION_ARCHETYPES_ENABLED`
setting. Build and parser checks do not substitute for rendered UI/game testing.

The standalone parser checks cover all three class modes, character lists,
server settings, addons, aura amounts, spell charges/recovery, JSON settings,
and truncated packets using synthetic data; an optional local
`ChrSpecs.dbc` can be passed to the executable without adding it to the repository:

```powershell
cmake -S Tests/AscensionCompatibility -B build/ascension-tests -G "Visual Studio 17 2022" -A x64
cmake --build build/ascension-tests --config Release
ctest --test-dir build/ascension-tests -C Release --output-on-failure
```

## Running

1. Copy the `Data` folder of your 3.3.5a client into `bin/Win/`, so you have `bin/Win/Data/`.
2. Point `Data/<locale>/realmlist.wtf` at the server you want to connect to.
3. Start `bin/Win/WowClient64.exe` (or `Wow64.exe`).

## Layout

| Path | Contents |
|---|---|
| `Engine/Source/` | Engine modules: Storm, Gx (Direct3D 9 / OpenGL), Model2, Frame/FrameXML UI, SoundEngine, Lua, ... |
| `WoW/Source/` | Game modules: objects, world rendering, UI, networking, database, glue screens |
| `WoW/Common/` | Code shared by the game modules |
| `ThirdParty/` | Libraries linked from their upstream sources |
| `Tools/WowClient/` | The `WowClient` executable target |
| `Tools/XvidDecoder/` | x64 movie decoder that replaces the original 32-bit `DivxDecoder.dll` |

## Special thanks

- [binana](https://github.com/thunderbrewhq/binana)
- [thunderbrew](https://github.com/thunderbrewhq/thunderbrew)
- [whoa](https://github.com/whoahq/whoa)
- TOMRUS, for the initial IDB
- Everyone who has reported bugs so far
- The WoW modding community
