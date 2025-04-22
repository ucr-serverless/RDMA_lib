dev:
    git checkout dev

reset_main:
    git fetch
    git reset --hard origin/main

br:
    cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release && cmake --build ./build

alias bc := build_c
build_c:
    cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Debug && cmake --build ./build

meson:
    meson setup build && ninja -C build/ -v

clean:
    rm -rf build/
    rm -rf bin/
    rm libRDMA_lib.a
    rm libRDMA_lib_cpp.a

fmt:
    clang-format -i src/c/*.c src/cpp/*.cpp include/c/*.h include/cpp/*.h utils/*.c utils/*.h
