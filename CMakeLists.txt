cmake_minimum_required(VERSION 3.21)

function(resolve_devkitpro_root OUT_VAR)
    set(RESOLVED_DEVKITPRO_ROOT "")
    if(DEFINED ENV{DEVKITPRO} AND NOT "$ENV{DEVKITPRO}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{DEVKITPRO}" RESOLVED_DEVKITPRO_ROOT)
    elseif(DEFINED DEVKITPRO_ROOT AND NOT "${DEVKITPRO_ROOT}" STREQUAL "")
        set(RESOLVED_DEVKITPRO_ROOT "${DEVKITPRO_ROOT}")
    elseif(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        get_filename_component(DEVKITPRO_TOOLCHAIN_PATH "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        get_filename_component(DEVKITPRO_TOOLCHAIN_DIR "${DEVKITPRO_TOOLCHAIN_PATH}" DIRECTORY)
        get_filename_component(DEVKITPRO_TOOLCHAIN_PARENT "${DEVKITPRO_TOOLCHAIN_DIR}" NAME)
        if(DEVKITPRO_TOOLCHAIN_PARENT STREQUAL "cmake")
            get_filename_component(RESOLVED_DEVKITPRO_ROOT "${DEVKITPRO_TOOLCHAIN_DIR}" DIRECTORY)
        endif()
    endif()

    if(RESOLVED_DEVKITPRO_ROOT STREQUAL "")
        message(FATAL_ERROR "Please define DEVKITPRO to point to your devkitPro path or set CMAKE_TOOLCHAIN_FILE explicitly.")
    endif()

    set(DEVKITPRO_ROOT "${RESOLVED_DEVKITPRO_ROOT}" CACHE PATH "devkitPro root directory")
    set(${OUT_VAR} "${RESOLVED_DEVKITPRO_ROOT}" PARENT_SCOPE)
endfunction()

function(select_devkitpro_toolchain)
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        return()
    endif()

    resolve_devkitpro_root(RESOLVED_DEVKITPRO_ROOT)
    foreach(TOOLCHAIN_RELATIVE_PATH IN LISTS ARGN)
        set(TOOLCHAIN_CANDIDATE "${RESOLVED_DEVKITPRO_ROOT}/${TOOLCHAIN_RELATIVE_PATH}")
        if(EXISTS "${TOOLCHAIN_CANDIDATE}")
            set(CMAKE_TOOLCHAIN_FILE "${TOOLCHAIN_CANDIDATE}" CACHE PATH "toolchain file" FORCE)
            return()
        endif()
    endforeach()

    message(FATAL_ERROR "Unable to find a devkitPro CMake toolchain under ${RESOLVED_DEVKITPRO_ROOT}. Set CMAKE_TOOLCHAIN_FILE explicitly.")
endfunction()

if(PLATFORM STREQUAL "3ds")
    select_devkitpro_toolchain(cmake/3DS.cmake)
endif()
if(PLATFORM STREQUAL "wii")
    select_devkitpro_toolchain(cmake/Wii.cmake)
endif()

if(PLATFORM STREQUAL "ps3")
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/ppu.cmake)
endif()

if(PLATFORM STREQUAL "vita")
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
      if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
      else()
        message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
      endif()
    endif()

    set(BACKEND "sdl2")

elseif(PLATFORM STREQUAL "switch")
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        if(NOT DEFINED ENV{DEVKITPRO})
            message(FATAL_ERROR "Please define DEVKITPRO to point to your SDK path!")
        endif()
        set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake" CACHE PATH "toolchain file")
    endif()

    set(BACKEND "sdl2")

elseif(PLATFORM STREQUAL "webos")
    set(BACKEND "sdl2")
endif()

project(butterscotch C)

if (PLATFORM STREQUAL "vita")
    include("${VITASDK}/share/vita.cmake" REQUIRED)
    set(VITA_APP_NAME "Butterscotch")
    set(VITA_TITLEID  "BUTTRSTCH")
    set(VITA_VERSION  "01.00")
    set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
    if(WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wvla)
    if(WERROR)
        add_compile_options(-Werror)
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CheckLinkerFlag)
    
    if(MSVC)
        # equivalent to -fomit-frame-pointer
        add_compile_options(/Oy)

        # equivalent to -Wl,--icf=all
        check_linker_flag(C "/OPT:ICF" HAS_MSVC_ICF)
        if(HAS_MSVC_ICF)
            add_link_options("$<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>:/OPT:ICF>")
        endif()
    else()
        add_compile_options(-ffast-math -fno-finite-math-only)

        # some targets don't support -fomit-frame-pointer IIRC
        include(CheckCCompilerFlag)
        check_c_compiler_flag("-fomit-frame-pointer" HAS_FOMIT_FRAME_POINTER)
        if(HAS_FOMIT_FRAME_POINTER)
            add_compile_options(-fomit-frame-pointer)
        endif()

        # bfd sucks ass but this saves a couple kbs of binary size on linkers that do support this
        check_linker_flag(C "-Wl,--icf=all" HAS_ICF_ALL)
        if(HAS_ICF_ALL)
            add_link_options("-Wl,--icf=all")
        endif()
    endif()
endif()

set(PLATFORM "cli" CACHE STRING "Platform backend")
set(BACKEND "" CACHE STRING "Desktop platform backend")
set(AUDIO_BACKEND "" CACHE STRING "Audio backend")

if(PLATFORM STREQUAL "cli" AND BACKEND STREQUAL "")
    message(FATAL_ERROR "You need to set a BACKEND!")
endif()

# Build metadata
set(BUTTERSCOTCH_COMMIT_HASH "unknown" CACHE STRING "Git commit hash")
set(BUTTERSCOTCH_COMMIT_DATE "unknown" CACHE STRING "Git commit date")
add_compile_definitions(
    BUTTERSCOTCH_COMMIT_HASH="${BUTTERSCOTCH_COMMIT_HASH}"
    BUTTERSCOTCH_COMMIT_DATE="${BUTTERSCOTCH_COMMIT_DATE}"
)

