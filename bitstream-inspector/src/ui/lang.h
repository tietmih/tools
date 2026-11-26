#pragma once

namespace ui {

enum class Lang {
  En,
  Zh,
};

Lang language();
void setLanguage(Lang lang);

// English string for Lang::En, Simplified Chinese for Lang::Zh.
const wchar_t* tr(const wchar_t* en, const wchar_t* zh);

}  // namespace ui
