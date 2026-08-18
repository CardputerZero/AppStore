#pragma once

#include "lvgl/lvgl.h"

#include <string>

void init_runtime_fonts(const std::string &app_dir);
const lv_font_t *font_for_text(const std::string &text, const lv_font_t *latin);
const lv_font_t *font_for_serif_text(const std::string &text, uint16_t size);
