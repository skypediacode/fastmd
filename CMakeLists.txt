cmake_minimum_required(VERSION 3.20)
project(FastMD VERSION 1.6.5 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Network)

# MD4C - fast CommonMark parser
include(FetchContent)
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG        release-0.5.2
)
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING             OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(md4c)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/DocumentModel.cpp
    src/MarkdownEditor.cpp
    src/MarkdownHighlighter.cpp
    src/PreviewWidget.cpp
    src/TabWidget.cpp
    src/FindReplaceDialog.cpp
    src/PreferencesDialog.cpp
    src/ExportManager.cpp
    resources/resources.qrc
)

if(WIN32)
    list(APPEND SOURCES resources/fastmd.rc)
endif()

add_executable(FastMD WIN32 ${SOURCES})

target_include_directories(FastMD PRIVATE
    src
    ${md4c_SOURCE_DIR}/src   # MD4C headers (md4c.h, md4c-html.h)
)

target_link_libraries(FastMD PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Network
    md4c
    md4c-html
)

if(MSVC)
    target_compile_options(FastMD PRIVATE /W3 /utf-8 $<$<CONFIG:Release>:/O2>)
    target_compile_definitions(FastMD PRIVATE _UNICODE UNICODE)
endif()

# Bundle KaTeX assets next to the executable for offline math rendering in
# Browser Preview / HTML export / PDF export. The in-app preview never uses these.
add_custom_command(TARGET FastMD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/katex"
            "$<TARGET_FILE_DIR:FastMD>/katex"
    COMMENT "Bundling KaTeX assets"
)

# Deploy Qt DLLs next to the executable on Windows
if(WIN32)
    add_custom_command(TARGET FastMD POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
                --no-translations --no-system-d3d-compiler --no-quick-import
                "$<TARGET_FILE:FastMD>"
        COMMENT "Deploying Qt DLLs"
    )
endif()
