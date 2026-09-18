set shell := ["zsh", "-cu"]

build:
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build

test: build
    ctest --test-dir build --output-on-failure

fmt:
    clang-format -i src/*.cpp src/*.hpp tools/*.cpp tests/*.cpp

# Load, use and unload the plugin in a throwaway nested Hyprland. Run before
# every release: a crash on unload takes the user's whole session with it.
check-unload: build
    scripts/unload-check.sh build/hyprknit.so

# Redraw the pictures in assets/: hero, By App and Zigzag, the collection plates.
catalogue: build
    ./build/hyprknit-catalogue assets

enable: build
    ./hyprknitctl enable

disable:
    ./hyprknitctl disable

reload: build
    ./hyprknitctl reload

refresh:
    ./hyprknitctl refresh

status:
    ./hyprknitctl status
