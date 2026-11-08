# Recompiled game logic for exefs/main.
cmake_minimum_required(VERSION 3.22)
project(grande_recomp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

# sead engine layer (heaps, framework)
add_library(grande_sead STATIC
    sead/heap.cpp
    sead/exp_heap.cpp
    sead/heap_mgr.cpp
    sead/framework.cpp
    sead/unimplemented.cpp
)
target_include_directories(grande_sead PUBLIC include)

# game code
add_library(grande_recomp STATIC game/main.cpp)
target_include_directories(grande_recomp PRIVATE game)
target_link_libraries(grande_recomp PUBLIC grande_sead)

if(ANDROID)
    # HLE replacement for the Nintendo SDK
    add_library(grande_nn_shim STATIC shim/nn_os_android.cpp)
    target_link_libraries(grande_nn_shim PUBLIC grande_sead log)

    add_executable(heap_test tests/heap_test.cpp)
    target_link_libraries(heap_test PRIVATE grande_sead grande_nn_shim)
endif()
