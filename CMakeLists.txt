cmake_minimum_required(VERSION 3.10)
project(lvgl_app C)

set(CMAKE_C_STANDARD 99)

# --- LVGL Configuration via compile definitions ---
# This replaces lv_conf.h entirely. Think of these as your vite.config.js feature flags.
add_compile_definitions(
    LV_CONF_SKIP=1            # Tell LVGL to skip looking for lv_conf.h
    LV_BUILD_EXAMPLES=0       # Don't compile LVGL's built-in examples (saves build time)
    LV_BUILD_DEMOS=0          # Don't compile LVGL's built-in demos

    # Color depth must match SDL (32-bit XRGB8888)
    LV_COLOR_DEPTH=32

    # Use the system C library malloc instead of LVGL's tiny built-in pool (default is only 64KB)
    LV_USE_STDLIB_MALLOC=1
    LV_USE_STDLIB_STRING=1
    LV_USE_STDLIB_SPRINTF=1

    # Fonts — enable only the sizes you need (each adds ~30-100KB to your binary)
    LV_FONT_MONTSERRAT_14=1
    LV_FONT_MONTSERRAT_24=1
    LV_FONT_MONTSERRAT_48=1

    LV_USE_LOG=1

    LV_USE_ASSERT_NULL=1
    LV_USE_ASSERT_MALLOC=1
)

if(WIN32)
    add_compile_definitions(
        LV_USE_SDL=1
        LV_SDL_HOR_RES=1920
        LV_SDL_VER_RES=1080
    )
else()
    add_compile_definitions(
        LV_USE_LINUX_FBDEV=1
        LV_USE_EVDEV=1
    )
endif()

include_directories(${CMAKE_SOURCE_DIR})

# Add LVGL — in v9 this is the only submodule you need
add_subdirectory(lvgl)

# App-level sources live outside the vendored lvgl/ directory by design.
# Keep this explicit so new files are never skipped by stale CMake globs.
set(APP_SOURCES
    src/home_api_client.c
    src/net_stream.c
    src/fonts/Antonio_bold_56.c
    src/fonts/Antonio_bold_80.c
)

add_executable(lvgl_app
    main.c
    ${APP_SOURCES}
)

# In v9, link against lvgl::lvgl (namespaced target), not just plain lvgl
find_package(Threads REQUIRED)
target_link_libraries(lvgl_app lvgl::lvgl Threads::Threads)

# On Windows with MinGW, also link SDL2 and its entry-point shim
if(WIN32)
    find_package(SDL2 REQUIRED)
    target_include_directories(lvgl_app PRIVATE ${SDL2_INCLUDE_DIRS})

    # Link only SDL2, NOT SDL2main — SDL2main forces the SDL_main entry point
    # which conflicts with our own main(). SDL_MAIN_HANDLED tells SDL we handle
    # main ourselves, but we also need to not link the library that enforces it.
    target_link_libraries(lvgl_app ${SDL2_LIBRARIES})

    # Remove SDL2main that LVGL added internally
    get_target_property(LVGL_LIBS lvgl::lvgl INTERFACE_LINK_LIBRARIES)
    if(LVGL_LIBS)
        list(REMOVE_ITEM LVGL_LIBS "SDL2main")
        set_target_properties(lvgl::lvgl PROPERTIES INTERFACE_LINK_LIBRARIES "${LVGL_LIBS}")
    endif()

    target_link_libraries(lvgl_app ws2_32)
    target_link_libraries(lvgl_app dbghelp)
endif()
