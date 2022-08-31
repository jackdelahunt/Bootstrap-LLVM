cmake_minimum_required(VERSION 3.22)
project(compiler)

# must specify PATH_TO_LLVM and LLVM_LIBS to cmake
message("Using path of llvm -> " ${PATH_TO_LLVM})
message("Using llvm libs -> " ${LLVM_LIBS})

set (search_paths
        ${PATH_TO_LLVM}
        ${PATH_TO_LLVM}/lib/cmake
        ${PATH_TO_LLVM}/lib/cmake/llvm
        ${PATH_TO_LLVM}/lib/cmake/clang
        ${PATH_TO_LLVM}/share/clang/cmake/
        ${PATH_TO_LLVM}/share/llvm/cmake/
        )

message(${search_paths})

find_package(LLVM REQUIRED CONFIG
        PATHS ${search_paths}
        NO_DEFAULT_PATH)

set(CMAKE_CXX_STANDARD 20)

include_directories(${PATH_TO_LLVM}/include)
link_directories(${PATH_TO_LLVM}/lib)

add_executable(compiler main.cpp)

target_link_libraries(compiler ${LLVM_LIBS} z)