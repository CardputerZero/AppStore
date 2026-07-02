#include "main.h"

#include "global_config.h"
#ifdef CONFIG_V9_5_LV_USE_SDL
#include "hal/hal_paths.h"
#endif
#include "lvgl/lvgl.h"

#include <semaphore.h>
#include <string>
#include <time.h>

extern "C" void cp0_lvgl_init(void);

static sem_t lvgl_sem;

static void lvgl_resume_cb(void *data)
{
    (void)data;
    sem_post(&lvgl_sem);
}

void lvgl_wake(void)
{
    sem_post(&lvgl_sem);
}

static std::string executable_dir(const char *argv0)
{
    if (!argv0 || argv0[0] == '\0')
        return ".";

    std::string path(argv0);
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "/";
    return path.substr(0, slash);
}

int lvgl_main(int argc, char **argv)
{
    (void)argc;
#ifdef CONFIG_V9_5_LV_USE_SDL
    const std::string exe_dir = executable_dir(argv && argv[0] ? argv[0] : nullptr);
    hal_paths_init(exe_dir.c_str());
#endif

    sem_init(&lvgl_sem, 0, 0);
    lv_init();
    lv_timer_handler_set_resume_cb(lvgl_resume_cb, NULL);
    cp0_lvgl_init();

    ui_init(argc, argv);
    while (!ui_should_quit()) {
        uint32_t ms = lv_timer_handler();
        if (ui_should_quit()) {
            break;
        }
        if (ms == LV_NO_TIMER_READY) {
            sem_wait(&lvgl_sem);      // 无定时器，阻塞等事件
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += (ms % 1000) * 1000000;
            ts.tv_sec += ms / 1000 + ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
            sem_timedwait(&lvgl_sem, &ts);  // 定时唤醒 或 被事件提前唤醒
        }
    }
    ui_deinit();
    return 0;
}
