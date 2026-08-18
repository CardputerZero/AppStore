#include "appstore_fonts.hpp"
#include "cp0_font_service.hpp"

#include <cstdlib>

namespace {

constexpr const char *kLatinSansFont = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
constexpr const char *kCjkSansFont = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
constexpr const char *kLatinSerifFont = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf";
constexpr const char *kCjkSerifFont = "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc";

#if LV_USE_FREETYPE
const char *g_latin_sans_path = kLatinSansFont;
const char *g_cjk_sans_path = kCjkSansFont;
const char *g_latin_serif_path = kLatinSerifFont;
const char *g_cjk_serif_path = kCjkSerifFont;
std::string g_latin_sans_path_storage;
std::string g_cjk_sans_path_storage;
std::string g_latin_serif_path_storage;
std::string g_cjk_serif_path_storage;
lv_font_t *g_latin_fonts[4] = {};
lv_font_t *g_cjk_fonts[4] = {};
lv_font_t *g_latin_serif_fonts[4] = {};
lv_font_t *g_cjk_serif_fonts[4] = {};

int font_slot(const lv_font_t *font)
{
    if (font == &lv_font_montserrat_20) return 3;
    if (font == &lv_font_montserrat_14) return 2;
    if (font == &lv_font_montserrat_12) return 1;
    return 0;
}

uint16_t font_size(int slot)
{
    static constexpr uint16_t sizes[] = {10, 12, 14, 20};
    return sizes[slot];
}

lv_font_t *latin_font_for(const lv_font_t *font)
{
    return g_latin_fonts[font_slot(font)];
}
#endif

}  // namespace

#if LV_USE_FREETYPE
void init_runtime_fonts(const std::string &app_dir)
{
    (void)app_dir;
    const char *latin_path = std::getenv("M5APPSTORE_LATIN_SANS_FONT");
    const char *cjk_path = std::getenv("M5APPSTORE_CJK_SANS_FONT");
    const char *latin_serif_path = std::getenv("M5APPSTORE_LATIN_SERIF_FONT");
    const char *cjk_serif_path = std::getenv("M5APPSTORE_CJK_SERIF_FONT");
    if (latin_path && latin_path[0]) {
        g_latin_sans_path_storage = latin_path;
        g_latin_sans_path = g_latin_sans_path_storage.c_str();
    }
    if (cjk_path && cjk_path[0]) {
        g_cjk_sans_path_storage = cjk_path;
        g_cjk_sans_path = g_cjk_sans_path_storage.c_str();
    }
    if (latin_serif_path && latin_serif_path[0]) {
        g_latin_serif_path_storage = latin_serif_path;
        g_latin_serif_path = g_latin_serif_path_storage.c_str();
    }
    if (cjk_serif_path && cjk_serif_path[0]) {
        g_cjk_serif_path_storage = cjk_serif_path;
        g_cjk_serif_path = g_cjk_serif_path_storage.c_str();
    }
    for (int slot = 0; slot < 4; ++slot) {
        const uint16_t size = font_size(slot);
        g_cjk_fonts[slot] = cp0_fonts().get(g_cjk_sans_path, size);
        g_latin_fonts[slot] = cp0_fonts().get(g_latin_sans_path, size);
        g_cjk_serif_fonts[slot] = cp0_fonts().get(g_cjk_serif_path, size);
        g_latin_serif_fonts[slot] = cp0_fonts().get(g_latin_serif_path, size);
        if (g_latin_fonts[slot] != cp0_fonts().fallback(size) &&
            g_cjk_fonts[slot] != cp0_fonts().fallback(size)) {
            g_latin_fonts[slot]->fallback = g_cjk_fonts[slot];
        }
        if (g_latin_serif_fonts[slot] != cp0_fonts().fallback(size) &&
            g_cjk_serif_fonts[slot] != cp0_fonts().fallback(size)) {
            g_latin_serif_fonts[slot]->fallback = g_cjk_serif_fonts[slot];
        }
    }
}
#else
void init_runtime_fonts(const std::string &) {}
#endif

const lv_font_t *font_for_text(const std::string &text, const lv_font_t *latin)
{
#if LV_USE_FREETYPE
    (void)text;
    lv_font_t *selected = latin_font_for(latin);
    return selected ? selected : latin;
#else
    (void)text;
    return latin;
#endif
}

const lv_font_t *font_for_serif_text(const std::string &text, uint16_t size)
{
#if LV_USE_FREETYPE
    (void)text;
    int slot = 0;
    if (size >= 18) slot = 3;
    else if (size >= 14) slot = 2;
    else if (size >= 12) slot = 1;
    lv_font_t *selected = g_latin_serif_fonts[slot];
    return selected ? selected : cp0_fonts().fallback(size);
#else
    (void)text;
    (void)size;
    return LV_FONT_DEFAULT;
#endif
}
