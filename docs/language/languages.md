# Text in other languages

`tr.inboxTitle(unread: n)` says a message of the app's catalogs
(`locales/en.json`, `de.json`, ...), checked when the app compiles: a
missing key or argument, or one of the wrong type, is an error. A message
said in a binding follows the app's language, so switching it
(`i18n.setLocale("de")`, std/i18n) changes what's shown, and a language
written right to left lays the app out from the right. See
[Languages](../i18n.md).
