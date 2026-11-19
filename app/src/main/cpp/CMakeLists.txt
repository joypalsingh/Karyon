cmake_minimum_required(VERSION 3.22.1)
project(karyon LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Reused platform-independent DEX parser & disassembler from Dexe
add_library(dexe_core STATIC
    dexe/dex/dex_file.cpp
    dexe/dex/disassembler.cpp
    dexe/dex/instruction.cpp
    dexe/util/checksum.cpp
    dexe/util/text.cpp
)
target_include_directories(dexe_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dexe)
target_compile_options(dexe_core PRIVATE -Wall -Wextra -fexceptions)

# Karyon host container supervisor, RootFS store, host capability probe, and JNI bridge
add_library(karyon_core SHARED
    karyon/axml_parser.cpp
    karyon/rootfs_store.cpp
    karyon/host_probe.cpp
    karyon/vm_supervisor.cpp
    karyon/jni_bridge.cpp
)
target_include_directories(karyon_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/karyon
    ${CMAKE_CURRENT_SOURCE_DIR}/dexe
)
target_compile_options(karyon_core PRIVATE -Wall -Wextra -fexceptions)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(karyon_core
    dexe_core
    ${log-lib}
    ${android-lib}
)

# Child supervisor / container bootstrapper packaged as libkaryon_init.so in nativeLibraryDir
# so fork()+execve() complies with Android 10+ W^X (writable filesDir cannot exec binaries).
add_executable(karyon_init
    karyon/karyon_init_main.cpp
)
set_target_properties(karyon_init PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "karyon_init"
    SUFFIX ".so"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)
target_compile_options(karyon_init PRIVATE -Wall -Wextra -fexceptions -fPIE)
target_link_options(karyon_init PRIVATE -pie)
target_link_libraries(karyon_init ${log-lib})
