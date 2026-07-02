#include "appstore_fonts.hpp"

#if LV_USE_FREETYPE
#include "lvgl/src/libs/freetype/lv_freetype.h"
#endif

#include "appstore_paths.hpp"

#include <cstdlib>
#include <vector>

namespace {

#if LV_USE_FREETYPE
lv_font_t *g_runtime_cjk_font_16 = nullptr;
lv_font_t *g_runtime_cjk_font_14 = nullptr;
#endif

bool has_non_ascii(const std::string &value)
{
    for (unsigned char ch : value) {
        if (ch >= 0x80) return true;
    }
    return false;
}

#if LV_USE_FREETYPE
std::string runtime_cjk_font_path(const std::string &app_dir)
{
    const char *env_path = std::getenv("M5APPSTORE_CJK_FONT");
    if (env_path && env_path[0] && file_exists(env_path)) {
        return env_path;
    }

    std::vector<std::string> candidates = {
        parent_dir(app_dir) + "/share/font/AlibabaPuHuiTi-3-55-Regular.ttf",
        app_dir + "/share/font/AlibabaPuHuiTi-3-55-Regular.ttf",
        "/usr/share/APPLaunch/share/font/AlibabaPuHuiTi-3-55-Regular.ttf",
        parent_dir(app_dir) + "/share/font/NotoSansSC-Regular.ttf",
        app_dir + "/share/font/NotoSansSC-Regular.ttf",
        "/usr/share/APPLaunch/share/font/NotoSansSC-Regular.ttf",
    };
    for (const auto &candidate : candidates) {
        if (file_exists(candidate)) return candidate;
    }
    return "";
}
#endif

}  // namespace

#if LV_USE_FREETYPE
void init_runtime_fonts(const std::string &app_dir)
{
    std::string font_path = runtime_cjk_font_path(app_dir);
    if (font_path.empty()) return;

    g_runtime_cjk_font_16 = lv_freetype_font_create(
        font_path.c_str(), LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 16,
        LV_FREETYPE_FONT_STYLE_NORMAL);
    g_runtime_cjk_font_14 = lv_freetype_font_create(
        font_path.c_str(), LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 14,
        LV_FREETYPE_FONT_STYLE_NORMAL);
#if defined(LV_FONT_SOURCE_HAN_SANS_SC_16_CJK) && LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
    if (g_runtime_cjk_font_16) g_runtime_cjk_font_16->fallback = &lv_font_source_han_sans_sc_16_cjk;
#elif defined(LV_FONT_SOURCE_HAN_SANS_SC_14_CJK) && LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    if (g_runtime_cjk_font_16) g_runtime_cjk_font_16->fallback = &lv_font_source_han_sans_sc_14_cjk;
#endif
#if defined(LV_FONT_SOURCE_HAN_SANS_SC_14_CJK) && LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    if (g_runtime_cjk_font_14) g_runtime_cjk_font_14->fallback = &lv_font_source_han_sans_sc_14_cjk;
#endif
}
#else
void init_runtime_fonts(const std::string &) {}
#endif

const lv_font_t *font_for_text(const std::string &text, const lv_font_t *latin)
{
    if (!has_non_ascii(text)) return latin;
#if LV_USE_FREETYPE
    if ((latin == &lv_font_montserrat_20 || latin == &lv_font_montserrat_14) && g_runtime_cjk_font_16) {
        return g_runtime_cjk_font_16;
    }
    if (g_runtime_cjk_font_14) return g_runtime_cjk_font_14;
#endif
#if defined(LV_FONT_SOURCE_HAN_SANS_SC_16_CJK) && LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
    if (latin == &lv_font_montserrat_20 || latin == &lv_font_montserrat_14) {
        return &lv_font_source_han_sans_sc_16_cjk;
    }
#endif
#if defined(LV_FONT_SOURCE_HAN_SANS_SC_14_CJK) && LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    return &lv_font_source_han_sans_sc_14_cjk;
#else
    return latin;
#endif
}
