# Read-only KTX2/Basis decoder. No Vulkan SDK, GL uploader, encoder or extra DLL.
# Source list and definitions follow KTX-Software v4.3.2's ktx_read target.
if(CMAKE_VERSION VERSION_LESS 3.18)
    message(FATAL_ERROR "Arena KTX2 requires CMake 3.18 or newer")
endif()
set(ARENA_KTX_SOURCE_DIR "" CACHE PATH "Offline KTX-Software v4.3.2 source directory")
if(ARENA_KTX_SOURCE_DIR)
    set(arena_ktx_SOURCE_DIR "${ARENA_KTX_SOURCE_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(arena_ktx
        URL https://github.com/KhronosGroup/KTX-Software/archive/91ace88675ac59a97e55d0378a6602a9ae6b98bd.tar.gz
        # Only fetch sources; avoid the upstream tools, tests and Bash requirement.
        SOURCE_SUBDIR arena-read-only-no-upstream-build)
    FetchContent_MakeAvailable(arena_ktx)
endif()
set(_arena_ktx_sources
    lib/basis_transcode.cpp
    lib/miniz_wrapper.cpp
    lib/basisu/transcoder/basisu_transcoder.cpp
    lib/basisu/zstd/zstddeclib.c
    lib/checkheader.c
    lib/dfdutils/createdfd.c
    lib/dfdutils/colourspaces.c
    lib/dfdutils/interpretdfd.c
    lib/dfdutils/printdfd.c
    lib/dfdutils/queries.c
    lib/dfdutils/vk2dfd.c
    lib/etcdec.cxx
    lib/etcunpack.cxx
    lib/filestream.c
    lib/hashlist.c
    lib/info.c
    lib/memstream.c
    lib/strings.c
    lib/swap.c
    lib/texture.c
    lib/texture1.c
    lib/texture2.c
    lib/vkformat_check.c
    lib/vkformat_str.c
    lib/vkformat_typesize.c)
list(TRANSFORM _arena_ktx_sources PREPEND "${arena_ktx_SOURCE_DIR}/")
add_library(arena_ktx_read STATIC ${_arena_ktx_sources})
set_target_properties(arena_ktx_read PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(arena_ktx_read PRIVATE c_std_99 cxx_std_11)
target_compile_definitions(arena_ktx_read PUBLIC KHRONOS_STATIC
    PRIVATE LIBKTX KTX_FEATURE_KTX2=1 KTX_FEATURE_KTX1=1 KTX_FEATURE_WRITE=0
    KTX_OMIT_VULKAN=1 KTX_OMIT_OPENGL=1 BASISD_SUPPORT_KTX2=0
    BASISD_SUPPORT_KTX2_ZSTD=0 BASISD_SUPPORT_FXT1=0)
target_include_directories(arena_ktx_read PUBLIC "${arena_ktx_SOURCE_DIR}/include"
    PRIVATE "${arena_ktx_SOURCE_DIR}/lib/basisu/transcoder"
    "${arena_ktx_SOURCE_DIR}/lib/basisu/zstd" "${arena_ktx_SOURCE_DIR}/utils")
target_include_directories(arena_ktx_read SYSTEM PRIVATE "${arena_ktx_SOURCE_DIR}/other_include")
if(WIN32)
    target_compile_definitions(arena_ktx_read PUBLIC BASISU_NO_ITERATOR_DEBUG_LEVEL)
endif()
find_package(Threads REQUIRED)
target_link_libraries(arena_ktx_read PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

install(FILES "${arena_ktx_SOURCE_DIR}/LICENSE.md" "${arena_ktx_SOURCE_DIR}/NOTICE.md"
    DESTINATION "licenses/ktx")
install(DIRECTORY "${arena_ktx_SOURCE_DIR}/LICENSES/" DESTINATION "licenses/ktx/LICENSES")
