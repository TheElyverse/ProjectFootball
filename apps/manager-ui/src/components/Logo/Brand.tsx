import logoSvg from "./logo-dark.svg?raw";

// The logo goes into the DOM rather than an <img>, so its text uses the page's font faces
// (see fonts.css) instead of fonts installed on the system. The XML prolog is dropped.
const logo = logoSvg.slice(logoSvg.indexOf("<svg"));

export function Brand() {
  return (
    <div
      role="img"
      aria-label="Elyverse: Football"
      className="flex w-full flex-col uppercase [&>svg]:h-auto [&>svg]:w-full"
      dangerouslySetInnerHTML={{ __html: logo }}
    />
  );
}
