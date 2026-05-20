# Emscripten Platform Detection and Configuration
# Include this file in the main CMakeLists.txt or StepmaniaCore.cmake

# Detect Emscripten
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR EMSCRIPTEN)
    message(STATUS "Detected Emscripten platform")

    set(EMSCRIPTEN_BUILD TRUE)
    set(LINUX FALSE)
    set(MACOSX FALSE)
    set(WIN32 FALSE)
    set(BSD FALSE)

    # Enable features required for browser
    set(WITH_SDL ON CACHE BOOL "Use SDL2" FORCE)
    set(WITH_GLES2 ON CACHE BOOL "Use OpenGL ES 2.0" FORCE)
    set(HAVE_SDL TRUE)

    # Disable incompatible features
    set(WITH_MINIMAID OFF CACHE BOOL "No USB lights support" FORCE)
    set(WITH_CRASH_HANDLER OFF CACHE BOOL "No crash handler" FORCE)
    set(WITH_FFMPEG OFF CACHE BOOL "Disable FFmpeg initially" FORCE)

    # Compiler standard
    set(SM_CPP_STANDARD "c++11")

    # Add Emscripten-specific compile definitions
    add_compile_definitions(
        EMSCRIPTEN_BUILD
        HAVE_SDL
        USE_GLES2
    )

    # Include Emscripten configuration
    if(EXISTS "${CMAKE_SOURCE_DIR}/CMakeLists.emscripten.txt")
        include("${CMAKE_SOURCE_DIR}/CMakeLists.emscripten.txt")
    endif()

    message(STATUS "Emscripten build configured")
endif()
