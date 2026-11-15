# The desktop build: the C core, its tools and its tests. (The FM-1 firmware is built by fm1-base's tools/build.py:
# see README.md.)
cmake_minimum_required(VERSION 3.15)
project(mirage_fm1 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra)
endif()

set(CORE src/mirage/mirage.c src/mirage/mirage_engine.c src/mirage/packed_disk.c)

# single precision, as on the FM-1
add_library(mirage_core STATIC ${CORE})
target_include_directories(mirage_core PUBLIC src/mirage)
target_link_libraries(mirage_core PUBLIC m)

# double precision: mirage-remake's arithmetic, for the oracle
add_library(mirage_core_double STATIC ${CORE})
target_include_directories(mirage_core_double PUBLIC src/mirage)
target_compile_definitions(mirage_core_double PUBLIC MIRAGE_DOUBLE)
target_link_libraries(mirage_core_double PUBLIC m)

add_executable(mirage_cli tools/mirage_cli.c)
target_include_directories(mirage_cli PRIVATE tools)
target_link_libraries(mirage_cli PRIVATE mirage_core)

add_executable(mirage_core_test tests/core_test.c)
target_link_libraries(mirage_core_test PRIVATE mirage_core)

enable_testing()
set(ROM ${CMAKE_SOURCE_DIR}/data/rom/mirage.bin)
set(OS32 ${CMAKE_SOURCE_DIR}/data/disks/os32.img)
add_test(NAME core COMMAND mirage_core_test ${ROM} ${OS32})

find_package(Threads REQUIRED)
add_executable(mirage_split_test tests/split_test.c)
target_link_libraries(mirage_split_test PRIVATE mirage_core Threads::Threads)
add_test(NAME split COMMAND mirage_split_test ${ROM} ${OS32})

add_executable(mirage_bench tools/mirage_bench.c)
target_include_directories(mirage_bench PRIVATE tools)
target_link_libraries(mirage_bench PRIVATE mirage_core)

# the built-in disk: packed by tools/pack_disk.py, decoded by src/mirage/packed_disk.c
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/os32.pack
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/pack_disk.py ${OS32} ${CMAKE_BINARY_DIR}/os32.pack
    DEPENDS ${OS32} ${CMAKE_SOURCE_DIR}/tools/pack_disk.py)
  add_custom_target(os32_pack ALL DEPENDS ${CMAKE_BINARY_DIR}/os32.pack)
  add_executable(mirage_pack_test tests/pack_test.c)
  target_link_libraries(mirage_pack_test PRIVATE mirage_core)
  add_test(NAME pack COMMAND mirage_pack_test ${ROM} ${OS32} ${CMAKE_BINARY_DIR}/os32.pack)
endif()

# The oracle: this core against mirage-remake's C++ emulator, sample for sample.
#   cmake -DMIRAGE_REMAKE_DIR=/path/to/mirage-remake ...
set(MIRAGE_REMAKE_DIR "" CACHE PATH "a mirage-remake checkout, for the oracle test")
if(MIRAGE_REMAKE_DIR)
  set(CMAKE_CXX_STANDARD 17)
  set(R ${MIRAGE_REMAKE_DIR}/src)
  add_library(remake_emu STATIC ${R}/emu/Cpu6809.cpp ${R}/emu/Via6522.cpp ${R}/emu/Wd1772.cpp ${R}/emu/Es5503.cpp
              ${R}/emu/Mirage.cpp)
  target_include_directories(remake_emu PUBLIC ${R})
  target_compile_options(remake_emu PRIVATE -w)
  add_executable(mirage_oracle tests/oracle.cpp)
  target_link_libraries(mirage_oracle PRIVATE remake_emu mirage_core_double)
  add_test(NAME oracle COMMAND mirage_oracle ${ROM} ${OS32})
endif()
