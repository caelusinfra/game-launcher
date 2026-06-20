cmake_minimum_required(VERSION 3.20)
project(AisakaLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(LibArchive REQUIRED)

add_executable(${PROJECT_NAME} WIN32
    src/entry.cxx
    src/platform/win32/main_dialog.cxx
    "src/locales.cxx"
    src/http_client.cxx
    
    src/file_deployer.cxx
    src/app_settings.cxx
    "src/platform/win32/registry.cxx"
    src/platform/win32/shortcut.cxx
    src/product_config.cxx
    src/bootstrapper.cxx
    src/framerate.cxx
    src/rpc.cxx
    assets/resource.rc
    app.manifest
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
)

if(WIN32)
    add_compile_definitions(UNICODE _UNICODE)
    add_definitions(-DUNICODE -D_UNICODE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    comctl32
    uxtheme
    dwmapi
    ole32
    shell32
    psapi
    CURL::libcurl
    LibArchive::LibArchive
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
