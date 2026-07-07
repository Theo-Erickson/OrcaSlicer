// SplashThemes.cpp
#include "SplashThemes.hpp"

#include <array>
#include <cctype>
#include <string>

namespace Slic3r {
namespace GUI {

// Palettes ported verbatim from the prototype THEMES object. Hex colours are
// converted to RGB triples; glowColor was already an [r,g,b] array there.
static const std::array<SplashThemeDef, (size_t) SplashTheme::Count> THEMES = {{
    // key            bg              version         sub             status          glow
    { "circuit",    { 17, 26, 14 }, { 85,238, 51 }, { 51, 85, 34 }, { 42, 72, 32 }, { 50,220,  0 } },
    { "makerspace", { 20, 16,  8 }, {255,170, 32 }, {136,102, 18 }, { 85, 68, 16 }, {255,140,  0 } },
    { "crashspace", {  9, 13, 24 }, { 85,136,255 }, { 51, 85,159 }, { 40, 64,122 }, { 40,110,255 } },
    { "synthwave",  { 12,  0, 22 }, {255, 68,204 }, {136, 34,102 }, {102, 17, 68 }, {220,  0,240 } },
    { "cyberpunk",  {  3,  8, 16 }, {  0,255,221 }, {255, 34, 68 }, {204, 34, 51 }, {  0,220,200 } },
    { "deepsea",    {  1,  9, 19 }, { 34,170,255 }, {  0, 68,136 }, {  0, 48, 96 }, {  0,120,220 } },
    { "retro",      {  7,  7,  0 }, {170,255,  0 }, {255, 68,  0 }, {136, 68,  0 }, {140,255,  0 } },
    { "fantasy",    { 10,  6, 22 }, {187,102,255 }, {119, 51,136 }, { 85, 34,102 }, {160, 50,220 } },
    { "techsalotl", {  2,  6, 16 }, { 64,192,160 }, {192,160, 32 }, {128, 96,144 }, { 40,190,150 } },
}};

const SplashThemeDef& splash_theme_def(SplashTheme theme)
{
    size_t idx = (size_t) theme;
    if (idx >= THEMES.size())
        idx = (size_t) SplashTheme::Circuit;
    return THEMES[idx];
}

SplashTheme theme_from_key(const std::string& key)
{
    for (size_t i = 0; i < THEMES.size(); ++i)
        if (key == THEMES[i].key)
            return (SplashTheme) i;
    return SplashTheme::Circuit;
}

const char* key_of(SplashTheme theme)
{
    return splash_theme_def(theme).key;
}

// Lowercase + strip anything that isn't a letter or digit.
static std::string normalize_key(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s)
        if (std::isalnum(ch))
            out.push_back((char) std::tolower(ch));
    return out;
}

SplashTheme theme_from_status_id(const std::string& status_theme_id)
{
    const std::string n = normalize_key(status_theme_id);
    if (!n.empty())
        for (size_t i = 0; i < THEMES.size(); ++i)
            if (normalize_key(THEMES[i].key) == n)
                return (SplashTheme) i;
    return SplashTheme::Circuit;
}

} // namespace GUI
} // namespace Slic3r
