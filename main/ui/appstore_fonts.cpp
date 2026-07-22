#include "appstore_fonts.hpp"
#include "cp0_font_service.hpp"

#include <cstdlib>

namespace {

const char *g_runtime_font_path = "AlibabaPuHuiTi-3-55-Regular.ttf";
std::string g_runtime_font_path_storage;

bool has_non_ascii(const std::string &value)
{
    for (unsigned char ch : value) {
        if (ch >= 0x80) return true;
    }
    return false;
}

}  // namespace

#if LV_USE_FREETYPE
void init_runtime_fonts(const std::string &app_dir)
{
    (void)app_dir;
    const char *env_path = std::getenv("M5APPSTORE_CJK_FONT");
    if (env_path && env_path[0]) {
        g_runtime_font_path_storage = env_path;
        g_runtime_font_path = g_runtime_font_path_storage.c_str();
    }
    lv_font_t *font16 = cp0_fonts().get(g_runtime_font_path, 16);
    lv_font_t *font14 = cp0_fonts().get(g_runtime_font_path, 14);
#if defined(LV_FONT_SOURCE_HAN_SANS_SC_16_CJK) && LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
    if (font16 != cp0_fonts().fallback(16)) font16->fallback = &lv_font_source_han_sans_sc_16_cjk;
#elif defined(LV_FONT_SOURCE_HAN_SANS_SC_14_CJK) && LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    if (font16 != cp0_fonts().fallback(16)) font16->fallback = &lv_font_source_han_sans_sc_14_cjk;
#endif
#if defined(LV_FONT_SOURCE_HAN_SANS_SC_14_CJK) && LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    if (font14 != cp0_fonts().fallback(14)) font14->fallback = &lv_font_source_han_sans_sc_14_cjk;
#endif
}
#else
void init_runtime_fonts(const std::string &) {}
#endif

const lv_font_t *font_for_text(const std::string &text, const lv_font_t *latin)
{
    if (!has_non_ascii(text)) return latin;
#if LV_USE_FREETYPE
    if (latin == &lv_font_montserrat_20 || latin == &lv_font_montserrat_14) {
        return cp0_fonts().get(g_runtime_font_path, 16);
    }
    return cp0_fonts().get(g_runtime_font_path, 14);
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
