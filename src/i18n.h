// Zweisprachige Texte (Deutsch / English). Die Sprache steht in den Einstellungen.
#pragma once

extern int uiLang;   // 0 = Deutsch, 1 = English

inline const char *T(const char *de, const char *en) { return uiLang == 1 ? en : de; }
