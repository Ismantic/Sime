include_guard(GLOBAL)

set(SIME_NCNN_PROVIDER "AUTO" CACHE STRING
    "ncnn provider: AUTO, SYSTEM, or FETCH")
set_property(CACHE SIME_NCNN_PROVIDER PROPERTY STRINGS AUTO SYSTEM FETCH)

function(sime_add_ncnn)
    if(TARGET ncnn)
        return()
    endif()

    string(TOUPPER "${SIME_NCNN_PROVIDER}" _provider)
    if(NOT _provider MATCHES "^(AUTO|SYSTEM|FETCH)$")
        message(FATAL_ERROR
            "Invalid SIME_NCNN_PROVIDER='${SIME_NCNN_PROVIDER}'")
    endif()

    if(_provider STREQUAL "AUTO" OR _provider STREQUAL "SYSTEM")
        find_package(ncnn CONFIG QUIET)
        if(TARGET ncnn)
            message(STATUS "Sime: using system ncnn")
            return()
        endif()
        if(_provider STREQUAL "SYSTEM")
            message(FATAL_ERROR
                "SIME_NCNN_PROVIDER=SYSTEM but ncnn was not found")
        endif()
    endif()

    include(FetchContent)

    # The GRU reranker is CPU-only and loads its model from the app bundle.
    # Keep this dependency small and avoid Vulkan/glslang and OpenMP runtimes.
    set(NCNN_SHARED_LIB OFF CACHE BOOL "" FORCE)
    set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
    set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)
    set(NCNN_THREADS ON CACHE BOOL "" FORCE)
    set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
    set(NCNN_C_API OFF CACHE BOOL "" FORCE)
    set(NCNN_PLATFORM_API OFF CACHE BOOL "" FORCE)
    set(NCNN_PIXEL OFF CACHE BOOL "" FORCE)
    set(NCNN_PIXEL_ROTATE OFF CACHE BOOL "" FORCE)
    set(NCNN_PIXEL_AFFINE OFF CACHE BOOL "" FORCE)
    set(NCNN_PIXEL_DRAWING OFF CACHE BOOL "" FORCE)

    if(SIME_NCNN_SOURCE_DIR AND EXISTS "${SIME_NCNN_SOURCE_DIR}/CMakeLists.txt")
        FetchContent_Declare(ncnn SOURCE_DIR "${SIME_NCNN_SOURCE_DIR}")
    elseif(SIME_NCNN_SOURCE_DIR)
        FetchContent_Declare(ncnn
            SOURCE_DIR "${SIME_NCNN_SOURCE_DIR}"
            URL
                "https://github.com/Tencent/ncnn/releases/download/20260526/ncnn-20260526-full-source.zip"
            URL_HASH
                "SHA256=754659d6fe65545cf2ef4483ffb84526fea631f8764c44b150f1601d0fb4004b"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
    else()
        FetchContent_Declare(ncnn
            URL
                "https://github.com/Tencent/ncnn/releases/download/20260526/ncnn-20260526-full-source.zip"
            URL_HASH
                "SHA256=754659d6fe65545cf2ef4483ffb84526fea631f8764c44b150f1601d0fb4004b"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
    endif()
    FetchContent_MakeAvailable(ncnn)
    # ncnn's Android target exports these flags to every consumer. Sime uses
    # exceptions, while ncnn itself may still compile without them.
    get_target_property(_ncnn_options ncnn INTERFACE_COMPILE_OPTIONS)
    if(_ncnn_options)
        list(REMOVE_ITEM _ncnn_options -fno-exceptions -fno-rtti)
        set_property(TARGET ncnn PROPERTY INTERFACE_COMPILE_OPTIONS
                     "${_ncnn_options}")
    endif()
    message(STATUS "Sime: using bundled CPU-only ncnn 20260526")
endfunction()
