# UI spike

The UI spike compared three ways to show a sortable squad table in Unreal: a UMG
`ListView`, a Slate `SListView` and a web page in Unreal's WebBrowser widget. The web
variant won (see `docs/implementation-plan.md` section 13.1): the manager UI is a
React app in `apps/manager-ui`, and Unreal only renders 3D content.

## Layout

- `apps/manager-ui` — React, TypeScript and Tailwind CSS, built with Vite into a single
  `dist/index.html`, because Chromium refuses module scripts from `file://` URLs.
  `src/bridge.ts` is the only code that talks to Unreal (`query` for view models,
  `command` for actions). `src/App.tsx` switches between the main menu and the manager
  screens, `src/components/ManagerLayout.tsx` is the frame around them, and
  `src/screens/index.ts` lists the manager screens.
- `apps/unreal-game/Source/ElyverseAdapter` — compiles `libs/sim-core` from source with
  Unreal's toolchain and exports `GenerateSquad`. On Linux Unreal uses its own clang and
  libc++, so the CMake-built library cannot be linked.
- `apps/unreal-game/Source/ElyverseFootball` — `UManagerUISubsystem` lives in the game
  instance and puts one web browser over the viewport, rendering at 60 fps instead of the
  WebBrowser default of 24, with `UManagerBridge` bound as `window.ue.manager`. Because
  it outlives maps, the page keeps its state across map changes. `AManagerGameMode` is
  the global default game mode: no pawn, and it shows the UI on every map. The UMG and
  Slate variants of the spike were removed after the decision.

## Build and run

The web UI, in a plain browser with mock data and hot reload:

```
cd apps/manager-ui && pnpm install && pnpm run dev
```

Inside Unreal (UE 5.8; close the editor first, otherwise the build links with hot
reload suffixes and fails):

```
cd apps/manager-ui && pnpm run build
<engine>/Engine/Build/BatchFiles/Linux/Build.sh ElyverseFootballEditor Linux Development \
  -Project=$PWD/apps/unreal-game/ElyverseFootball.uproject -WaitMutex
<engine>/Engine/Binaries/Linux/UnrealEditor apps/unreal-game/ElyverseFootball.uproject
```

`UManagerUISubsystem` loads `apps/manager-ui/dist/index.html`. Its settings live in
`apps/unreal-game/Config/DefaultGame.ini`:

```
[/Script/ElyverseFootball.ManagerUISubsystem]
PageUrl=http://localhost:5173
PlayerCount=5000
```

`PageUrl` loads the Vite dev server instead of the built file; `Seed` and `PlayerCount`
drive the generated squad until the world simulation exists.

## Measuring

The status line at the bottom of the manager screens shows whether the Unreal bridge is
bound and the page's frame rate. In Unreal, `stat unit` and `stat slate` show the engine
side; the CEF processes (`EpicWebHelper`) are not part of Unreal's memory statistics.
The editor throttles itself when it loses focus, so turn off *Editor Preferences →
General → Performance → Use Less CPU when in Background* before measuring.
