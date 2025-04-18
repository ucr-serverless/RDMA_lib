dev:
    git checkout dev

reset_main:
    git fetch
    git reset --hard origin/main

bc:
    cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release && cmake --build ./build

meson:
    meson setup build && ninja -C build/ -v

clean:
    rm -rf build/
    rm libRDMA_lib.a
