// Not part of the app: makes `tsc` check that every catalog has exactly the ids of en.json.
import type de from "../../public/locales/de.json";
import type { MessageId } from ".";

type HasExactlyTheMessageIds<Catalog> = [keyof Catalog] extends [MessageId]
  ? [MessageId] extends [keyof Catalog]
    ? true
    : false
  : false;

export const deIsComplete: HasExactlyTheMessageIds<typeof de> = true;
