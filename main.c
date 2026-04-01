#include "lvgl/lvgl.h"
#include "src/ui.h"
#include "src/device_config.h"
#include "src/provisioning_service.h"

#ifdef _WIN32
    #include <SDL2/SDL.h>
    #include <windows.h>
    #include <dbghelp.h>
#else
    #include <unistd.h>
    #include <signal.h>
    #include <execinfo.h>
    #include "lvgl/src/drivers/evdev/lv_evdev.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile int g_crash_logging = 0;

static void write_crash_log(const char * reason) {
    const char * safe_reason = (reason != NULL) ? reason : "Unknown crash";

    fprintf(stderr, "\n=== CRASH: %s ===\n", safe_reason);

    FILE * f = fopen("crash_log.txt", "a");
    if (f != NULL) {
        time_t t = time(NULL);
        char tbuf[32] = {0};
#ifdef _WIN32
        struct tm tm_info;
        localtime_s(&tm_info, &t);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm_info);
#else
        struct tm tm_info;
        localtime_r(&t, &tm_info);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm_info);
#endif
        fprintf(f, "[%s] CRASH: %s\n", tbuf, safe_reason);
        fclose(f);
    }
}

#ifdef _WIN32
static LONG WINAPI crash_handler(EXCEPTION_POINTERS * info) {
    if (g_crash_logging) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    g_crash_logging = 1;

    char reason[256];
    char module_name[MAX_PATH] = {0};
    HMODULE module = NULL;
    DWORD64 offset = 0;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)info->ExceptionRecord->ExceptionAddress,
                           &module)) {
        GetModuleFileNameA(module, module_name, MAX_PATH);
        offset = (DWORD64)((uintptr_t)info->ExceptionRecord->ExceptionAddress - (uintptr_t)module);
    }

    if (module_name[0] != '\0') {
        snprintf(reason, sizeof(reason),
                 "Exception code 0x%08lX at 0x%p (%s+0x%llX)",
                 (unsigned long)info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress,
                 module_name,
                 (unsigned long long)offset);
    } else {
        snprintf(reason, sizeof(reason),
                 "Exception code 0x%08lX at address 0x%p",
                 (unsigned long)info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
    }
    write_crash_log(reason);

    HANDLE file = CreateFileA("crash.dmp",
                              GENERIC_WRITE,
                              0,
                              NULL,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(),
                          GetCurrentProcessId(),
                          file,
                          MiniDumpNormal,
                          &mei,
                          NULL,
                          NULL);
        CloseHandle(file);
        fprintf(stderr, "Minidump written to crash.dmp\n");
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#else
static void crash_handler(int sig) {
    if (g_crash_logging) {
        _exit(1);
    }
    g_crash_logging = 1;

    char reason[64];
    snprintf(reason, sizeof(reason), "Signal %d received", sig);
    write_crash_log(reason);

    void * bt[32];
    int count = backtrace(bt, 32);
    char ** symbols = backtrace_symbols(bt, count);

    fprintf(stderr, "Stack trace:\n");
    FILE * f = fopen("crash_log.txt", "a");
    if (symbols != NULL) {
        for (int i = 0; i < count; i++) {
            fprintf(stderr, "  %s\n", symbols[i]);
            if (f != NULL) {
                fprintf(f, "  %s\n", symbols[i]);
            }
        }
        free(symbols);
    }
    if (f != NULL) {
        fclose(f);
    }

    _exit(1);
}
#endif

void lv_assert_handler(void) {
    write_crash_log("LVGL assert failed");
    abort();
}

static void lv_print_cb(lv_log_level_t level, const char * buf) {
    (void)level;
    printf("%s\n", buf);
    fflush(stdout);

    FILE * f = fopen("lvgl_log.txt", "a");
    if (f != NULL) {
        fprintf(f, "%s\n", buf);
        fclose(f);
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
#else
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);
    signal(SIGILL,  crash_handler);
#endif

    lv_log_register_print_cb(lv_print_cb);
    device_config_load();
    bool provisioned = device_config_is_provisioned();
    bool provisioning_active = provisioning_service_start_if_needed();
    lv_init();

#ifdef _WIN32
    lv_display_t * disp  = lv_sdl_window_create(800, 480);
    lv_indev_t   * mouse = lv_sdl_mouse_create();
    (void)mouse;
#else
    lv_display_t * disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");

    lv_indev_t * touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0");
    (void)touch;
#endif

    (void)disp;

    if (!provisioned) {
        const char * softap_ssid = provisioning_active ? provisioning_service_get_softap_ssid() : "PiSetup-XXXX";
        ui_show_provisioning_screen(softap_ssid);
    }
    else {
        ui_init();
    }

    while (1) {
        if (ui_is_showing_provisioning_screen() && device_config_is_provisioned()) {
            ui_init();
        }

        lv_timer_handler();
#ifdef _WIN32
        SDL_Delay(5);
#else
        usleep(5000);
#endif
    }

    return 0;
}