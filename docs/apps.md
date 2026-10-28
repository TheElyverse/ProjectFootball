# Apps

The static website and the manager UI spike in Unreal.

- [Website](#website)
- [UI spike](#ui-spike)

## Website

`apps/website` is the public website of Elyverse: Football: one static landing
page with the game's vision (from the
[game design document](game-design-document.md)), the state of development and
how to follow it. It has no backend and no link to the simulation. The animated
pitch in the hero is a small decorative script.

### Build and preview

Needs Node.js 22 or newer and pnpm:

```sh
cd apps/website
pnpm install
pnpm run build
pnpm run serve
```

Then open `http://localhost:8081/`. `pnpm run build` writes the site to
`apps/website/dist/`; any static host can serve it (GitHub Pages, Netlify, an S3
bucket, a web server). `pnpm run serve --port <n>` changes the port.
`pnpm run watch:css` rebuilds `dist/styles.css` on every change; after changing
HTML, JavaScript or an image, run `pnpm run build` again.

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

- **Plain HTML, no framework or bundler.** One static page needs no React,
  Astro or Vite. Plain HTML is readable for everyone, and only the CSS has a
  build step. If the site grows to several pages (news, devlog), move to a
  static site generator such as Astro; the Tailwind theme carries over.
- **Tailwind CSS v4 with its CLI.** The theme is in `src/styles.css` (`@theme`);
  there is no `tailwind.config.js`. `@import "tailwindcss" source("./")` limits
  the class scan to `src/`. Repeated patterns (buttons, cards, headings) are
  small `@layer components` classes; the rest are utilities in the HTML.
- **No web fonts.** Headings use Bahnschrift (Windows) or DIN Condensed / Avenir
  Next Condensed (macOS), text the system UI font. No font downloads and no
  requests to third parties such as Google Fonts, which matters for privacy
  (GDPR). Self-hosted fonts, for example from Fontsource, can come later
  without changing the markup.
- **Works without JavaScript.** Content, navigation and layout are HTML and CSS;
  `main.js` only adds motion and the mobile menu. Motion respects
  `prefers-reduced-motion`.
- **Dependencies pinned exactly.** `package.json` pins `tailwindcss` and
  `@tailwindcss/cli`, and the committed `pnpm-lock.yaml` makes CI install the
  same tree with `--frozen-lockfile`. Corepack provides pnpm from the
  `packageManager` field. Behind a proxy that blocks `registry.npmjs.org`, set
  `registry=<url>` in an `.npmrc` to use a mirror.

## UI spike

The UI spike compared three ways to show a sortable squad table in Unreal: a UMG
`ListView`, a Slate `SListView` and a web page in Unreal's WebBrowser widget.
The web page won (see `docs/implementation-plan.md` section 13.1): the manager
UI is a React app in `apps/manager-ui`, and Unreal renders only 3D content. The
UMG and Slate variants were removed after the decision.

### Layout

- `apps/manager-ui` — React, TypeScript and Tailwind CSS. Vite builds it into
  one file, `apps/unreal-game/Content/ManagerUI/index.html`, because Chromium
  refuses module scripts from `file://` URLs; packaging stages that folder as
  loose files.
  - `src/ue/bridge.ts` is the only code that talks to Unreal: `query` for view
    models, `command` for actions, `onGameEvent` for what Unreal sends through
    `window.ui`.
  - `src/Game.tsx` switches between the full-page screens in `src/screens`: the
    main menu and the manager screen. The manager screen shows one view inside
    `ManagerLayout`; `src/screens/Manager/views/index.ts` lists the views.
  - UI texts use `react-intl`. The catalogs are in
    `public/locales/<locale>.json`; the build copies them to
    `Content/ManagerUI/locales`. English defines the message ids and is bundled
    as fallback; the game sends the catalog for its language through the
    `messages` query. A new language is one JSON file plus its check in
    `src/i18n/catalogs.check.ts`.
- `apps/unreal-game/Source/ElyverseAdapter` — compiles `libs/sim-core` from
  source with Unreal's toolchain and exports `GenerateSquad`. On Linux Unreal
  uses its own clang and libc++, so it cannot link the CMake-built library.
- `apps/unreal-game/Source/ElyverseFootball` — `UManagerUISubsystem` lives in
  the game instance. It puts one transparent web browser over the viewport,
  rendering at 60 fps instead of the WebBrowser default of 24, with
  `UManagerBridge` bound as `window.ue.manager`. It outlives maps, so the page
  keeps its state across map changes. `AManagerGameMode` is the global default
  game mode: it has no pawn and shows the UI on every map without its own game
  mode.

### Showing the UI

`UManagerUISubsystem` has three Blueprint-callable functions:

- `Preload` adds the browser fully transparent, below UMG widgets, so the page
  loads behind the studio splash in `L_Start`.
- `Show(Route)` makes it visible and calls `window.ui.navigate(route)`. `"/"` is
  the main menu, `"/<view>"` a view of the manager screen. An empty route keeps
  the current page, or opens the main menu the first time; `AManagerGameMode`
  uses it, so a map change does not reset the page.
- `Hide` hides it and returns input to the game.

Once the page subscribes to `window.ui`, it sends the `ready` command. A `Show`
before that waits for it, and a reloaded page (Vite dev server) gets its route
again.

The page owns the focus. Unreal forwards only `nav.up`, `nav.down`,
`nav.confirm` and `nav.back` through `window.ui.input`. A Slate input
preprocessor takes Slate's navigation keys (arrow keys, Enter, Space, Escape,
the gamepad's D-pad, left stick and face buttons) before any widget sees them.
Enhanced Input cannot do this: the browser takes keyboard focus on every click
and then swallows all keys, gamepad buttons included. In a plain browser the
mock maps the same keys. Open point: text fields will need the arrow keys and
Enter in the page. `nav.back` returns to the previous view of the current
screen; it never leaves a screen.

The page is transparent over the 3D scene, so scrims are CSS gradients.
`backdrop-filter` blur does not work: CEF renders the page alone and never sees
the scene.

### Build and run

The web UI in a plain browser, with mock data and hot reload:

```
cd apps/manager-ui && pnpm install && pnpm run dev
```

Inside Unreal (UE 5.8). Close the editor first; otherwise the build links with
hot reload suffixes and fails:

```
cd apps/manager-ui && pnpm run build
<engine>/Engine/Build/BatchFiles/Linux/Build.sh ElyverseFootballEditor Linux Development \
  -Project=$PWD/apps/unreal-game/ElyverseFootball.uproject -WaitMutex
<engine>/Engine/Binaries/Linux/UnrealEditor apps/unreal-game/ElyverseFootball.uproject
```

`UManagerUISubsystem` loads `Content/ManagerUI/index.html`. Its settings are in
`apps/unreal-game/Config/DefaultGame.ini`:

```
[/Script/ElyverseFootball.ManagerUISubsystem]
PageUrl=http://localhost:5173
PlayerCount=5000
```

`PageUrl` loads the Vite dev server instead of the built file. `Seed` and
`PlayerCount` drive the generated squad until the world simulation exists.

### Package

`apps/unreal-game/package.sh` builds the manager UI, compiles the game target,
cooks the content and archives a self-contained Linux build, Chromium included,
under `build/package/Linux`. It packs that into
`build/package/ElyverseFootball-Linux.tar.gz`. It leaves out debug files and
strips the debug info Epic ships in Chromium's `libcef.so`; the build shrinks
from 2.6 GB to under 900 MB. The engine comes from `UE_ROOT`, the configuration
from the argument (`Shipping` by default; `Development` keeps console and logs).
On Linux, `Config/Linux/LinuxEngine.ini` turns off Chromium's GPU acceleration:
its GPU process fails there (seen with NVIDIA under Wayland), and with
acceleration the page never paints in a packaged build.

```
UE_ROOT=<engine> apps/unreal-game/package.sh
build/package/Linux/ElyverseFootball.sh
```

### Measuring

The status line at the bottom of the manager screen shows whether the Unreal
bridge is bound and the page's frame rate. In Unreal, `stat unit` and
`stat slate` show the engine side; the CEF processes (`EpicWebHelper`) are not
in Unreal's memory statistics. The editor throttles itself without focus, so
turn off *Editor Preferences → General → Performance → Use Less CPU when in
Background* before measuring.
