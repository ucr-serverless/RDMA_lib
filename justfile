dev:
    git checkout dev
reset_main:
    git fetch
    git reset --hard origin/main

cmake:
    cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release && cmake --build ./build

