#include "ui/lang.h"

namespace ui {

namespace {
Lang g_language = Lang::En;
}

Lang language() { return g_language; }

void setLanguage(Lang lang) { g_language = lang; }

const wchar_t* tr(const wchar_t* en, const wchar_t* zh) {
  return g_language == Lang::Zh ? zh : en;
}

}  // namespace ui
