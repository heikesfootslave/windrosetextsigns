cmake_minimum_required(VERSION 3.22)

set(TARGET WindroseTextSigns)
project(${TARGET})

add_library(${TARGET} SHARED
    src/dllmain.cpp
    src/NativeBridge.cpp
    src/RelayHttp.cpp
    src/SignTextMod.cpp
    src/UpnpNat.cpp
)

target_include_directories(${TARGET} PRIVATE include)

if(TARGET UE4SS)
    target_link_libraries(${TARGET} PUBLIC UE4SS)
else()
    set(UE4SS_RE_ROOT "" CACHE PATH "Path to RE-UE4SS repository root")
    set(UE4SS_SDK_INCLUDE_DIR "" CACHE PATH "Path to RE-UE4SS/UE4SS/include (optional if UE4SS_RE_ROOT is set)")
    set(UE4SS_IMPORT_LIB "" CACHE FILEPATH "Path to import library for UE4SS.dll (for example libUE4SS.a)")
    set(UE4SS_EXTRA_INCLUDE_DIRS "" CACHE STRING "Additional include directories (semicolon-separated)")

    if(UE4SS_RE_ROOT AND NOT UE4SS_SDK_INCLUDE_DIR)
        set(UE4SS_SDK_INCLUDE_DIR "${UE4SS_RE_ROOT}/UE4SS/include")
    endif()

    if(NOT UE4SS_SDK_INCLUDE_DIR)
        message(FATAL_ERROR "UE4SS_SDK_INCLUDE_DIR (or UE4SS_RE_ROOT) is required when UE4SS target is not provided.")
    endif()
    if(NOT UE4SS_IMPORT_LIB)
        message(FATAL_ERROR "UE4SS_IMPORT_LIB is required when UE4SS target is not provided.")
    endif()

    target_include_directories(${TARGET} PRIVATE "${UE4SS_SDK_INCLUDE_DIR}")

    if(UE4SS_RE_ROOT)
        file(GLOB UE4SS_FIRST_INCLUDE_DIRS LIST_DIRECTORIES true "${UE4SS_RE_ROOT}/deps/first/*/include")
        file(GLOB UE4SS_THIRD_INCLUDE_DIRS LIST_DIRECTORIES true "${UE4SS_RE_ROOT}/deps/third/*/include")
        target_include_directories(${TARGET} PRIVATE ${UE4SS_FIRST_INCLUDE_DIRS} ${UE4SS_THIRD_INCLUDE_DIRS})
    endif()
    if(UE4SS_EXTRA_INCLUDE_DIRS)
        target_include_directories(${TARGET} PRIVATE ${UE4SS_EXTRA_INCLUDE_DIRS})
    endif()

    target_link_libraries(${TARGET} PUBLIC "${UE4SS_IMPORT_LIB}")
endif()

target_compile_features(${TARGET} PUBLIC cxx_std_23)

if(WIN32)
    target_link_libraries(${TARGET} PRIVATE ws2_32 winhttp ole32 oleaut32)
endif()
