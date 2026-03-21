#include "lvgl/lvgl.h"
#include "src/ui.h"

#ifdef _WIN32
    #include <SDL2/SDL.h>
#else
    #include <unistd.h>
#endif

static void lv_print_cb(lv_log_level_t level, const char * buf) {
    (void)level;
    printf("%s\n", buf);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    lv_log_register_print_cb(lv_print_cb);
    lv_init();

#ifdef _WIN32
    lv_display_t * disp  = lv_sdl_window_create(640, 480);
    lv_indev_t   * mouse = lv_sdl_mouse_create();
    (void)mouse;
#else
    lv_display_t * disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");
#endif

    (void)disp;

    ui_init();

    while (1) {
        lv_timer_handler();
#ifdef _WIN32
        SDL_Delay(5);
#else
        usleep(5000);
#endif
    }

    return 0;
}