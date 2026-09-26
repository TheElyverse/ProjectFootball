# Website

`apps/website` is the public, static website of Elyverse: Football: a single
landing page that presents the game's vision (from the
[game design document](game-design-document.md)), where development stands and
how to follow it. It has no backend and no connection to the simulation; the
animated pitch in the hero is a small decorative script, not the match engine.

## Build and preview

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

## Layout

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

## Decisions

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