# Audio backend defaults
if(AUDIO_BACKEND STREQUAL "")
    if(PLATFORM STREQUAL "ps3" OR PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch")
        set(AUDIO_BACKEND "openal")
    elseif(PLATFORM STREQUAL "ps2")
        set(AUDIO_BACKEND "ps2")
    elseif(PLATFORM STREQUAL "3ds" OR PLATFORM STREQUAL "wii")
        set(AUDIO_BACKEND "${PLATFORM}")
    else()
        set(AUDIO_BACKEND "miniaudio")
    endif()
endif()

if(AUDIO_BACKEND STREQUAL "openal")
    file(GLOB AUDIO_SOURCES src/audio/openal/*.c)
elseif(AUDIO_BACKEND STREQUAL "miniaudio")
    file(GLOB AUDIO_SOURCES src/audio/miniaudio/*.c)
    if(PLATFORM STREQUAL "vita")
        add_compile_definitions(MA_NO_RUNTIME_LINKING)
    endif()
elseif(AUDIO_BACKEND STREQUAL "web")
    file(GLOB AUDIO_SOURCES src/audio/web/*.c src/audio/miniaudio/*.c)
elseif(AUDIO_BACKEND STREQUAL "ps2")
    file(GLOB AUDIO_SOURCES src/audio/ps2/*.c)
elseif(AUDIO_BACKEND STREQUAL "3ds" OR AUDIO_BACKEND STREQUAL "wii")
    file(GLOB AUDIO_SOURCES CONFIGURE_DEPENDS src/audio/${AUDIO_BACKEND}/*.c)
elseif(NOT AUDIO_BACKEND STREQUAL "none")
    message(FATAL_ERROR "Invalid AUDIO_BACKEND!")
endif()

# To avoid unused code in production builds that are only used for a single game (example: PlayStation 2 target), we can enable/disable specific features
#
# If only a single WAD version is enabled, the checks will be collapsed and removed by the compiler during build time, reducing code size and improving
# performance on low end hardware!
option(ENABLE_WAD14 "Enable support for WAD Version 14 and below" ON)
option(ENABLE_WAD16 "Enable support for WAD Version 15/16" ON)
option(ENABLE_WAD17 "Enable support for WAD Version 17" ON)
option(ENABLE_LEGACY_GL "Enable the legacy OpenGL renderer" ON)
if(PLATFORM STREQUAL "switch" OR PLATFORM STREQUAL "webos" OR PLATFORM STREQUAL "web")
    set(ENABLE_LEGACY_GL OFF CACHE BOOL "Enable the legacy OpenGL renderer" FORCE)
endif()
option(ENABLE_MODERN_GL "Enable the modern OpenGL renderer" ON)
option(ENABLE_NOOP_RENDERER "Enable the no-op renderer (headless stub, no drawing)" OFF)
option(ENABLE_PHYSICS "Enable Box2D physics support" ON)
option(ENABLE_FMOD "Enable standalone FMOD bank/event compatibility" ON)
# Noop renderer is exclusive to the noop desktop backend (no window, no GL).
# Auto-enable for BACKEND=noop and reject otherwise.
if(BACKEND STREQUAL "noop")
    set(ENABLE_NOOP_RENDERER ON CACHE BOOL "Enable the no-op renderer (headless stub, no drawing)" FORCE)
    set(ENABLE_LEGACY_GL OFF CACHE BOOL "Enable the legacy OpenGL renderer" FORCE)
    set(ENABLE_MODERN_GL OFF CACHE BOOL "Enable the modern OpenGL renderer" FORCE)
endif()
if(ENABLE_NOOP_RENDERER AND NOT BACKEND STREQUAL "noop")
    message(FATAL_ERROR "ENABLE_NOOP_RENDERER is exclusive to BACKEND=noop")
endif()
if(NOT ENABLE_WAD14 AND NOT ENABLE_WAD16 AND NOT ENABLE_WAD17)
    message(FATAL_ERROR "You need to build Butterscotch with at least one WAD version enabled!")
endif()
if(ENABLE_WAD14)
    add_compile_definitions(ENABLE_WAD14)
endif()
if(ENABLE_WAD16)
    add_compile_definitions(ENABLE_WAD16)
endif()
if(ENABLE_WAD17)
    add_compile_definitions(ENABLE_WAD17)
endif()
if(ENABLE_LEGACY_GL)
    add_compile_definitions(ENABLE_LEGACY_GL)
endif()
if(ENABLE_MODERN_GL)
    add_compile_definitions(ENABLE_MODERN_GL)
endif()
if(ENABLE_NOOP_RENDERER)
    add_compile_definitions(ENABLE_NOOP_RENDERER)
endif()
if(ENABLE_PHYSICS)
    add_compile_definitions(ENABLE_PHYSICS)
endif()
if(ENABLE_FMOD)
    add_compile_definitions(ENABLE_FMOD)
endif()

file(GLOB SOURCES src/*.c)
file(GLOB DEFAULT_FONT_SOURCES src/default_font/*.c)
if(ENABLE_FMOD)
    list(APPEND SOURCES src/fmod/fmod.c src/fmod/fmod_bank.c src/fmod/fsb.c src/fmod/fmod_dsp.c)
endif()
if(ENABLE_PHYSICS)
    file(GLOB PHYSICS_SOURCES CONFIGURE_DEPENDS src/physics/*.c)
else()
    set(PHYSICS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/physics.c"
                        "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/disabled/physics_disabled.c")
endif()
list(APPEND SOURCES ${PHYSICS_SOURCES})

# These platforms haven't been converted to the refactored system yet.
# This list should NEVER grow, all new platforms should use the new system.
if(PLATFORM STREQUAL "ps2" OR PLATFORM STREQUAL "ps3" OR PLATFORM STREQUAL "android" OR PLATFORM STREQUAL "3ds" OR PLATFORM STREQUAL "wii")
    list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/loop.c")
endif()

# Platform specific files builds
file(GLOB PLATFORM_SOURCES src/${PLATFORM}/*.c)

if(PLATFORM STREQUAL "android")
    add_library(butterscotch SHARED ${SOURCES} ${PLATFORM_SOURCES} ${AUDIO_SOURCES} ${DEFAULT_FONT_SOURCES})
else()
    add_executable(butterscotch ${SOURCES} ${PLATFORM_SOURCES} ${AUDIO_SOURCES} ${DEFAULT_FONT_SOURCES})
endif()

# Nintendo backends own their event loop, native textures, and streamed audio.
if(PLATFORM STREQUAL "3ds" OR PLATFORM STREQUAL "wii")
    target_sources(butterscotch PRIVATE
        src/lz11/lz11.c
        src/audio/common/sound_bank.c
        src/audio/common/vorbis_probe.c
        src/audio/common/streamed_audio.c
    )
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
    # These SDKs do not provide FFmpeg; avoid finding host libraries when cross compiling.
    set(VIDEO_BACKEND "none" CACHE STRING "Video playback backend (ffmpeg or none)")
endif()

# Video playback backend
set(VIDEO_BACKEND "ffmpeg" CACHE STRING "Video playback backend (ffmpeg or none)")
set(BUTTERSCOTCH_FFMPEG_STATIC_LIBS "")

if(NOT VIDEO_BACKEND STREQUAL "ffmpeg" AND NOT VIDEO_BACKEND STREQUAL "none")
    message(FATAL_ERROR "Invalid VIDEO_BACKEND: ${VIDEO_BACKEND}")
endif()

if(VIDEO_BACKEND STREQUAL "ffmpeg")
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        if(PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch")
            # Console SDKs only ship static FFmpeg archives, and the private
            # dependency closure (mpg123, lame, zlib, ...) is not exposed by a
            # plain --libs query. Ask for the static closure and link it as a
            # single, normalised list so each archive is passed exactly once.
            execute_process(
                COMMAND ${PKG_CONFIG_EXECUTABLE} --static --libs libavformat libavcodec libavutil libswscale libswresample
                OUTPUT_VARIABLE _ffmpeg_static_libs
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _ffmpeg_static_rc)
            execute_process(
                COMMAND ${PKG_CONFIG_EXECUTABLE} --static --cflags libavformat libavcodec libavutil libswscale libswresample
                OUTPUT_VARIABLE _ffmpeg_static_cflags
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _ffmpeg_static_cflags_rc)
            if(_ffmpeg_static_rc EQUAL 0 AND NOT _ffmpeg_static_libs STREQUAL "")
                add_compile_definitions(BUTTERSCOTCH_FFMPEG)
                set(BUTTERSCOTCH_FFMPEG 1)
                separate_arguments(_ffmpeg_static_libs UNIX_COMMAND "${_ffmpeg_static_libs}")
                foreach(_lib IN LISTS _ffmpeg_static_libs)
                    if(_lib MATCHES "^-l(.+)$")
                        list(APPEND BUTTERSCOTCH_FFMPEG_STATIC_LIBS "${CMAKE_MATCH_1}")
                    elseif(_lib STREQUAL "-pthread")
                        list(APPEND BUTTERSCOTCH_FFMPEG_STATIC_LIBS pthread)
                    else()
                        list(APPEND BUTTERSCOTCH_FFMPEG_STATIC_LIBS "${_lib}")
                    endif()
                endforeach()
                if(NOT _ffmpeg_static_cflags_rc EQUAL 0)
                    set(_ffmpeg_static_cflags "")
                endif()
                separate_arguments(_ffmpeg_static_cflags UNIX_COMMAND "${_ffmpeg_static_cflags}")
                target_compile_options(butterscotch PRIVATE ${_ffmpeg_static_cflags})
            else()
                message(WARNING "FFmpeg development libraries not found - video playback will be disabled")
                add_compile_definitions(BUTTERSCOTCH_VIDEO_NULL)
                set(BUTTERSCOTCH_VIDEO_NULL 1)
            endif()
        else()
            pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
            if(FFMPEG_FOUND)
                add_compile_definitions(BUTTERSCOTCH_FFMPEG)
                set(BUTTERSCOTCH_FFMPEG 1)
                target_link_libraries(butterscotch PRIVATE PkgConfig::FFMPEG)
            else()
                message(WARNING "FFmpeg development libraries not found - video playback will be disabled")
                add_compile_definitions(BUTTERSCOTCH_VIDEO_NULL)
                set(BUTTERSCOTCH_VIDEO_NULL 1)
            endif()
        endif()
    else()
        message(WARNING "pkg-config not found - video playback will be disabled")
        add_compile_definitions(BUTTERSCOTCH_VIDEO_NULL)
        set(BUTTERSCOTCH_VIDEO_NULL 1)
    endif()
else()
    # VIDEO_BACKEND=none
    add_compile_definitions(BUTTERSCOTCH_VIDEO_NULL)
    set(BUTTERSCOTCH_VIDEO_NULL 1)
endif()

target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(ENABLE_PHYSICS)
    file(GLOB BOX2D_SOURCES CONFIGURE_DEPENDS vendor/box2d/src/*.c)
    list(REMOVE_ITEM BOX2D_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/src/timer.c")
    add_library(butterscotch_box2d STATIC ${BOX2D_SOURCES})
    target_include_directories(butterscotch_box2d BEFORE PUBLIC vendor/box2d/include)
    target_include_directories(butterscotch_box2d PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_include_directories(butterscotch BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/vendor/box2d/include)
    target_compile_definitions(butterscotch_box2d PRIVATE BOX2D_DISABLE_SIMD B2_SINGLE_THREADED)
    if(NOT MSVC)
        target_compile_options(butterscotch_box2d PRIVATE -w -fno-fast-math -ffp-contract=off)
    endif()
    target_link_libraries(butterscotch PRIVATE butterscotch_box2d)
    if(NOT MSVC)
        set_source_files_properties(src/physics/physics_engine.c src/physics/physics_joints.c src/physics/physics_particles.c
            PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
    endif()
endif()
target_include_directories(butterscotch PRIVATE src/${PLATFORM})
target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/video)

if(AUDIO_BACKEND STREQUAL "openal")
    target_include_directories(butterscotch PUBLIC src/audio/openal)
    add_compile_definitions(USE_OPENAL)
    if(APPLE)
        set(AUDIO_LIBRARIES "-framework OpenAL")
    elseif(PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch")
        set(AUDIO_LIBRARIES openal pthread)
    elseif(NOT PLATFORM STREQUAL "ps3")
        find_package(OpenAL REQUIRED)
        target_include_directories(butterscotch PUBLIC "${OPENAL_INCLUDE_DIR}")
        set(AUDIO_LIBRARIES "${OPENAL_LIBRARY}")
    endif()
elseif(AUDIO_BACKEND STREQUAL "miniaudio")
    target_include_directories(butterscotch SYSTEM PUBLIC vendor/miniaudio)
    target_include_directories(butterscotch PUBLIC src/audio/miniaudio)
    add_compile_definitions(USE_MINIAUDIO)
    if (NOT WIN32)
        set(AUDIO_LIBRARIES pthread)
    endif()
elseif(AUDIO_BACKEND STREQUAL "web")
    target_include_directories(butterscotch SYSTEM PUBLIC vendor/miniaudio)
    target_include_directories(butterscotch PUBLIC src/audio/web)
    target_include_directories(butterscotch PUBLIC src/audio/miniaudio)
    add_compile_definitions(USE_WEB_AUDIO)
elseif(AUDIO_BACKEND STREQUAL "ps2")
    target_include_directories(butterscotch PUBLIC src/audio/ps2)
    add_compile_definitions(USE_PS2_AUDIO)
endif()

if(AUDIO_BACKEND STREQUAL "3ds")
    target_include_directories(butterscotch PUBLIC src/audio/3ds)
    add_compile_definitions(USE_3DS_AUDIO)
elseif(AUDIO_BACKEND STREQUAL "wii")
    target_include_directories(butterscotch PUBLIC src/audio/wii)
    add_compile_definitions(USE_WII_AUDIO)
endif()

# stb_ds
add_library(stb_ds INTERFACE)
target_include_directories(stb_ds SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/vendor/stb/ds)

# stb_vorbis
add_library(stb_vorbis INTERFACE)
target_include_directories(stb_vorbis SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/vendor/stb/vorbis)

# RFC 1321 reference MD5 implementation
target_sources(butterscotch PRIVATE vendor/md5/md5.c)
target_include_directories(butterscotch PUBLIC vendor/md5)

# Steve Reid's SHA1 implementation
add_library(sha1 STATIC vendor/sha1/sha1.c)
target_include_directories(sha1 SYSTEM PUBLIC vendor/sha1)
target_compile_options(sha1 PRIVATE -w)

# Base64 implementation
target_sources(butterscotch PRIVATE vendor/base64/base64.c)
target_include_directories(butterscotch PUBLIC vendor/base64)

# bzip2
add_library(bzip2 STATIC
    vendor/bzip2/bz_internal_error.c
    vendor/bzip2/bzlib.c
    vendor/bzip2/crctable.c
    vendor/bzip2/decompress.c
    vendor/bzip2/huffman.c
    vendor/bzip2/randtable.c
)
target_include_directories(bzip2 PUBLIC vendor/bzip2)
target_compile_options(bzip2 PRIVATE -w)

# miniz 3.1.2 (https://github.com/richgel999/miniz, tag 3.1.2, commit 77d0dce86277).
# All files vendored byte-for-byte from that tag except:
#  - miniz_export.h, which upstream generates during its own CMake configure; this
#    static build just leaves MINIZ_EXPORT and MINIZ_NO_EXPORT empty since the
#    archive/stdio APIs are disabled above.
#  - miniz.h's MZ_FORCEINLINE for _MSC_VER, changed from __forceinline to __inline
#    because the zlib-compat layer writes "static MZ_FORCEINLINE" and older MSVC C
#    rejects "static __forceinline".
target_sources(butterscotch PRIVATE
    vendor/miniz/miniz.c
    vendor/miniz/miniz_tdef.c
    vendor/miniz/miniz_tinfl.c
)
target_include_directories(butterscotch SYSTEM PUBLIC vendor/miniz)
target_compile_definitions(butterscotch PRIVATE MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO)
if(NOT MSVC)
    set_source_files_properties(
        vendor/miniz/miniz.c
        vendor/miniz/miniz_tdef.c
        vendor/miniz/miniz_tinfl.c
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

# Video playback source
if(BUTTERSCOTCH_FFMPEG)
    target_sources(butterscotch PRIVATE src/video/ffmpeg/ffmpeg.c)
elseif(BUTTERSCOTCH_VIDEO_NULL)
    target_sources(butterscotch PRIVATE src/video/null_video.c)
endif()

# Spatial grid logs
option(ENABLE_SPATIAL_GRID_LOGS "Enable Spatial Grid logs" OFF)
if(ENABLE_SPATIAL_GRID_LOGS)
    add_compile_definitions(ENABLE_SPATIAL_GRID_LOGS)
endif()

# VM Exceptions logs
option(ENABLE_VM_EXCEPTIONS_LOGS "Enable VM Exceptions logs" OFF)
if(ENABLE_VM_EXCEPTIONS_LOGS)
    add_compile_definitions(ENABLE_VM_EXCEPTIONS_LOGS)
endif()

if(PLATFORM STREQUAL "cli" OR PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch" OR PLATFORM STREQUAL "webos" OR PLATFORM STREQUAL "web")
    if(PLATFORM STREQUAL "webos")
        set(VM_GML_PROFILER_DEFAULT OFF)
        set(VM_TRACING_DEFAULT OFF)
        set(VM_OPCODE_PROFILER_DEFAULT OFF)
        set(VM_STUB_LOGS_DEFAULT OFF)
        set(ENABLE_FILE_PICKER OFF)
    elseif(PLATFORM STREQUAL "vita")
        # Normally, __VITA__ should already be defined, but for some reason,
        # in my case, this didn't really work... so just in case, when we're
        # building for the Vita, just force-feed these...
        add_compile_definitions(__VITA__)
        add_compile_definitions(PLATFORM_VITA)

        set(VM_GML_PROFILER_DEFAULT OFF)
        set(VM_TRACING_DEFAULT OFF)
        set(VM_OPCODE_PROFILER_DEFAULT OFF)
        set(VM_STUB_LOGS_DEFAULT OFF)
        set(ENABLE_FILE_PICKER OFF)
    elseif(PLATFORM STREQUAL "switch")
        add_compile_definitions(PLATFORM_SWITCH)

        set(VM_GML_PROFILER_DEFAULT OFF)
        set(VM_TRACING_DEFAULT OFF)
        set(VM_OPCODE_PROFILER_DEFAULT OFF)
        set(VM_STUB_LOGS_DEFAULT OFF)
        set(ENABLE_FILE_PICKER OFF)
    elseif(PLATFORM STREQUAL "web")
        add_compile_definitions(PLATFORM_WEB)
        target_sources(butterscotch PRIVATE src/cli/args.c)
    else()
        set(VM_GML_PROFILER_DEFAULT ON)
        set(VM_TRACING_DEFAULT ON)
        set(VM_OPCODE_PROFILER_DEFAULT ON)
        set(VM_STUB_LOGS_DEFAULT ON)
        set(ENABLE_FILE_PICKER ON)
    endif()
    # Matches the GitHub Actions container flags (FORTIFY requires building with optimizations enabled!)
    target_compile_options(butterscotch PRIVATE "$<$<CONFIG:Release,RelWithDebInfo>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=2>")

    if(NOT ENABLE_LEGACY_GL AND NOT ENABLE_MODERN_GL AND NOT ENABLE_NOOP_RENDERER)
        message(FATAL_ERROR "You must enable at least one renderer!")
    endif()
    file(GLOB GL_SOURCES src/image/*.c src/gl_common/*.c)
    if(ENABLE_LEGACY_GL)
        file(GLOB GL_SOURCES ${GL_SOURCES} src/gl_legacy/*.c)
        target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl_legacy)
    endif()
    if(ENABLE_MODERN_GL)
        file(GLOB GL_SOURCES ${GL_SOURCES} src/gl/*.c)
    endif()
    target_sources(butterscotch PRIVATE ${GL_SOURCES})
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl)
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl_common)
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/image)

    if (MSVC)
        target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/compat/getopt)
        add_compile_definitions(NO_STRTOK_R)
        add_compile_definitions(NO_STRCASECMP)
    endif()

    # Butterscotch VM/interpreter profiler
    option(ENABLE_VM_GML_PROFILER "Enable Butterscotch VM/interpreter profiler" ${VM_GML_PROFILER_DEFAULT})
    if(ENABLE_VM_GML_PROFILER)
        add_compile_definitions(ENABLE_VM_GML_PROFILER)
    endif()

    # Butterscotch VM/interpreter tracing
    option(ENABLE_VM_TRACING "Enable Butterscotch VM/interpreter tracing checks" ${VM_TRACING_DEFAULT})
    if(ENABLE_VM_TRACING)
        add_compile_definitions(ENABLE_VM_TRACING)
    endif()

    # Butterscotch VM opcode count profiler
    option(ENABLE_VM_OPCODE_PROFILER "Enable Butterscotch VM opcode count profiler" ${VM_OPCODE_PROFILER_DEFAULT})
    if(ENABLE_VM_OPCODE_PROFILER)
        add_compile_definitions(ENABLE_VM_OPCODE_PROFILER)
    endif()

    # VM unknown/stubbed function logs
    option(ENABLE_VM_STUB_LOGS "Enable VM unknown/stubbed function logs" ${VM_STUB_LOGS_DEFAULT})
    if(ENABLE_VM_STUB_LOGS)
        add_compile_definitions(ENABLE_VM_STUB_LOGS)
    endif()

    # File Picker
    option(ENABLE_FILE_PICKER "Enable file picker" ${VM_STUB_LOGS_DEFAULT})
    if(ENABLE_FILE_PICKER)
        add_compile_definitions(ENABLE_FILE_PICKER)
    endif()

    # stb_image
    target_include_directories(butterscotch PUBLIC vendor/stb/image)

    if (ENABLE_FILE_PICKER)
        # tinyfd
        target_sources(butterscotch PRIVATE vendor/tinyfd/tinyfiledialogs.c)
        target_include_directories(butterscotch PUBLIC vendor/tinyfiledialogs)
    endif()

    set(PLATFORM_LIBRARIES bzip2 stb_ds sha1 stb_vorbis)
    if(NOT PLATFORM STREQUAL "vita" AND NOT PLATFORM STREQUAL "switch")
        # GLAD
        add_library(glad STATIC vendor/glad/src/glad.c)
        target_include_directories(glad PUBLIC vendor/glad/include)
        target_compile_options(glad PRIVATE -Wno-unused-but-set-variable)
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} glad)
    endif()
    if(NOT MSVC)
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} m)
    endif()
    if(WIN32)
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} opengl32 gdi32 winmm)
    elseif(NOT PLATFORM STREQUAL "vita" AND NOT PLATFORM STREQUAL "switch")
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} ${CMAKE_DL_LIBS})
    endif()

    if(BACKEND STREQUAL "glfw3")
        add_compile_definitions(USE_GLFW3)

        if(MINGW)
            find_package(glfw3 CONFIG REQUIRED)

            # Target Windows 7+ (avoids referencing newer APIs like MapViewOfFileNuma2 that MinGW import libs don't provide)
            # https://github.com/mirror/mingw-w64/blob/master/mingw-w64-headers/include/sdkddkver.h
            target_compile_definitions(butterscotch PRIVATE _WIN32_WINNT=0x0601 NTDDI_VERSION=0x06010000 WIN32_LEAN_AND_MEAN)
            target_link_options(butterscotch PRIVATE -static)
            set(BACKEND_LIBRARIES glfw opengl32 gdi32 winmm)
        elseif(MSVC)
            target_compile_definitions(butterscotch PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
            find_package(glfw3 CONFIG REQUIRED)
            set(BACKEND_LIBRARIES glfw opengl32 gdi32 winmm)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(GLFW3 REQUIRED glfw3)
            target_include_directories(butterscotch PRIVATE ${GLFW3_INCLUDE_DIRS})
            target_link_directories(butterscotch PRIVATE ${GLFW3_LIBRARY_DIRS})
            set(BACKEND_LIBRARIES ${GLFW3_LIBRARIES})
        endif()
    elseif(BACKEND STREQUAL "glfw2")
        add_compile_definitions(USE_GLFW2)

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW2 REQUIRED libglfw)
        target_include_directories(butterscotch PRIVATE ${GLFW2_INCLUDE_DIRS})
        target_link_directories(butterscotch PRIVATE ${GLFW2_LIBRARY_DIRS})
        set(BACKEND_LIBRARIES ${GLFW2_LIBRARIES})
    elseif(BACKEND STREQUAL "sdl1")
        add_compile_definitions(USE_SDL1)

        find_package(SDL REQUIRED)
        target_include_directories(butterscotch PUBLIC ${SDL_INCLUDE_DIR})
        set(BACKEND_LIBRARIES ${SDL_LIBRARY})
    elseif(BACKEND STREQUAL "sdl2")
        add_compile_definitions(USE_SDL2)

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2)
        target_include_directories(butterscotch PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_directories(butterscotch PRIVATE ${SDL2_LIBRARY_DIRS})
        set(BACKEND_LIBRARIES ${SDL2_LIBRARIES})
    elseif(BACKEND STREQUAL "sdl3")
        add_compile_definitions(USE_SDL3)

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3 REQUIRED sdl3)
        target_include_directories(butterscotch PRIVATE ${SDL3_INCLUDE_DIRS})
        target_link_directories(butterscotch PRIVATE ${SDL3_LIBRARY_DIRS})
        set(BACKEND_LIBRARIES ${SDL3_LIBRARIES})
    elseif(BACKEND STREQUAL "appkit")
        add_compile_definitions(USE_APPKIT)
        target_compile_definitions(butterscotch PRIVATE GL_SILENCE_DEPRECATION)
        set(BACKEND_LIBRARIES "-framework Cocoa" "-framework GameController")
    elseif(BACKEND STREQUAL "noop")
        if(NOT ENABLE_NOOP_RENDERER)
            message(FATAL_ERROR "BACKEND=noop requires ENABLE_NOOP_RENDERER=ON")
        endif()
        add_compile_definitions(USE_NOOP)
        set(BACKEND_LIBRARIES "")
    endif()
    if (PLATFORM STREQUAL "vita")
        set(BUTTERSCOTCH_CONSOLE_LIBS
            ${BACKEND_LIBRARIES}
            ${AUDIO_LIBRARIES}
            ${PLATFORM_LIBRARIES}
            vitaGL
            stdc++
            SceShaccCgExt
            taihen_stub
            SceShaccCg_stub
        )
    elseif (PLATFORM STREQUAL "switch")
        set(BUTTERSCOTCH_CONSOLE_LIBS
            ${BACKEND_LIBRARIES}
            ${AUDIO_LIBRARIES}
            ${PLATFORM_LIBRARIES}
            GLESv2
            stdc++
        )
    else()
        set(BUTTERSCOTCH_CONSOLE_LIBS ${BACKEND_LIBRARIES} ${AUDIO_LIBRARIES} ${PLATFORM_LIBRARIES})
    endif()

    # Console SDKs link pre-built static archives alongside the platform
    # libraries. Deduplicate the combined list so each archive is passed once:
    # without this, LTO + duplicated libpthread resolves the same archive
    # member twice and fails with a multiple definition error.
    if (BUTTERSCOTCH_CONSOLE_LIBS)
        list(REMOVE_DUPLICATES BUTTERSCOTCH_CONSOLE_LIBS)
    endif()
    target_link_libraries(butterscotch PRIVATE ${BUTTERSCOTCH_CONSOLE_LIBS})

    # FFmpeg's pkg-config files list external codecs as raw -l flags, so the
    # transitive dependencies of those codecs (e.g. libmp3lame -> libmpg123)
    # are not expanded by --static. Add the known gap and rescan the whole
    # closure through a linker group so static ordering/cycles resolve no
    # matter where each archive appears.
    if (BUTTERSCOTCH_FFMPEG_STATIC_LIBS)
        find_library(BUTTERSCOTCH_MPG123_LIBRARY NAMES mpg123 libmpg123)
        if (BUTTERSCOTCH_MPG123_LIBRARY)
            list(APPEND BUTTERSCOTCH_FFMPEG_STATIC_LIBS mpg123)
        endif()
        list(REMOVE_DUPLICATES BUTTERSCOTCH_FFMPEG_STATIC_LIBS)
        target_link_libraries(butterscotch PRIVATE
            "-Wl,--start-group"
            ${BUTTERSCOTCH_FFMPEG_STATIC_LIBS}
            "-Wl,--end-group"
        )
    endif()

    # Duplicate system archives pulled in by both the platform and FFmpeg
    # pkg-config closures are harmless but rejected by the LTO plugin.
    if(CMAKE_INTERPROCEDURAL_OPTIMIZATION AND (PLATFORM STREQUAL "vita" OR PLATFORM STREQUAL "switch"))
        target_link_options(butterscotch PRIVATE "-Wl,--allow-multiple-definition")
    endif()

    if (PLATFORM STREQUAL "vita")
        vita_create_self(eboot.bin butterscotch UNSAFE)
        vita_create_vpk(Butterscotch.vpk ${VITA_TITLEID} eboot.bin
            VERSION ${VITA_VERSION}
            NAME ${VITA_APP_NAME}
        )
    elseif (PLATFORM STREQUAL "switch")
        nx_generate_nacp(OUTPUT Butterscotch.nacp NAME "Butterscotch" AUTHOR "Butterscotch" VERSION "1.0.0")
        set(ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
        if (EXISTS "${ROMFS_DIR}" AND IS_DIRECTORY "${ROMFS_DIR}")
            nx_create_nro(butterscotch NACP Butterscotch.nacp ROMFS "${ROMFS_DIR}")
        else()
            nx_create_nro(butterscotch NACP Butterscotch.nacp)
        endif()
    elseif (PLATFORM STREQUAL "web")
        set_target_properties(butterscotch PROPERTIES SUFFIX ".mjs")

        target_link_options(
                butterscotch
                PRIVATE
                "-sMODULARIZE=1"
                "-sEXPORT_ES6=1"
                "-sENVIRONMENT=web"
                "-sWASMFS=1"
                "-sEXPORTED_RUNTIME_METHODS=['HEAP8', 'HEAPU8', 'HEAPF32', 'ccall', 'cwrap', 'specialHTMLTargets', 'UTF8ToString', 'stringToNewUTF8', 'setValue', 'FS']"
                "-sEXPORTED_FUNCTIONS=['_malloc', '_free', '_startRunner', '_mountOpfs', '_parseDataWin', '_getGameName', '_getGameDisplayName', '_freeDataWin', '_onKeyDown', '_onKeyUp', '_onMouseDown', '_onMouseUp', '_onMouseMove', '_requestRunnerExit']"
                "-sMIN_WEBGL_VERSION=2"
                "-sMAX_WEBGL_VERSION=2"
                "-sFULL_ES3=1"
                "-sALLOW_MEMORY_GROWTH"
                "-sINVOKE_RUN=0"
                "-sJSPI"
                "-sJSPI_EXPORTS=['startRunner', 'parseDataWin', 'mountOpfs']"
                # Useful to know when a stack overflow (that is, the WASM stack) happens
                "-sSTACK_OVERFLOW_CHECK=2"
                # Increases the stack size, needed because for some reason MiniAudio causes a stack overflow in DELTARUNE
                # "Aborted(stack overflow (Attempt to set SP to 0xfffff640, with stack limits [0x00000000 - 0x00010000]). If you require more stack space build with -sSTACK_SIZE=<bytes>)"
                "-sSTACK_SIZE=1MB"
                # We need this if we want to access "FS" from JavaScript (Module.FS.writeFile)
                "-sFORCE_FILESYSTEM=1"
                # "explicitSwapControl" and "renderViaOffscreenBackBuffer", see the web.c file
                "-sOFFSCREEN_FRAMEBUFFER=1"
        )
    endif()
    if(BACKEND STREQUAL "appkit")
        target_sources(butterscotch PRIVATE src/backends/${BACKEND}.m)
    else()
        target_sources(butterscotch PRIVATE src/backends/${BACKEND}.c)
    endif()

    # Enable AddressSanitizer by default in Debug builds
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ENABLE_ASAN_DEFAULT ON)
    else()
        set(ENABLE_ASAN_DEFAULT OFF)
    endif()
    set(ENABLE_ASAN ${ENABLE_ASAN_DEFAULT} CACHE BOOL "Enable AddressSanitizer")
    if(ENABLE_ASAN AND NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_compile_options(butterscotch PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(butterscotch PRIVATE -fsanitize=address)
    endif()

    #gamecontrollerdb
    add_custom_command(TARGET butterscotch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/vendor/gamecontrollerdb.txt
            $<TARGET_FILE_DIR:butterscotch>/gamecontrollerdb.txt
    )
elseif(PLATFORM STREQUAL "android")
    if (NOT ENABLE_MODERN_GL)
        message(FATAL_ERROR "Android requires modern gl!")
    endif()
    file(GLOB GL_SOURCES src/gl/*.c src/gl_common/*.c src/image/*.c)
    target_sources(butterscotch PRIVATE ${GL_SOURCES})
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl)
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl_common)
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/image)
    add_compile_definitions(ENABLE_GLES PLATFORM_ANDROID)

    # Butterscotch VM/interpreter profiler
    option(ENABLE_VM_GML_PROFILER "Enable Butterscotch VM/interpreter profiler" ON)
    if(ENABLE_VM_GML_PROFILER)
        add_compile_definitions(ENABLE_VM_GML_PROFILER)
    endif()

    # Butterscotch VM/interpreter tracing
    option(ENABLE_VM_TRACING "Enable Butterscotch VM/interpreter tracing checks" ON)
    if(ENABLE_VM_TRACING)
        add_compile_definitions(ENABLE_VM_TRACING)
    endif()

    # Butterscotch VM opcode count profiler
    option(ENABLE_VM_OPCODE_PROFILER "Enable Butterscotch VM opcode count profiler" OFF)
    if(ENABLE_VM_OPCODE_PROFILER)
        add_compile_definitions(ENABLE_VM_OPCODE_PROFILER)
    endif()

    # Spatial grid logs
    option(ENABLE_SPATIAL_GRID_LOGS "Enable Spatial Grid logs" OFF)
    if(ENABLE_SPATIAL_GRID_LOGS)
        add_compile_definitions(ENABLE_SPATIAL_GRID_LOGS)
    endif()

    # VM unknown/stubbed function logs
    option(ENABLE_VM_STUB_LOGS "Enable VM unknown/stubbed function logs" ON)
    if(ENABLE_VM_STUB_LOGS)
        add_compile_definitions(ENABLE_VM_STUB_LOGS)
    endif()

    # stb_image
    target_include_directories(butterscotch PUBLIC vendor/stb/image)
    # stb_vorbis (for miniaudio's OGG decoding)
    target_include_directories(butterscotch PUBLIC vendor/stb/vorbis)
    # bzip2
    target_link_libraries(butterscotch PRIVATE bzip2)

    # Android NDK system libraries:
    # - log: __android_log_*
    # - GLESv3: OpenGL ES 3.0 functions
    # - OpenSLES: miniaudio's OpenSL ES backend (it dlopen's libaaudio.so at runtime when available, so no link-time aaudio dep)
    target_link_libraries(butterscotch PRIVATE log GLESv3 OpenSLES bzip2 stb_ds sha1 stb_vorbis)
elseif(PLATFORM STREQUAL "ps2")
    # The PS2 EE has a single-precision FPU only, so double is software-emulated
    add_compile_definitions(USE_FLOAT_REALS)
    # By disabling Int64 support, we can fit the entire RValue struct in 8 bytes, improving performance and memory usage
    add_compile_definitions(NO_RVALUE_INT64)

    # Butterscotch VM/interpreter profiler
    option(ENABLE_VM_GML_PROFILER "Enable Butterscotch VM/interpreter profiler" OFF)
    if(ENABLE_VM_GML_PROFILER)
        add_compile_definitions(ENABLE_VM_GML_PROFILER)
    endif()

    # Butterscotch VM/interpreter tracing
    option(ENABLE_VM_TRACING "Enable Butterscotch VM/interpreter tracing checks" OFF)
    if(ENABLE_VM_TRACING)
        add_compile_definitions(ENABLE_VM_TRACING)
    endif()

    # Butterscotch VM opcode count profiler
    option(ENABLE_VM_OPCODE_PROFILER "Enable Butterscotch VM opcode count profiler" OFF)
    if(ENABLE_VM_OPCODE_PROFILER)
        add_compile_definitions(ENABLE_VM_OPCODE_PROFILER)
    endif()

    # Spatial grid logs
    option(ENABLE_SPATIAL_GRID_LOGS "Enable Spatial Grid logs" OFF)
    if(ENABLE_SPATIAL_GRID_LOGS)
        add_compile_definitions(ENABLE_SPATIAL_GRID_LOGS)
    endif()

    # VM unknown/stubbed function logs
    option(ENABLE_VM_STUB_LOGS "Enable VM unknown/stubbed function logs" OFF)
    if(ENABLE_VM_STUB_LOGS)
        add_compile_definitions(ENABLE_VM_STUB_LOGS)
    endif()

    # Platform identifier for PS2-specific code paths (e.g., scratchpad allocation)
    add_compile_definitions(PLATFORM_PS2)

    # PS2 renderer hot path logs
    option(ENABLE_PS2_RENDERER_LOGS "Enable hot path logs in the PS2 renderer" OFF)
    if(ENABLE_PS2_RENDERER_LOGS)
        add_compile_definitions(ENABLE_PS2_RENDERER_LOGS)
    endif()

    # -fsingle-precision-constant: Treat unsuffixed float constants as single-precision to avoid accidental  float->double promotions on platforms with no hardware double
    # -Werror=double-promotion: Warn when something is being promoted to a double
    # -Wno-format: because there are too many formatting issues that, at the end of the day, do not matter
    target_compile_options(butterscotch PRIVATE -fsingle-precision-constant -Werror=double-promotion -Wno-format)

    if(PS2)
        # Compiling on the Docker container
        target_include_directories(butterscotch PRIVATE $ENV{PS2SDK}/ee/include $ENV{PS2SDK}/common/include $ENV{PS2DEV}/ee/include $ENV{GSKIT}/include)
        target_link_directories(butterscotch PRIVATE $ENV{PS2SDK}/ee/lib $ENV{GSKIT}/lib)
        target_link_libraries(butterscotch PRIVATE ${AUDIO_LIBRARIES} gskit dmakit audsrv pad mc kbd patches kernel c m stb_ds sha1)

        # Embed ps2sdk IRX modules (freesio2, freepad) as C arrays via bin2c
        # This avoids loading from rom0: which has compatibility issues between fat/slim PS2
        set(IRX_DIR $ENV{PS2SDK}/iop/irx)
        set(IRX_GEN_DIR ${CMAKE_BINARY_DIR}/generated/irx)
        file(MAKE_DIRECTORY ${IRX_GEN_DIR})

        add_custom_command(
            OUTPUT ${IRX_GEN_DIR}/freesio2_irx.c
            COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/freesio2.irx ${IRX_GEN_DIR}/freesio2_irx.c freesio2_irx
            DEPENDS ${IRX_DIR}/freesio2.irx
            COMMENT "Embedding freesio2.irx"
        )
        add_custom_command(
            OUTPUT ${IRX_GEN_DIR}/mcman_irx.c
            COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/mcman.irx ${IRX_GEN_DIR}/mcman_irx.c mcman_irx
            DEPENDS ${IRX_DIR}/mcman.irx
            COMMENT "Embedding mcman.irx"
        )
        add_custom_command(
            OUTPUT ${IRX_GEN_DIR}/mcserv_irx.c
            COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/mcserv.irx ${IRX_GEN_DIR}/mcserv_irx.c mcserv_irx
            DEPENDS ${IRX_DIR}/mcserv.irx
            COMMENT "Embedding mcserv.irx"
        )
        add_custom_command(
            OUTPUT ${IRX_GEN_DIR}/freepad_irx.c
            COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/freepad.irx ${IRX_GEN_DIR}/freepad_irx.c freepad_irx
            DEPENDS ${IRX_DIR}/freepad.irx
            COMMENT "Embedding freepad.irx"
        )
        # We need the USB mass storage IRX modules for writing gmon.out when not running from host: and for keyboard support
        add_custom_command(
            OUTPUT ${IRX_GEN_DIR}/usbd_irx.c
            COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/usbd.irx ${IRX_GEN_DIR}/usbd_irx.c usbd_irx
            DEPENDS ${IRX_DIR}/usbd.irx
            COMMENT "Embedding usbd.irx"
        )
        add_custom_command(
            OUTPUT ${IRX_GEN_DIR}/ps2kbd_irx.c
            COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/ps2kbd.irx ${IRX_GEN_DIR}/ps2kbd_irx.c ps2kbd_irx
            DEPENDS ${IRX_DIR}/ps2kbd.irx
            COMMENT "Embedding ps2kbd.irx"
        )
        if(AUDIO_BACKEND STREQUAL "ps2")
            add_custom_command(
                OUTPUT ${IRX_GEN_DIR}/freesd_irx.c
                COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/freesd.irx ${IRX_GEN_DIR}/freesd_irx.c freesd_irx
                DEPENDS ${IRX_DIR}/freesd.irx
                COMMENT "Embedding freesd.irx"
            )
            add_custom_command(
                OUTPUT ${IRX_GEN_DIR}/audsrv_irx.c
                COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/audsrv.irx ${IRX_GEN_DIR}/audsrv_irx.c audsrv_irx
                DEPENDS ${IRX_DIR}/audsrv.irx
                COMMENT "Embedding audsrv.irx"
            )
        endif()

        target_sources(butterscotch PRIVATE ${IRX_GEN_DIR}/freesio2_irx.c ${IRX_GEN_DIR}/mcman_irx.c ${IRX_GEN_DIR}/mcserv_irx.c ${IRX_GEN_DIR}/freepad_irx.c ${IRX_GEN_DIR}/usbd_irx.c ${IRX_GEN_DIR}/ps2kbd_irx.c)
        if(AUDIO_BACKEND STREQUAL "ps2")
            target_sources(butterscotch PRIVATE ${IRX_GEN_DIR}/freesd_irx.c ${IRX_GEN_DIR}/audsrv_irx.c)
        endif()
        target_include_directories(butterscotch PRIVATE ${IRX_GEN_DIR})

        # ===[ gprof Profiling Support ]===
        option(ENABLE_GPROF_PROFILING "Enable gprof profiling for PS2" OFF)
        if(ENABLE_GPROF_PROFILING)
            add_compile_definitions(GPROF_PROFILING)
            target_compile_options(butterscotch PRIVATE -pg)
            target_link_options(butterscotch PRIVATE -pg)

            add_custom_command(
                OUTPUT ${IRX_GEN_DIR}/bdm_irx.c
                COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/bdm.irx ${IRX_GEN_DIR}/bdm_irx.c bdm_irx
                DEPENDS ${IRX_DIR}/bdm.irx
                COMMENT "Embedding bdm.irx (profiler)"
            )
            add_custom_command(
                OUTPUT ${IRX_GEN_DIR}/bdmfs_fatfs_irx.c
                COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/bdmfs_fatfs.irx ${IRX_GEN_DIR}/bdmfs_fatfs_irx.c bdmfs_fatfs_irx
                DEPENDS ${IRX_DIR}/bdmfs_fatfs.irx
                COMMENT "Embedding bdmfs_fatfs.irx (profiler)"
            )
            add_custom_command(
                OUTPUT ${IRX_GEN_DIR}/usbmass_bd_irx.c
                COMMAND $ENV{PS2SDK}/bin/bin2c ${IRX_DIR}/usbmass_bd.irx ${IRX_GEN_DIR}/usbmass_bd_irx.c usbmass_bd_irx
                DEPENDS ${IRX_DIR}/usbmass_bd.irx
                COMMENT "Embedding usbmass_bd.irx (profiler)"
            )
            target_sources(butterscotch PRIVATE
                ${IRX_GEN_DIR}/bdm_irx.c
                ${IRX_GEN_DIR}/bdmfs_fatfs_irx.c
                ${IRX_GEN_DIR}/usbmass_bd_irx.c
            )
        endif()
    else()
        # This block is for when we are developing Butterscotch with CLion!
        # ps2sdk and gsKit scatter headers across per-module subdirectories;
        # the flat include/ layout only exists after building the SDK.
        # We use file(GLOB) to discover all include dirs automatically.
        file(GLOB PS2SDK_EE_INCLUDES ${CMAKE_SOURCE_DIR}/ps2sdk/ee/*/include)
        file(GLOB PS2SDK_EE_RPC_INCLUDES ${CMAKE_SOURCE_DIR}/ps2sdk/ee/rpc/*/include)
        file(GLOB PS2SDK_EE_NETWORK_INCLUDES ${CMAKE_SOURCE_DIR}/ps2sdk/ee/network/*/include)
        file(GLOB GSKIT_INCLUDES ${CMAKE_SOURCE_DIR}/gsKit/ee/*/include)

        # _EE is normally set by the PS2 toolchain; without it, tamtypes.h
        # refuses to define u64/u32/etc. We set it so CLion can resolve them.
        target_compile_definitions(butterscotch PRIVATE _EE)

        target_include_directories(butterscotch PRIVATE
                ${PS2SDK_EE_INCLUDES}
                ${PS2SDK_EE_RPC_INCLUDES}
                ${PS2SDK_EE_NETWORK_INCLUDES}
                ${CMAKE_SOURCE_DIR}/ps2sdk/common/include
                ${GSKIT_INCLUDES}
                # Newlib and GCC built-in headers are build artifacts (not in any repo),
                # so these are still extracted from Docker by setup.sh.
                ${CMAKE_SOURCE_DIR}/ps2dev-headers/newlib-include
                ${CMAKE_SOURCE_DIR}/ps2dev-headers/gcc-include
        )
    endif()
elseif(PLATFORM STREQUAL "ps3")
    if (NOT ENABLE_LEGACY_GL)
        message(FATAL_ERROR "PS3 requires legacy gl!")
    endif()
    file(GLOB GL_SOURCES src/gl_common/*.c src/gl_legacy/*.c src/image/*.c)
    target_sources(butterscotch PRIVATE ${GL_SOURCES})
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl_legacy)
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/image)
    target_include_directories(butterscotch PRIVATE ${CMAKE_SOURCE_DIR}/src/gl_common)

    # ps3gl
    file(GLOB PS3GL_SOURCES vendor/ps3gl/*.c)
    add_library(ps3gl STATIC ${PS3GL_SOURCES})
    target_include_directories(ps3gl SYSTEM PUBLIC vendor/ps3gl)
    target_compile_options(ps3gl PRIVATE -w)

    # mojoal
    add_library(mojoal STATIC vendor/mojoal/mojoal.c)
    target_include_directories(mojoal SYSTEM PUBLIC vendor/mojoal)
    target_compile_definitions(mojoal PRIVATE AL_LIBTYPE_STATIC)
    target_compile_options(mojoal PRIVATE -w)

    # Butterscotch VM/interpreter profiler
    option(ENABLE_VM_GML_PROFILER "Enable Butterscotch VM/interpreter profiler" OFF)
    if(ENABLE_VM_GML_PROFILER)
        add_compile_definitions(ENABLE_VM_GML_PROFILER)
    endif()

    # Butterscotch VM/interpreter tracing
    option(ENABLE_VM_TRACING "Enable Butterscotch VM/interpreter tracing checks" OFF)
    if(ENABLE_VM_TRACING)
        add_compile_definitions(ENABLE_VM_TRACING)
    endif()

    # Butterscotch VM opcode count profiler
    option(ENABLE_VM_OPCODE_PROFILER "Enable Butterscotch VM opcode count profiler" OFF)
    if(ENABLE_VM_OPCODE_PROFILER)
        add_compile_definitions(ENABLE_VM_OPCODE_PROFILER)
    endif()

    # Spatial grid logs
    option(ENABLE_SPATIAL_GRID_LOGS "Enable Spatial Grid logs" OFF)
    if(ENABLE_SPATIAL_GRID_LOGS)
        add_compile_definitions(ENABLE_SPATIAL_GRID_LOGS)
    endif()

    # VM unknown/stubbed function logs
    option(ENABLE_VM_STUB_LOGS "Enable VM unknown/stubbed function logs" OFF)
    if(ENABLE_VM_STUB_LOGS)
        add_compile_definitions(ENABLE_VM_STUB_LOGS)
    endif()

    # stb_image
    target_include_directories(butterscotch PUBLIC vendor/stb/image)

    # Platform identifier for PS3-specific code paths
    add_compile_definitions(PLATFORM_PS3)
    add_compile_definitions(AL_LIBTYPE_STATIC)

    set(CMAKE_EXE_LINKER_FLAGS "-mhard-float -fmodulo-sched -Wl,--gc-sections -L$ENV{PS3DEV}/ppu/lib -L$ENV{PS3DEV}/portlibs/ppu/lib")

    find_package(SDL2 REQUIRED)
    target_include_directories(butterscotch PRIVATE ${SDL2_INCLUDE_DIRS})
    # mojoal calls SDL atomics/threads, so SDL2 must link after it (static link order)
    target_link_libraries(mojoal PUBLIC ${SDL2_LIBRARIES})
    target_link_libraries(butterscotch PRIVATE bzip2 sha1 ps3gl mojoal ${SDL2_LIBRARIES} gcm_sys rsx sysutil io audio rt lv2 m stb_ds stb_vorbis)
elseif(PLATFORM STREQUAL "wii")
    add_compile_definitions(PLATFORM_WII USE_FLOAT_REALS GEKKO HW_RVL __wii__)

    resolve_devkitpro_root(DEVKITPRO_ROOT)

    target_include_directories(butterscotch PRIVATE
        ${CMAKE_SOURCE_DIR}/src/wii
        ${CMAKE_SOURCE_DIR}/vendor
        ${CMAKE_SOURCE_DIR}/vendor/stb/ds
        ${DEVKITPRO_ROOT}/libogc/include
        ${DEVKITPRO_ROOT}/portlibs/wii/include
        ${DEVKITPRO_ROOT}/portlibs/ppc/include
    )
    target_link_directories(butterscotch PRIVATE
        ${DEVKITPRO_ROOT}/libogc/lib/wii
        ${DEVKITPRO_ROOT}/portlibs/wii/lib
        ${DEVKITPRO_ROOT}/portlibs/ppc/lib
    )
    target_compile_options(butterscotch PRIVATE
        -mcpu=750
        -meabi
        -mhard-float
        -ffunction-sections
        -fdata-sections
        -fno-common
        -Wno-format
    )
    target_link_options(butterscotch PRIVATE
        -mcpu=750
        -meabi
        -mhard-float
        -Wl,--gc-sections
    )

    if(NOT EXISTS "${DEVKITPRO_ROOT}/portlibs/ppc/include/tremor/ivorbisfile.h" OR NOT EXISTS "${DEVKITPRO_ROOT}/portlibs/ppc/lib/libvorbisidec.a")
        message(FATAL_ERROR "Wii libvorbisidec/Tremor is required for streamed Ogg audio")
    endif()
    set(WII_VORBIS_LIBS vorbisidec ogg)

    target_link_libraries(butterscotch PRIVATE
        fat
        ${WII_VORBIS_LIBS}
        wiiuse
        bte
        ogc
        m
        bzip2
        sha1
        stb_ds
    )
    set_target_properties(butterscotch PROPERTIES OUTPUT_NAME boot)
    ogc_create_dol(butterscotch)
    add_custom_command(TARGET butterscotch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/resources/wii/icon.png
            ${CMAKE_SOURCE_DIR}/resources/wii/meta.xml
            $<TARGET_FILE_DIR:butterscotch>
        COMMENT "Copying Wii Homebrew Channel resources"
        VERBATIM
    )
elseif(PLATFORM STREQUAL "3ds")
    set_target_properties(butterscotch PROPERTIES OUTPUT_NAME cinnamon)
    add_compile_definitions(PLATFORM_3DS USE_FLOAT_REALS NO_RVALUE_INT64)

    option(APPSURFACE-HALF "Render the 3DS application surface at half resolution (640x480 to 320x240)" OFF)
    if(APPSURFACE-HALF)
        target_compile_definitions(butterscotch PRIVATE APPSURFACE_HALF=1)
    endif()

    option(useROMFS "Load 3DS game data from the packaged RomFS" OFF)
    if(useROMFS)
        add_compile_definitions(N3DS_USE_ROMFS)
    endif()

    resolve_devkitpro_root(DEVKITPRO_ROOT)

    target_include_directories(butterscotch PRIVATE
        ${CMAKE_SOURCE_DIR}/src/3ds
        ${CMAKE_SOURCE_DIR}/vendor
        ${CMAKE_SOURCE_DIR}/vendor/stb/ds
        ${CMAKE_SOURCE_DIR}/vendor/stb/image
        ${DEVKITPRO_ROOT}/libctru/include
        ${DEVKITPRO_ROOT}/portlibs/3ds/include
    )

    target_link_directories(butterscotch PRIVATE
        ${DEVKITPRO_ROOT}/libctru/lib
        ${DEVKITPRO_ROOT}/portlibs/3ds/lib
    )

    target_compile_options(butterscotch PRIVATE
        -march=armv6k
        -mtune=mpcore
        -mfloat-abi=hard
        -mtp=soft
        -mword-relocations
        -ffunction-sections
        -fdata-sections
        -fomit-frame-pointer
        -fno-common
        -fno-unwind-tables
        -fno-asynchronous-unwind-tables
        -fsingle-precision-constant
        -fno-math-errno
        -Wno-format
    )
    target_compile_options(butterscotch PRIVATE "$<$<CONFIG:Release>:-O3;-DNDEBUG;-g0;-flto>")
    target_link_options(butterscotch PRIVATE
        -march=armv6k
        -mtune=mpcore
        -mfloat-abi=hard
        -mtp=soft
        -mword-relocations
        -Wl,--gc-sections
        -Wl,--sort-section=name
    )
    target_link_options(butterscotch PRIVATE "$<$<CONFIG:Release>:-g0;-flto>")

    set_source_files_properties(
        src/vm.c
        PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-O3;-falign-functions=32;-falign-loops=16>"
    )

    target_compile_options(sha1 PRIVATE -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft)
    target_compile_options(bzip2 PRIVATE -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft)

    set(N3DS_RESOURCE_ROOT ${CMAKE_SOURCE_DIR}/resources/3ds)
    set(N3DS_ROMFS_SOURCE_DIR ${N3DS_RESOURCE_ROOT}/romfs CACHE PATH "Files packaged in the 3DS RomFS")
    set(N3DS_BUILD_RESOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/resources/3ds)
    set(N3DS_ROMFS_DIR ${N3DS_BUILD_RESOURCE_DIR}/romfs)
    set(N3DS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/3ds)
    set(N3DS_HOST_TOOLS_DIR ${CMAKE_CURRENT_BINARY_DIR}/host-tools)
    file(MAKE_DIRECTORY ${N3DS_GENERATED_DIR})

    find_program(CTR_PICASSO picasso HINTS ${DEVKITPRO_ROOT}/tools/bin)
    find_program(CTR_TEX3DS tex3ds HINTS ${DEVKITPRO_ROOT}/tools/bin)
    find_program(CTR_3DSXTOOL 3dsxtool HINTS ${DEVKITPRO_ROOT}/tools/bin)
    find_program(CTR_SMDHTOOL smdhtool HINTS ${DEVKITPRO_ROOT}/tools/bin)
    find_program(CTR_BANNERTOOL bannertool HINTS ${N3DS_HOST_TOOLS_DIR} ${DEVKITPRO_ROOT}/tools/bin)
    find_program(CTR_MAKEROM makerom HINTS ${N3DS_HOST_TOOLS_DIR} ${DEVKITPRO_ROOT}/tools/bin)

    option(N3DS_AUTO_FETCH_CIA_TOOLS
        "Download pinned makerom and bannertool host binaries when they are not installed" ON)
    set(N3DS_CIA_TOOL_OUTPUTS)
    if(N3DS_AUTO_FETCH_CIA_TOOLS AND (NOT CTR_BANNERTOOL OR NOT CTR_MAKEROM))
        string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" N3DS_HOST_PROCESSOR)
        set(N3DS_MAKEROM_VERSION 0.19.0)
        set(N3DS_BANNERTOOL_VERSION 1.2.2)
        set(N3DS_BANNERTOOL_URL
            "https://github.com/Epicpkmn11/bannertool/releases/download/v${N3DS_BANNERTOOL_VERSION}/bannertool.zip")
        set(N3DS_BANNERTOOL_SHA256
            e4259c08fe8944ebadd5f4b96f9a8603e5427338074cfc46323fbbc3410d51ed)
        set(N3DS_CIA_TOOLS_SUPPORTED TRUE)

        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
            N3DS_HOST_PROCESSOR MATCHES "^(x86_64|amd64)$")
            set(N3DS_MAKEROM_PLATFORM ubuntu_x86_64)
            set(N3DS_MAKEROM_SHA256
                287b809dec064e0ad597e3d272c49ecb7eed41693d5ee6fef9d8a8aa24c2497e)
            set(N3DS_BANNERTOOL_MEMBER linux-x86_64/bannertool)
            set(N3DS_HOST_TOOL_SUFFIX "")
        elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
            N3DS_HOST_PROCESSOR MATCHES "^(x86_64|amd64)$")
            set(N3DS_MAKEROM_PLATFORM macos_x86_64)
            set(N3DS_MAKEROM_SHA256
                a52563e5549cd2dd84e9b8d1cb0d1fdf15ea9bd0f6241e2e178fe4e4bf3a9277)
            set(N3DS_BANNERTOOL_MEMBER mac-x86_64/bannertool)
            set(N3DS_HOST_TOOL_SUFFIX "")
        elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND
            N3DS_HOST_PROCESSOR MATCHES "^(x86_64|amd64)$")
            set(N3DS_MAKEROM_PLATFORM win_x86_64)
            set(N3DS_MAKEROM_SHA256
                88df4455e60556374e202d507f54ee03fac7493ec9554ab853157524b6d69db0)
            set(N3DS_BANNERTOOL_MEMBER windows-x86_64/bannertool.exe)
            set(N3DS_HOST_TOOL_SUFFIX .exe)
        else()
            set(N3DS_CIA_TOOLS_SUPPORTED FALSE)
            message(STATUS
                "Automatic makerom/bannertool setup is unavailable for "
                "${CMAKE_HOST_SYSTEM_NAME}/${CMAKE_HOST_SYSTEM_PROCESSOR}")
        endif()

        if(N3DS_CIA_TOOLS_SUPPORTED)
            set(N3DS_MAKEROM_URL
                "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v${N3DS_MAKEROM_VERSION}/makerom-v${N3DS_MAKEROM_VERSION}-${N3DS_MAKEROM_PLATFORM}.zip")
            if(NOT CTR_MAKEROM)
                set(CTR_MAKEROM
                    ${N3DS_HOST_TOOLS_DIR}/makerom-${N3DS_MAKEROM_VERSION}${N3DS_HOST_TOOL_SUFFIX})
                add_custom_command(
                    OUTPUT ${CTR_MAKEROM}
                    COMMAND ${CMAKE_COMMAND}
                        -DTOOL_NAME=makerom
                        -DTOOL_URL=${N3DS_MAKEROM_URL}
                        -DTOOL_SHA256=${N3DS_MAKEROM_SHA256}
                        -DTOOL_ARCHIVE_MEMBER=makerom${N3DS_HOST_TOOL_SUFFIX}
                        -DTOOL_OUTPUT=${CTR_MAKEROM}
                        -P ${CMAKE_SOURCE_DIR}/cmake/fetch_3ds_tool.cmake
                    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/fetch_3ds_tool.cmake
                    COMMENT "Installing makerom ${N3DS_MAKEROM_VERSION}"
                    VERBATIM
                )
                list(APPEND N3DS_CIA_TOOL_OUTPUTS ${CTR_MAKEROM})
            endif()
            if(NOT CTR_BANNERTOOL)
                set(CTR_BANNERTOOL
                    ${N3DS_HOST_TOOLS_DIR}/bannertool-${N3DS_BANNERTOOL_VERSION}${N3DS_HOST_TOOL_SUFFIX})
                add_custom_command(
                    OUTPUT ${CTR_BANNERTOOL}
                    COMMAND ${CMAKE_COMMAND}
                        -DTOOL_NAME=bannertool
                        -DTOOL_URL=${N3DS_BANNERTOOL_URL}
                        -DTOOL_SHA256=${N3DS_BANNERTOOL_SHA256}
                        -DTOOL_ARCHIVE_MEMBER=${N3DS_BANNERTOOL_MEMBER}
                        -DTOOL_OUTPUT=${CTR_BANNERTOOL}
                        -P ${CMAKE_SOURCE_DIR}/cmake/fetch_3ds_tool.cmake
                    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/fetch_3ds_tool.cmake
                    COMMENT "Installing bannertool ${N3DS_BANNERTOOL_VERSION}"
                    VERBATIM
                )
                list(APPEND N3DS_CIA_TOOL_OUTPUTS ${CTR_BANNERTOOL})
            endif()
            add_custom_target(n3ds_cia_tools DEPENDS ${N3DS_CIA_TOOL_OUTPUTS})
        endif()
    endif()

    foreach(REQUIRED_TOOL CTR_PICASSO CTR_3DSXTOOL)
        if(NOT ${REQUIRED_TOOL})
            message(FATAL_ERROR "Required 3DS tool not found: ${REQUIRED_TOOL}")
        endif()
    endforeach()

    set(N3DS_SHADER_SHBIN ${N3DS_GENERATED_DIR}/n3ds_2d.shbin)
    set(N3DS_SHADER_C ${N3DS_GENERATED_DIR}/n3ds_2d_shbin.c)
    add_custom_command(
        OUTPUT "${N3DS_SHADER_C}"
        BYPRODUCTS "${N3DS_SHADER_SHBIN}"
        COMMAND "${CTR_PICASSO}" -o "${N3DS_SHADER_SHBIN}" "${N3DS_RESOURCE_ROOT}/shaders/3ds_2d.pica"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${N3DS_SHADER_SHBIN}" "-DOUTPUT=${N3DS_SHADER_C}" -DSYMBOL=n3ds_2d_shbin -P "${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake"
        DEPENDS "${N3DS_RESOURCE_ROOT}/shaders/3ds_2d.pica" "${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake"
        COMMENT "Embedding 3DS shader"
        VERBATIM
    )
    target_sources(butterscotch PRIVATE ${N3DS_SHADER_C})

    set(N3DS_3DSX ${CMAKE_CURRENT_BINARY_DIR}/cinnamon.3dsx)
    set(N3DS_SMDH ${CMAKE_CURRENT_BINARY_DIR}/cinnamon.smdh)
    set(N3DS_BANNER ${CMAKE_CURRENT_BINARY_DIR}/cinnamon.bnr)
    set(N3DS_CIA ${CMAKE_CURRENT_BINARY_DIR}/cinnamon.cia)
    set(N3DS_APP_TITLE "UNDERTALE")
    set(N3DS_APP_DESCRIPTION "OG game by Toby Fox.")
    set(N3DS_APP_AUTHOR "Sunshine Dev Team")
    set(N3DS_APP_PRODUCT_CODE "000400000B0E2A00")
    set(N3DS_APP_UNIQUE_ID "0x1929")
    set(N3DS_APP_ENCRYPTED "false")
    set(N3DS_APP_SYSTEM_MODE "80MB")
    set(N3DS_APP_SYSTEM_MODE_EXT "124MB")
    if(EXISTS ${N3DS_RESOURCE_ROOT}/meta/AppInfo)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${N3DS_RESOURCE_ROOT}/meta/AppInfo")
        file(STRINGS ${N3DS_RESOURCE_ROOT}/meta/AppInfo N3DS_APPINFO_LINES)
        foreach(APPINFO_LINE IN LISTS N3DS_APPINFO_LINES)
            if(APPINFO_LINE MATCHES "^([A-Za-z0-9_]+)[ \t]*=[ \t]*(.*)$")
                set(N3DS_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
            endif()
        endforeach()
    endif()

    if(NOT N3DS_APP_SYSTEM_MODE MATCHES "^(64MB|96MB|80MB|72MB|32MB)$")
        message(FATAL_ERROR "APP_SYSTEM_MODE must be 64MB, 96MB, 80MB, 72MB, or 32MB")
    endif()
    if(NOT N3DS_APP_SYSTEM_MODE_EXT MATCHES "^(Legacy|124MB|178MB)$")
        message(FATAL_ERROR "APP_SYSTEM_MODE_EXT must be Legacy, 124MB, or 178MB")
    endif()

    set(N3DS_RESOURCE_OUTPUTS ${N3DS_SHADER_C})
    set(N3DS_3DSXTOOL_ARGS $<TARGET_FILE:butterscotch> ${N3DS_3DSX})
    set(N3DS_ROMFS_PREPARED FALSE)
    set(N3DS_ROMFS_T3X_OUTPUTS)

    if(CTR_TEX3DS AND EXISTS ${N3DS_ROMFS_SOURCE_DIR}/gfx/debugTinyFont.png)
        foreach(N3DS_ROMFS_T3X_ASSET debugTinyFont)
            set(N3DS_ROMFS_T3X_SOURCE ${N3DS_ROMFS_SOURCE_DIR}/gfx/${N3DS_ROMFS_T3X_ASSET}.png)
            set(N3DS_ROMFS_T3X_GENERATED ${N3DS_GENERATED_DIR}/romfs/gfx/${N3DS_ROMFS_T3X_ASSET}.t3x)
            add_custom_command(
                OUTPUT ${N3DS_ROMFS_T3X_GENERATED}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${N3DS_GENERATED_DIR}/romfs/gfx
                COMMAND ${CTR_TEX3DS} -f rgba8 -o ${N3DS_ROMFS_T3X_GENERATED} ${N3DS_ROMFS_T3X_SOURCE}
                DEPENDS ${N3DS_ROMFS_T3X_SOURCE}
                COMMENT "Converting 3DS ROMFS texture ${N3DS_ROMFS_T3X_ASSET}"
            )
            list(APPEND N3DS_ROMFS_T3X_OUTPUTS ${N3DS_ROMFS_T3X_GENERATED})
        endforeach()
    elseif(EXISTS ${N3DS_ROMFS_SOURCE_DIR}/gfx/debugTinyFont.png)
        message(STATUS "tex3ds not found; built-in 3DS ROMFS textures will not be converted.")
    endif()

    if(useROMFS AND NOT EXISTS ${N3DS_ROMFS_SOURCE_DIR}/data.win)
        message(FATAL_ERROR "useROMFS requires ${N3DS_ROMFS_SOURCE_DIR}/data.win")
    endif()

    file(GLOB_RECURSE N3DS_BUNDLED_ROMFS_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE ${N3DS_ROMFS_SOURCE_DIR}/*)
    if(N3DS_BUNDLED_ROMFS_FILES OR N3DS_ROMFS_T3X_OUTPUTS)
        set(N3DS_ROMFS_STAMP ${N3DS_BUILD_RESOURCE_DIR}/romfs-copy.stamp)
        add_custom_command(
            OUTPUT ${N3DS_ROMFS_STAMP}
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${N3DS_ROMFS_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${N3DS_ROMFS_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${N3DS_ROMFS_SOURCE_DIR} ${N3DS_ROMFS_DIR}
            DEPENDS ${N3DS_BUNDLED_ROMFS_FILES} ${N3DS_ROMFS_T3X_OUTPUTS}
            COMMENT "Staging 3DS RomFS resources"
        )
        if(N3DS_ROMFS_T3X_OUTPUTS)
            add_custom_command(
                OUTPUT ${N3DS_ROMFS_STAMP} APPEND
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${N3DS_GENERATED_DIR}/romfs/gfx/debugTinyFont.t3x ${N3DS_ROMFS_DIR}/gfx/debugTinyFont.t3x
            )
        endif()
        add_custom_command(
            OUTPUT ${N3DS_ROMFS_STAMP} APPEND
            COMMAND ${CMAKE_COMMAND} -E touch ${N3DS_ROMFS_STAMP}
        )
        list(APPEND N3DS_RESOURCE_OUTPUTS ${N3DS_ROMFS_STAMP} ${N3DS_ROMFS_T3X_OUTPUTS})
        list(APPEND N3DS_3DSXTOOL_ARGS --romfs=${N3DS_ROMFS_DIR})
        set(N3DS_ROMFS_PREPARED TRUE)
    else()
        message(STATUS "3DS ROMFS resources not found; building without embedded RomFS.")
    endif()

    set(N3DS_CIA_ROMFS_READY ${N3DS_ROMFS_PREPARED})
    set(N3DS_CIA_ROMFS_ARGS -DAPP_ROMFS=${N3DS_ROMFS_DIR})

    set(N3DS_HAVE_SMDH FALSE)
    if(CTR_SMDHTOOL AND EXISTS ${N3DS_RESOURCE_ROOT}/meta/icon.png)
        set(N3DS_SMDH_DEPENDS ${N3DS_RESOURCE_ROOT}/meta/icon.png)
        if(EXISTS ${N3DS_RESOURCE_ROOT}/meta/AppInfo)
            list(APPEND N3DS_SMDH_DEPENDS ${N3DS_RESOURCE_ROOT}/meta/AppInfo)
        endif()
        add_custom_command(
            OUTPUT ${N3DS_SMDH}
            COMMAND ${CTR_SMDHTOOL} --create "${N3DS_APP_TITLE}" "${N3DS_APP_DESCRIPTION}" "${N3DS_APP_AUTHOR}" ${N3DS_RESOURCE_ROOT}/meta/icon.png ${N3DS_SMDH}
            DEPENDS ${N3DS_SMDH_DEPENDS}
            COMMENT "Generating 3DS SMDH"
        )
        list(APPEND N3DS_RESOURCE_OUTPUTS ${N3DS_SMDH})
        list(APPEND N3DS_3DSXTOOL_ARGS --smdh=${N3DS_SMDH})
        set(N3DS_HAVE_SMDH TRUE)
    elseif(CTR_SMDHTOOL)
        message(STATUS "3DS icon.png not found; SMDH output will be skipped.")
    endif()

    set(N3DS_HAVE_BANNER FALSE)
    if(CTR_BANNERTOOL AND EXISTS ${N3DS_RESOURCE_ROOT}/meta/banner.png AND EXISTS ${N3DS_RESOURCE_ROOT}/meta/audio.wav)
        set(N3DS_BANNER_CWAV ${N3DS_GENERATED_DIR}/banner.cwav)
        add_custom_command(
            OUTPUT ${N3DS_BANNER}
            BYPRODUCTS ${N3DS_BANNER_CWAV}
            COMMAND "${CTR_BANNERTOOL}" makecwav -i "${N3DS_RESOURCE_ROOT}/meta/audio.wav" -o "${N3DS_BANNER_CWAV}" -l false
            COMMAND "${CTR_BANNERTOOL}" makebanner -i "${N3DS_RESOURCE_ROOT}/meta/banner.png" -ca "${N3DS_BANNER_CWAV}" -o "${N3DS_BANNER}"
            DEPENDS ${N3DS_RESOURCE_ROOT}/meta/banner.png ${N3DS_RESOURCE_ROOT}/meta/audio.wav ${CTR_BANNERTOOL}
            COMMENT "Generating 3DS banner"
            VERBATIM
        )
        list(APPEND N3DS_RESOURCE_OUTPUTS ${N3DS_BANNER})
        set(N3DS_HAVE_BANNER TRUE)
    else()
        message(STATUS "bannertool or 3DS banner resources not found; 3DS banner output will be skipped.")
    endif()

    add_custom_target(n3ds_resources ALL DEPENDS ${N3DS_RESOURCE_OUTPUTS})
    add_dependencies(butterscotch n3ds_resources)
    if(TARGET n3ds_cia_tools)
        add_dependencies(n3ds_resources n3ds_cia_tools)
        add_dependencies(butterscotch n3ds_cia_tools)
    endif()

    target_link_libraries(butterscotch PRIVATE citro2d citro3d ctru vorbisidec ogg m bzip2 sha1 stb_vorbis)

    add_custom_command(TARGET butterscotch POST_BUILD
        COMMAND ${CTR_3DSXTOOL} ${N3DS_3DSXTOOL_ARGS}
        COMMENT "Generating 3DSX package"
    )

    if(CTR_BANNERTOOL AND CTR_MAKEROM AND N3DS_HAVE_SMDH AND N3DS_HAVE_BANNER AND N3DS_CIA_ROMFS_READY AND EXISTS ${N3DS_RESOURCE_ROOT}/meta/template.rsf)
        set_property(TARGET butterscotch APPEND PROPERTY LINK_DEPENDS
            "${N3DS_SMDH}" "${N3DS_BANNER}" "${N3DS_RESOURCE_ROOT}/meta/template.rsf")
        add_custom_command(TARGET butterscotch POST_BUILD
            COMMAND ${CTR_MAKEROM} -f cia -o ${N3DS_CIA} -elf $<TARGET_FILE:butterscotch> -rsf ${N3DS_RESOURCE_ROOT}/meta/template.rsf -icon ${N3DS_SMDH} -banner ${N3DS_BANNER} ${N3DS_CIA_ROMFS_ARGS} -DAPP_TITLE=${N3DS_APP_TITLE} -DAPP_PRODUCT_CODE=${N3DS_APP_PRODUCT_CODE} -DAPP_UNIQUE_ID=${N3DS_APP_UNIQUE_ID} -DAPP_ENCRYPTED=${N3DS_APP_ENCRYPTED} -DAPP_SYSTEM_MODE=${N3DS_APP_SYSTEM_MODE} -DAPP_SYSTEM_MODE_EXT=${N3DS_APP_SYSTEM_MODE_EXT}
            COMMENT "Generating CIA package"
        )
    else()
        message(STATUS "makerom, bannertool, or 3DS CIA resources not found; CIA output will be skipped.")
    endif()
elseif(PLATFORM STREQUAL "vita")
else()
    message(FATAL_ERROR "Unknown platform! ${PLATFORM}")
endif()
