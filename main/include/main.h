#pragma once

#ifdef __cplusplus
int lvgl_main(int argc, char **argv);
void ui_init(int argc, char **argv);
void ui_loop(void);
void ui_deinit(void);
bool ui_should_quit(void);
void lvgl_wake(void);
#endif
