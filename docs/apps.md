# Apps

The static website and the manager UI spike shown inside Unreal.

- [Website](#website)
- [UI spike](#ui-spike)

## Website

`apps/website` is the public, static website of Elyverse: Football: a single
landing page that presents the game's vision (from the
[game design document](game-design-document.md)), where development stands and
how to follow it. It has no backend and no connection to the simulation; the
animated pitch in the hero is a small decorative script, not the match engine.

### Build and preview

Node.js 22 or newer, and pnpm:

```sh
cd apps/website
pnpm install
pnpm run build
pnpm run serve
```

and open `http://localhost:8081/`. `pnpm run build` writes the finished site to
`apps/website/dist/`, which can be uploaded to any static host (GitHub Pages,
Netlify, an S3 bucket, a plain web server). `pnpm run serve --port <n>` changes
the port. While working on styles, `pnpm run watch:css` recompiles
`dist/styles.css` on every change; after changing the HTML, JavaScript or an
image, run `pnpm run build` again.

### Layout

```
apps/website/
  src/
    index.html        the page: all content and markup
    styles.css        Tailwind entry point and theme (colours, fonts, animations)
    main.js           progressive enhancements: header, mobile menu, reveals, hero pitch
    assets/           logo and favicon (SVG)
  scripts/
    copyStatic.mjs    copies src/ to dist/, except the stylesheet
    serve.mjs         local preview server for dist/
```

### Decisions

- **Plain HTML, no framework or bundler.** One page with static content does
  not need React, Astro or Vite; plain HTML stays readable for everyone and has
  no build step besides CSS. If the site grows to several pages (news, devlog),
  a static site generator such as Astro is the natural next step, and the
  Tailwind theme carries over unchanged.
- **Tailwind CSS v4 with its CLI.** The theme lives in `src/styles.css`
  (`@theme`); there is no `tailwind.config.js`. `@import "tailwindcss"
  source("./")` limits the class scan to `src/`. Repeated patterns (buttons,
  cards, headings) are small `@layer components` classes; everything else is
  utilities in the HTML.
- **No web fonts.** The page uses system font stacks: Bahnschrift (Windows) or
  DIN Condensed / Avenir Next Condensed (macOS) for headings, the system UI font
  for text. That means no font downloads and no requests to third parties such
  as Google Fonts, which matters for privacy (GDPR). Self-hosted fonts, for
  example from Fontsource, can be added later without changing the markup.
- **Works without JavaScript.** Content, navigation and layout are plain HTML
  and CSS; `main.js` only adds motion and the mobile menu. Motion respects
  `prefers-reduced-motion`.
- **Dependencies pinned exactly.** `tailwindcss` and `@tailwindcss/cli` are
  pinned in `package.json`; the `pnpm-lock.yaml` that `pnpm install` writes is
  committed, so CI installs the same dependency tree with `--frozen-lockfile`.
  pnpm itself comes from the `packageManager` field via Corepack. Behind a proxy
  that blocks `registry.npmjs.org`, point pnpm at a mirror with `registry=<url>`
  in an `.npmrc`; pnpm honours that setting like any other client.

## UI spike

The UI spike compared three ways to show a sortable squad table in Unreal: a UMG
`ListView`, a Slate `SListView` and a web page in Unreal's WebBrowser widget. The web
variant won (see `docs/implementation-plan.md` section 13.1): the manager UI is a
React app in `apps/manager-ui`, and Unreal only renders 3D content.

### Layout

- `apps/manager-ui` — React, TypeScript and Tailwind CSS, built with Vite into a single
  `apps/unreal-game/Content/ManagerUI/index.html`, because Chromium refuses module
  scripts from `file://` URLs. Packaging stages that folder as loose files.
  `src/ue/bridge.ts` is the only code that talks to Unreal (`query` for view models,
  `command` for actions, `onGameEvent` for what Unreal sends through `window.ui`).
  `src/Game.tsx` switches between the screens in `src/screens`, each filling the whole page:
  the main menu and the manager screen. The manager screen shows one of its views inside
  `ManagerLayout`; `src/screens/Manager/views/index.ts` lists them. UI texts use `react-intl`; the
  catalogs live in `public/locales/<locale>.json`, which the build copies to
  `Content/ManagerUI/locales`. English defines the message ids and is bundled as the
  fallback; the game sends the catalog for its language through the `messages` query.
  A new language is one JSON file plus its check in `src/i18n/catalogs.check.ts`.
- `apps/unreal-game/Source/ElyverseAdapter` — compiles `libs/sim-core` from source with
  Unreal's toolchain and exports `GenerateSquad`. On Linux Unreal uses its own clang and
  libc++, so the CMake-built library cannot be linked.
- `apps/unreal-game/Source/ElyverseFootball` — `UManagerUISubsystem` lives in the game
  instance and puts one transparent web browser over the viewport, rendering at 60 fps
  instead of the WebBrowser default of 24, with `UManagerBridge` bound as
  `window.ue.manager`. Because it outlives maps, the page keeps its state across map
  changes. `AManagerGameMode` is the global default game mode: no pawn, and it shows the
  UI on every map that does not set its own game mode.

### Showing the UI

`UManagerUISubsystem` has three Blueprint-callable functions. `Preload` adds the browser
fully transparent, below UMG widgets, so the page loads behind the studio splash in
`L_Start`. `Show(Route)` makes it visible and calls `window.ui.navigate(route)`; `"/"` is
the main menu, `"/<view>"` a view of the manager screen. An empty route keeps the page where
it is, or opens the main menu the first time; `AManagerGameMode` uses it, so a map change does
not reset the page. `Hide` hides it and gives the input back to
the game. Once the page has subscribed to `window.ui`, it sends the `ready` command; a
`Show` before that waits for it, and a reloaded page (the Vite dev server) gets its route
again.

The page owns the focus. Unreal only forwards `nav.up`, `nav.down`, `nav.confirm` and
`nav.back` through `window.ui.input`, from a Slate input preprocessor that takes Slate's
navigation keys (arrow keys, Enter, Space, Escape, the gamepad's D-pad, left stick and face
buttons) before any widget sees them. Enhanced Input cannot do this: the browser takes the
keyboard focus on every click and then swallows all keys, gamepad buttons included. In a
plain browser the mock maps the same keys. Text fields will need the arrow keys and
Enter in the page; that is still open. `nav.back` returns to the previous view of the current
screen; it never leaves a screen.

The page is transparent over the 3D scene, so scrims are CSS gradients. `backdrop-filter`
blur does not work: CEF renders the page on its own and never sees the scene. The UMG and
  Slate variants of the spike were removed after the decision.

### Build and run

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

`UManagerUISubsystem` loads `Content/ManagerUI/index.html`. Its settings live in
`apps/unreal-game/Config/DefaultGame.ini`:

```
[/Script/ElyverseFootball.ManagerUISubsystem]
PageUrl=http://localhost:5173
PlayerCount=5000
```

`PageUrl` loads the Vite dev server instead of the built file; `Seed` and `PlayerCount`
drive the generated squad until the world simulation exists.

### Package

`apps/unreal-game/package.sh` builds the manager UI, compiles the game target, cooks the
content and archives a self-contained Linux build, Chromium included, under
`build/package/Linux`, and packs it into `build/package/ElyverseFootball-Linux.tar.gz`.
It leaves out debug files and strips the debug info Epic ships in Chromium's
`libcef.so`, which brings the build from 2.6 GB to under 900 MB. It takes the engine
from `UE_ROOT` and the configuration from its argument (`Shipping` by default,
`Development` keeps the console and logs). On Linux, `Config/Linux/LinuxEngine.ini`
turns off Chromium's GPU acceleration: its GPU process fails there (seen with NVIDIA
under Wayland), and with acceleration the page never paints in a packaged build.

```
UE_ROOT=<engine> apps/unreal-game/package.sh
build/package/Linux/ElyverseFootball.sh
```

### Measuring

The status line at the bottom of the manager screen shows whether the Unreal bridge is
bound and the page's frame rate. In Unreal, `stat unit` and `stat slate` show the engine
side; the CEF processes (`EpicWebHelper`) are not part of Unreal's memory statistics.
The editor throttles itself when it loses focus, so turn off *Editor Preferences →
General → Performance → Use Less CPU when in Background* before measuring.
