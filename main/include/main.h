#pragma once

#ifdef __cplusplus
int run_appstore_app(int argc, char **argv);
void ui_init(int argc, char **argv);
void ui_loop(void);
void ui_deinit(void);
bool ui_should_quit(void);
#endif
