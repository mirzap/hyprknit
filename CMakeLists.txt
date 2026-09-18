cmake_minimum_required(VERSION 3.27)

project(hyprknit VERSION 0.2.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET
  hyprland
  libdrm
  libinput
  libudev
  pangocairo
  pixman-1
  wayland-server
  xkbcommon
)
# The renderer needs no compositor: gdk-pixbuf only decodes user PNG charts.
pkg_check_modules(PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)

function(hyprknit_target_defaults target)
  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -fstack-protector-strong
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-D_FORTIFY_SOURCE=3>
  )
  target_link_options(${target} PRIVATE -Wl,-z,relro,-z,now)
endfunction()

add_library(hyprknit-core STATIC
  src/apps.cpp
  src/chart.cpp
  src/knit.cpp
)
set_target_properties(hyprknit-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(hyprknit-core PUBLIC src)
target_link_libraries(hyprknit-core PUBLIC PkgConfig::PIXBUF)
hyprknit_target_defaults(hyprknit-core)

add_library(hyprknit SHARED
  src/globals.cpp
  src/knit-deco.cpp
  src/pass-element.cpp
  src/plugin.cpp
)
set_target_properties(hyprknit PROPERTIES PREFIX "" OUTPUT_NAME "hyprknit")
target_link_libraries(hyprknit PRIVATE hyprknit-core rt PkgConfig::HYPRLAND)
hyprknit_target_defaults(hyprknit)

add_executable(hyprknit-swatch tools/swatch.cpp)
target_link_libraries(hyprknit-swatch PRIVATE hyprknit-core)
hyprknit_target_defaults(hyprknit-swatch)

# The picture book: plates, comparison and hero, drawn by the real renderer.
pkg_check_modules(CAIRO IMPORTED_TARGET pangocairo)
if(CAIRO_FOUND)
  add_executable(hyprknit-catalogue tools/catalogue.cpp)
  target_link_libraries(hyprknit-catalogue PRIVATE hyprknit-core PkgConfig::CAIRO)
  hyprknit_target_defaults(hyprknit-catalogue)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(hyprknit-tests tests/core-tests.cpp)
  target_link_libraries(hyprknit-tests PRIVATE hyprknit-core)
  hyprknit_target_defaults(hyprknit-tests)
  add_test(NAME core COMMAND hyprknit-tests)
  add_test(NAME swatch-cli
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/swatch-tests.sh
            $<TARGET_FILE:hyprknit-swatch>)
  add_test(NAME omarchy-lifecycle
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/omarchy-lifecycle-tests.sh
            ${CMAKE_CURRENT_SOURCE_DIR})
endif()

install(TARGETS hyprknit hyprknit-swatch
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
