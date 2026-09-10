cmake_minimum_required(VERSION 3.16)
project(klypr C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.c
    src/app.c
    src/config.c
    src/input.c
    src/launcher.c
    src/calc.c
    src/tray.c
)

add_executable(klypr WIN32 ${SOURCES})

target_include_directories(klypr PRIVATE include)

if(MSVC)
    target_compile_options(klypr PRIVATE /W4 /WX)
else()
    target_compile_options(klypr PRIVATE -Wall -Wextra -Werror)
endif()

target_link_libraries(klypr PRIVATE user32 gdi32 shell32 advapi32 dwmapi ole32)

set_target_properties(klypr PROPERTIES
    OUTPUT_NAME "klypr"
    SUFFIX ".exe"
)
