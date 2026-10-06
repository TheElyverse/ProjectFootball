import { Suspense, use, type ReactNode } from "react";
import { IntlProvider } from "react-intl";
import { query } from "../ue/bridge";
import en from "../../public/locales/en.json";

// English is the source language: public/locales/en.json defines every message id and is
// bundled as the fallback for every text. The other catalogs are not bundled; the game
// loads the one for its language (the "messages" query).
export type MessageId = keyof typeof en;
export type Messages = Record<MessageId, string>;

// Type-checks every id passed to formatMessage and <FormattedMessage> against en.json.
declare global {
  namespace FormatjsIntl {
    interface Message {
      ids: MessageId;
    }
  }
}

const catalog = query("messages").catch((error: unknown) => {
  console.error(error);
  return { locale: "en", messages: {} };
});

function LocalizedIntlProvider({ children }: { children: ReactNode }) {
  const { locale, messages } = use(catalog);
  return (
    <IntlProvider
      locale={locale}
      defaultLocale="en"
      messages={{ ...en, ...messages }}
    >
      {children}
    </IntlProvider>
  );
}

// Renders nothing until the game has sent the catalog.
export function I18nProvider({ children }: { children: ReactNode }) {
  return (
    <Suspense fallback={null}>
      <LocalizedIntlProvider>{children}</LocalizedIntlProvider>
    </Suspense>
  );
}
