import logoUrl from "./logo-dark.svg";

export function Brand() {
  return (
    <div className="flex w-full flex-col uppercase">
      <img src={logoUrl} alt="Elyverse: Football" />
    </div>
  );
}
