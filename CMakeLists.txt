cmake_minimum_required(VERSION 3.24)

# Set Apple defaults before compiler detection, including under Rosetta.
if(CMAKE_HOST_APPLE AND NOT CMAKE_SYSTEM_NAME)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS target architecture")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
endif()
project(ImgToTable VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "macOS supports arm64 only; Intel and universal builds are no longer supported. Set CMAKE_OSX_ARCHITECTURES=arm64.")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "14.0")
        message(FATAL_ERROR "macOS deployment target must be 14.0 or newer.")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IMGTOTABLE_BUILD_APP "Build the Qt Widgets desktop application" ON)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Extracted ONNX Runtime C/C++ SDK directory")
include(CTest)

add_library(imgtotable_core STATIC core/InferenceEngine.cpp core/InferenceEngine.h)
target_include_directories(imgtotable_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(ONNXRUNTIME_ROOT)
    # NO_CACHE ensures changing the SDK root cannot retain an old SDK library.
    find_path(onnxInclude onnxruntime_cxx_api.h
        PATHS "${ONNXRUNTIME_ROOT}/include" NO_DEFAULT_PATH NO_CACHE REQUIRED)
    find_library(onnxLibrary NAMES onnxruntime
        PATHS "${ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH NO_CACHE REQUIRED)
    add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${onnxInclude}")
    if(WIN32)
        find_file(onnxRuntime NAMES onnxruntime.dll
            PATHS "${ONNXRUNTIME_ROOT}/lib" "${ONNXRUNTIME_ROOT}/bin"
            NO_DEFAULT_PATH NO_CACHE REQUIRED)
        set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
            IMPORTED_IMPLIB "${onnxLibrary}" IMPORTED_LOCATION "${onnxRuntime}")
    elseif(APPLE)
        set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
            IMPORTED_LOCATION "${onnxLibrary}")
    else()
        message(FATAL_ERROR "ONNX SDK integration currently supports Windows and macOS only")
    endif()
    target_link_libraries(imgtotable_core PRIVATE ONNXRuntime::ONNXRuntime)
    target_compile_definitions(imgtotable_core PRIVATE IMGTOTABLE_HAS_ONNX)
else()
    message(STATUS "ONNXRUNTIME_ROOT is empty: building without inference support")
endif()

function(imgtotable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
imgtotable_warnings(imgtotable_core)

if(IMGTOTABLE_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
    qt_add_executable(imgtotable WIN32 MACOSX_BUNDLE
        src/main.cpp ui/MainWindow.cpp ui/MainWindow.h
        platform/PlatformUtils.cpp platform/PlatformUtils.h)
    target_link_libraries(imgtotable PRIVATE imgtotable_core Qt6::Core Qt6::Gui Qt6::Widgets)
    imgtotable_warnings(imgtotable)
    set_target_properties(imgtotable PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "ImgToTable"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

    if(WIN32)
        set(modelDirectory "$<TARGET_FILE_DIR:imgtotable>/models")
        target_compile_definitions(imgtotable PRIVATE IMGTOTABLE_MODEL_RELATIVE_PATH="models/model.onnx")
    elseif(APPLE)
        set(modelDirectory "$<TARGET_BUNDLE_CONTENT_DIR:imgtotable>/Resources/models")
        target_compile_definitions(imgtotable PRIVATE IMGTOTABLE_MODEL_RELATIVE_PATH="../Resources/models/model.onnx")
        if(ONNXRUNTIME_ROOT)
            # Preserve versioned dylib names/symlinks used by @rpath install names.
            file(GLOB onnxDylibs "${ONNXRUNTIME_ROOT}/lib/libonnxruntime*.dylib")
            add_custom_command(TARGET imgtotable POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:imgtotable>/Frameworks"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${onnxDylibs}
                    "$<TARGET_BUNDLE_CONTENT_DIR:imgtotable>/Frameworks" VERBATIM)
            set_property(TARGET imgtotable APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
        endif()
    else()
        message(FATAL_ERROR "The desktop application targets Windows and macOS")
    endif()

    # Runs on every build, so replacing/removing a model needs no source relink.
    add_custom_target(imgtotable_model ALL
        COMMAND ${CMAKE_COMMAND}
            "-DMODEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/resources/models/model.onnx"
            "-DMODEL_DIRECTORY=${modelDirectory}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyModel.cmake"
        VERBATIM)
    add_dependencies(imgtotable imgtotable_model)
endif()

if(BUILD_TESTING)
    add_executable(inference_tests tests/InferenceEngineTests.cpp)
    target_link_libraries(inference_tests PRIVATE imgtotable_core)
    imgtotable_warnings(inference_tests)
    add_test(NAME inference_errors COMMAND inference_tests)
    set(IMGTOTABLE_TEST_MODEL "" CACHE FILEPATH "Optional float32 Identity ONNX model, shape [1,3]")
    if(IMGTOTABLE_TEST_MODEL)
        add_test(NAME inference_identity COMMAND inference_tests "${IMGTOTABLE_TEST_MODEL}")
    endif()
endif()

if(WIN32 AND ONNXRUNTIME_ROOT)
    foreach(target IN ITEMS imgtotable inference_tests)
        if(TARGET ${target})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${onnxRuntime}" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
        endif()
    endforeach()
endif()
