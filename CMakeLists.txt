cmake_minimum_required(VERSION 3.20)
project(FastMD VERSION 3.7.5 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Release builds carry full debug information so crash dumps resolve to
# functions and source lines. Code generation is unchanged: /Z7 only adds debug
# records to the objects, and /OPT:REF /OPT:ICF restore what /DEBUG would
# otherwise turn off. /PDBALTPATH keeps the build machine's path out of the
# binaries; debuggers match the PDB by name and signature. PDBs never ship in a
# package -- the release scripts archive them and build the Store's .appxsym.
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Z7>)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG:FULL>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>)
endif()

# Partner Center's crash analytics take public symbols only. The linker writes
# them beside the full PDB, with the same signature, as <name>.public.pdb.
function(fastmd_public_symbols target)
    if(MSVC)
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:/PDBSTRIPPED:$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.public.pdb>)
    endif()
endfunction()

set(FASTMD_TARGET_ARCH "" CACHE STRING "Windows target architecture (x64 or arm64)")
if(NOT FASTMD_TARGET_ARCH)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64)$")
        set(FASTMD_TARGET_ARCH "arm64")
    else()
        set(FASTMD_TARGET_ARCH "x64")
    endif()
endif()
string(TOLOWER "${FASTMD_TARGET_ARCH}" FASTMD_TARGET_ARCH)
if(NOT FASTMD_TARGET_ARCH STREQUAL "x64" AND NOT FASTMD_TARGET_ARCH STREQUAL "arm64")
    message(FATAL_ERROR "FASTMD_TARGET_ARCH must be x64 or arm64 (got '${FASTMD_TARGET_ARCH}').")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Network Concurrent CorePrivate)

# KSyntaxHighlighting's ECM configuration queries Qt with qtpaths.  An Arm64
# qtpaths.exe cannot run on an x64 build host, so keep the Arm64 libraries but
# direct that configure-time query to the x64 host Qt tool.
if(WIN32 AND FASTMD_TARGET_ARCH STREQUAL "arm64")
    if(NOT QT_HOST_PATH OR NOT EXISTS "${QT_HOST_PATH}/bin/qtpaths.exe")
        message(FATAL_ERROR "Arm64 builds require QT_HOST_PATH to name an x64 Qt installation containing bin/qtpaths.exe.")
    endif()
    find_package(Qt6 REQUIRED COMPONENTS CoreTools)
    foreach(_qt_tool
            syncqt moc rcc tracepointgen tracegen cmake_automoc_parser qlalr
            qtpaths wasmdeployqt windeployqt qmake)
        foreach(_qt_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            set_property(TARGET Qt6::${_qt_tool} PROPERTY IMPORTED_LOCATION_${_qt_config}
                "${QT_HOST_PATH}/bin/${_qt_tool}.exe")
        endforeach()
        set_property(TARGET Qt6::${_qt_tool} PROPERTY IMPORTED_LOCATION
            "${QT_HOST_PATH}/bin/${_qt_tool}.exe")
    endforeach()
endif()

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
# Lets a table interrupt a paragraph, as in GFM (upstream needs a blank line).
include("${CMAKE_SOURCE_DIR}/cmake/PatchMd4c.cmake")
fastmd_patch_md4c("${CMAKE_SOURCE_DIR}/cmake/patches/md4c-table-interrupts-paragraph.patch")

# Microsoft Edge WebView2 SDK (headers + static loader). The NuGet package is a
# plain zip; we fetch it at configure time and link the *static* loader so no
# WebView2Loader.dll needs to be deployed. The Evergreen WebView2 Runtime that
# actually renders is a machine-wide component installed separately — we never
# bundle Chromium.
set(FASTMD_WEBVIEW2_VERSION "1.0.4078.44")
FetchContent_Declare(
    webview2
    URL "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${FASTMD_WEBVIEW2_VERSION}/microsoft.web.webview2.${FASTMD_WEBVIEW2_VERSION}.nupkg"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(webview2)
set(WEBVIEW2_INCLUDE_DIR "${webview2_SOURCE_DIR}/build/native/include")
set(WEBVIEW2_LOADER_LIB
    "${webview2_SOURCE_DIR}/build/native/${FASTMD_TARGET_ARCH}/WebView2LoaderStatic.lib")
if(NOT EXISTS "${WEBVIEW2_LOADER_LIB}")
    message(FATAL_ERROR "WebView2 static loader is unavailable for ${FASTMD_TARGET_ARCH}: ${WEBVIEW2_LOADER_LIB}")
endif()

# Extra CMake Modules - required by KSyntaxHighlighting's own CMake config.
# ECM is only ever consumed here for its CMake modules/find-modules, so we
# fetch its source tree without configuring it as a CMake subproject (its own
# ECMConfig.cmake assumes an installed prefix, which FetchContent never
# provides) and hand KSyntaxHighlighting a minimal shim config pointing
# straight at the fetched source tree.
FetchContent_Declare(
    extra-cmake-modules
    GIT_REPOSITORY https://github.com/KDE/extra-cmake-modules.git
    GIT_TAG        v6.28.0
    # ECM's test tree has paths longer than MAX_PATH; without this the clone
    # fails on Windows machines that have not enabled Git's long-path support.
    GIT_CONFIG     core.longpaths=true
)
FetchContent_Populate(extra-cmake-modules)
list(APPEND CMAKE_MODULE_PATH
    "${extra-cmake-modules_SOURCE_DIR}/modules"
    "${extra-cmake-modules_SOURCE_DIR}/find-modules"
    "${extra-cmake-modules_SOURCE_DIR}/kde-modules")

set(FASTMD_ECM_SHIM_DIR "${CMAKE_BINARY_DIR}/ecm-shim")
file(MAKE_DIRECTORY "${FASTMD_ECM_SHIM_DIR}")
file(WRITE "${FASTMD_ECM_SHIM_DIR}/ECMConfig.cmake" "
set(ECM_MODULE_DIR \"${extra-cmake-modules_SOURCE_DIR}/modules\")
set(ECM_FIND_MODULE_DIR \"${extra-cmake-modules_SOURCE_DIR}/find-modules\")
set(ECM_KDE_MODULE_DIR \"${extra-cmake-modules_SOURCE_DIR}/kde-modules\")
set(ECM_MODULE_PATH \"\${ECM_MODULE_DIR}\" \"\${ECM_FIND_MODULE_DIR}\" \"\${ECM_KDE_MODULE_DIR}\")
")
file(WRITE "${FASTMD_ECM_SHIM_DIR}/ECMConfigVersion.cmake" "
set(PACKAGE_VERSION \"6.28.0\")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
set(PACKAGE_VERSION_EXACT TRUE)
")
set(ECM_DIR "${FASTMD_ECM_SHIM_DIR}" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "${FASTMD_ECM_SHIM_DIR}")

# KSyntaxHighlighting - Markdown editor syntax highlighting + embedded
# fenced-code-block language definitions. Built as a static library from
# source (matching the WebView2Loader static-link approach) so no extra DLL
# needs to be deployed. Only the core library is built - no CLI tools, no
# tests, no QCH docs.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_QCH OFF CACHE BOOL "" FORCE)
set(KSYNTAXHIGHLIGHTING_USE_GUI OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ksyntaxhighlighting
    GIT_REPOSITORY https://github.com/KDE/syntax-highlighting.git
    GIT_TAG        v6.28.0
)
FetchContent_MakeAvailable(ksyntaxhighlighting)

set(SOURCES
    src/main.cpp
    src/CliExport.cpp
    src/QtGpuProbe.cpp
    src/CliExportArguments.cpp
    src/StartupSettings.cpp
    src/ApplicationTheme.cpp
    src/MainWindow.cpp
    src/DocumentModel.cpp
    src/RecoveryManager.cpp
    src/RecentFilesManager.cpp
    src/RecentFoldersManager.cpp
    src/ShellWarmUp.cpp
    src/WorkspacePanelController.cpp
    src/WorkspacePanelControllerSearch.cpp
    src/WorkspacePanelControllerOperations.cpp
    src/WorkspaceTreeView.cpp
    src/WorkspaceTreeFilterModel.cpp
    src/WorkspaceNavigationModel.cpp
    src/WorkspaceRenameDelegate.cpp
    src/WorkspaceFileOps.cpp
    src/WorkspaceTextSearch.cpp
    src/WorkspaceSearchFiles.cpp
    src/WorkspaceSearchPanel.cpp
    src/GitTools.cpp
    src/NativeTreeContainer.cpp
    src/ThemedSplitter.cpp
    src/UpdateChecker.cpp
    src/RatePrompt.cpp
    src/UpdateProgressDialog.cpp
    src/MainWindowTextIO.cpp
    src/MainWindowFile.cpp
    src/ImportManager.cpp
    src/MainWindowExport.cpp
    src/MainWindowSession.cpp
    src/MainWindowSpellCheck.cpp
    src/MainWindowAutoSave.cpp
    src/MainWindowTabs.cpp
    src/MainWindowCommands.cpp
    src/CommandRegistry.cpp
    src/CommandPaletteLauncher.cpp
    src/CommandPalettePopup.cpp
    src/TextTransforms.cpp
    src/MarkdownFormatting.cpp
    src/MarkdownInlineFormatting.cpp
    src/MarkdownMathFormatting.cpp
    src/MarkdownToc.cpp
    src/MarkdownExtensions.cpp
    src/MarkdownEditor.cpp
    src/GutterSelection.cpp
    src/MenuCommandBar.cpp
    src/MathSymbols.cpp
    src/MathContext.cpp
    src/MathSymbolPopup.cpp
    src/ChemistrySymbols.cpp
    src/ChemistryPopup.cpp
    src/ToolbarPopupCoordinator.cpp
    src/ToolbarHoverAnimation.cpp
    src/ToolbarSvgButton.cpp
    src/ToolbarOverflow.cpp
    src/FlowLayout.cpp
    src/ChemistryToolbar.cpp
    src/MathToolbar.cpp
    src/MarkdownHighlighter.cpp
    src/FenceLanguageAliases.cpp
    src/SourceHighlighter.cpp
    src/SyntaxDefinitionResolver.cpp
    src/SyntaxHighlightingRepository.cpp
    src/LanguageSelectorPopup.cpp
    src/PreviewWidget.cpp
    src/PreviewPatch.cpp
    src/WebView2Environment.cpp
    src/ConPtySession.cpp
    src/TerminalProfiles.cpp
    src/TerminalLinks.cpp
    src/TerminalSession.cpp
    src/TerminalController.cpp
    src/TerminalView.cpp
    src/TerminalPanel.cpp
    src/TerminalCommands.cpp
    src/PreferencesTargets.cpp
    src/DetachedTerminalWindow.cpp
    src/NativeWindowFrame.cpp
    src/WindowTitleBar.cpp
    src/ImageFormats.cpp
    src/ImageViewer.cpp
    src/ImageViewerToolbar.cpp
    src/OutlinePopup.cpp
    src/OutlineModel.cpp
    src/TabWidget.cpp
    src/TabDragController.cpp
    src/FindController.cpp
    src/FindOverlay.cpp
    src/GoToLineDialog.cpp
    src/FixAllMisspellingsDialog.cpp
    src/SpellChecker.cpp
    src/SpellCheckExclusions.cpp
    src/NavigationHistory.cpp
    src/MatrixInsertDialog.cpp
    src/DocumentSummary.cpp
    src/DocumentSummaryDialog.cpp
    src/PreferencesDialog.cpp
    src/SearchableComboBox.cpp
    src/HeaderFooter.cpp
    src/HeaderFooterDialog.cpp
    src/ExportManager.cpp
    src/ExportManagerMarkdown.cpp
    src/ExportManagerPdf.cpp
    src/PdfExportHost.cpp
    src/ExportManagerPandoc.cpp
    src/MarkdownPreviewRenderer.cpp
    src/MathRenderer.cpp
    src/LicensesDialog.cpp
    src/WhatsNew.cpp
    src/AboutDialog.cpp
    src/ShortcutsDialog.cpp
    src/CommandShortcutRegistry.cpp
    src/GitRunner.cpp
    src/GitBranchSelector.cpp
    src/GitRepository.cpp
    src/GitDiffView.cpp
    src/SourceControlPanel.cpp
    src/SourceControlPanelHistory.cpp
    src/SourceControlPanelSync.cpp
    src/AlignedMenu.h
    resources/resources.qrc
)

if(WIN32)
    # Generate the version resource from project(FastMD VERSION ...) so the
    # executable reports a version at all -- it previously shipped with no
    # VERSIONINFO, leaving Explorer's Details tab and every benchmark or
    # packaging script that reads ProductVersion with an empty string.
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/fastmd.ico" FASTMD_RC_ICON)
    set(FASTMD_GENERATED_RC "${CMAKE_CURRENT_BINARY_DIR}/fastmd.rc")
    configure_file(resources/fastmd.rc.in "${FASTMD_GENERATED_RC}" @ONLY)
    list(APPEND SOURCES "${FASTMD_GENERATED_RC}")
endif()

# Include the committed Math Toolbar SVG/PNG tree in the Qt resource system.
# These are pre-generated, checked-in assets, not produced at configure/build
# time: icons rarely change, so regenerating them (scripts/generate-math-icons.py,
# generate-function-icons.py, generate-matrices-icons.py, generate-icon-pngs.py)
# is a manual step a developer runs and commits the results of, same as any
# other checked-in asset. Each icon's baked master PNG twin is preferred by
# IconHelper over live SVG rendering when present -- see generatedMathSvgIcon().
file(GLOB_RECURSE FASTMD_MATH_ICON_ASSETS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/icons/math/*.png")
set(FASTMD_MATH_ICONS_QRC "${CMAKE_CURRENT_BINARY_DIR}/fastmd_math_icons.qrc")
file(WRITE "${FASTMD_MATH_ICONS_QRC}" "<RCC>\n  <qresource prefix=\"/\">\n")
foreach(_math_icon IN LISTS FASTMD_MATH_ICON_ASSETS)
    file(RELATIVE_PATH _math_icon_rel
        "${CMAKE_SOURCE_DIR}/resources/icons/math" "${_math_icon}")
    file(TO_CMAKE_PATH "${_math_icon}" _math_icon_path)
    file(APPEND "${FASTMD_MATH_ICONS_QRC}"
        "    <file alias=\"icons/math/${_math_icon_rel}\">${_math_icon_path}</file>\n")
endforeach()
file(APPEND "${FASTMD_MATH_ICONS_QRC}" "  </qresource>\n</RCC>\n")
list(APPEND SOURCES "${FASTMD_MATH_ICONS_QRC}")

add_executable(FastMD WIN32 ${SOURCES})
# Console subsystem so shells wait for `fastmd a.md -o a.pdf` and see its exit
# code; the manifest's detached console policy keeps GUI launches console-free.
add_executable(FastMDLauncher src/FastMDLauncher.cpp src/FastMDLauncher.manifest)

if(WIN32)
    add_library(FastMDShellExtension SHARED
        src/shellext/FastMDShellExtension.cpp
        src/shellext/FastMDShellExtension.rc)
    set_target_properties(FastMDShellExtension PROPERTIES
        OUTPUT_NAME "FastMDShellExtension"
        LINK_FLAGS "/DEF:${CMAKE_SOURCE_DIR}/src/shellext/FastMDShellExtension.def")
    target_include_directories(FastMDShellExtension PRIVATE src)
    target_link_libraries(FastMDShellExtension PRIVATE ole32 shlwapi shell32 runtimeobject)
    if(MSVC)
        target_compile_options(FastMDShellExtension PRIVATE /W3 /utf-8 $<$<CONFIG:Release>:/O2>)
    endif()
    target_compile_definitions(FastMDShellExtension PRIVATE _UNICODE UNICODE)
endif()

option(FASTMD_BUILD_TESTS "Build FastMD focused regression tests" OFF)
if(FASTMD_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(NavigationHistoryTest tests/NavigationHistoryTest.cpp src/NavigationHistory.cpp)
    target_include_directories(NavigationHistoryTest PRIVATE src)
    target_link_libraries(NavigationHistoryTest PRIVATE Qt6::Core)

    add_executable(MarkdownFormattingTest tests/MarkdownFormattingTest.cpp src/TextTransforms.cpp)
    target_include_directories(MarkdownFormattingTest PRIVATE src)
    target_link_libraries(MarkdownFormattingTest PRIVATE Qt6::Core)

    add_executable(GutterSelectionTest tests/GutterSelectionTest.cpp src/GutterSelection.cpp)
    target_include_directories(GutterSelectionTest PRIVATE src)
    target_link_libraries(GutterSelectionTest PRIVATE Qt6::Core Qt6::Gui)

    add_executable(MarkdownInlineFormattingTest tests/MarkdownInlineFormattingTest.cpp
        src/MarkdownInlineFormatting.cpp)
    target_include_directories(MarkdownInlineFormattingTest PRIVATE src)
    target_link_libraries(MarkdownInlineFormattingTest PRIVATE Qt6::Core Qt6::Gui)

    add_executable(MarkdownTableFormatTest tests/MarkdownTableFormatTest.cpp src/TextTransforms.cpp)
    target_include_directories(MarkdownTableFormatTest PRIVATE src)
    target_link_libraries(MarkdownTableFormatTest PRIVATE Qt6::Core)

    add_executable(TextTransformsLineOpsTest tests/TextTransformsLineOpsTest.cpp src/TextTransforms.cpp)
    target_include_directories(TextTransformsLineOpsTest PRIVATE src)
    target_link_libraries(TextTransformsLineOpsTest PRIVATE Qt6::Core)

    add_executable(MarkdownNumberingTest tests/MarkdownNumberingTest.cpp src/TextTransforms.cpp)
    target_include_directories(MarkdownNumberingTest PRIVATE src)
    target_link_libraries(MarkdownNumberingTest PRIVATE Qt6::Core)

    add_executable(SpellCheckExclusionsTest tests/SpellCheckExclusionsTest.cpp
        src/SpellCheckExclusions.cpp)
    target_include_directories(SpellCheckExclusionsTest PRIVATE src)
    target_link_libraries(SpellCheckExclusionsTest PRIVATE Qt6::Core)

    add_executable(PreviewPatchTest tests/PreviewPatchTest.cpp src/PreviewPatch.cpp)
    target_include_directories(PreviewPatchTest PRIVATE src)
    target_link_libraries(PreviewPatchTest PRIVATE Qt6::Core)

    add_executable(GitParsingTest tests/GitParsingTest.cpp src/GitRepository.cpp)
    target_include_directories(GitParsingTest PRIVATE src)
    target_link_libraries(GitParsingTest PRIVATE Qt6::Core)

    add_executable(WorkspaceTextSearchTest tests/WorkspaceTextSearchTest.cpp
        src/WorkspaceTextSearch.cpp)
    target_include_directories(WorkspaceTextSearchTest PRIVATE src)
    target_link_libraries(WorkspaceTextSearchTest PRIVATE Qt6::Core)

    add_executable(WorkspaceSearchFilesTest tests/WorkspaceSearchFilesTest.cpp
        src/WorkspaceSearchFiles.cpp src/WorkspaceTextSearch.cpp src/MainWindowTextIO.cpp
        src/SyntaxDefinitionResolver.cpp src/SyntaxHighlightingRepository.cpp)
    target_include_directories(WorkspaceSearchFilesTest PRIVATE src)
    target_link_libraries(WorkspaceSearchFilesTest PRIVATE Qt6::Core Qt6::Gui KF6SyntaxHighlighting)
    target_compile_definitions(WorkspaceSearchFilesTest PRIVATE
        FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR="${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting")

    add_executable(WorkspaceSearchPanelTest tests/WorkspaceSearchPanelTest.cpp
        src/WorkspaceSearchPanel.cpp src/WorkspaceTextSearch.cpp src/WorkspaceSearchFiles.cpp
        src/MainWindowTextIO.cpp src/SyntaxDefinitionResolver.cpp
        src/SyntaxHighlightingRepository.cpp src/ApplicationTheme.cpp src/AlignedMenu.h)
    target_include_directories(WorkspaceSearchPanelTest PRIVATE src)
    target_link_libraries(WorkspaceSearchPanelTest PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Test
        KF6SyntaxHighlighting dwmapi)
    target_compile_definitions(WorkspaceSearchPanelTest PRIVATE
        FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR="${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting"
        FASTMD_QT_PLATFORMS_DIR="$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")

    add_executable(TerminalProfilesTest tests/TerminalProfilesTest.cpp src/TerminalProfiles.cpp)
    target_include_directories(TerminalProfilesTest PRIVATE src)
    target_link_libraries(TerminalProfilesTest PRIVATE Qt6::Core)

    add_executable(TerminalLinksTest tests/TerminalLinksTest.cpp src/TerminalLinks.cpp)
    target_include_directories(TerminalLinksTest PRIVATE src)
    target_link_libraries(TerminalLinksTest PRIVATE Qt6::Core)

    add_executable(TerminalShellIntegrationTest tests/TerminalShellIntegrationTest.cpp
        src/ConPtySession.cpp src/TerminalProfiles.cpp src/TerminalLinks.cpp)
    target_include_directories(TerminalShellIntegrationTest PRIVATE src)
    target_link_libraries(TerminalShellIntegrationTest PRIVATE Qt6::Core)

    add_executable(TerminalVisibilityPersistenceTest tests/TerminalVisibilityPersistenceTest.cpp)
    target_include_directories(TerminalVisibilityPersistenceTest PRIVATE src)
    target_link_libraries(TerminalVisibilityPersistenceTest PRIVATE Qt6::Core)

    add_executable(TerminalCursorStyleTest tests/TerminalCursorStyleTest.cpp)
    target_link_libraries(TerminalCursorStyleTest PRIVATE Qt6::Core)
    target_compile_definitions(TerminalCursorStyleTest PRIVATE
        FASTMD_TERMINAL_CSS_PATH="${CMAKE_SOURCE_DIR}/resources/terminal/terminal.css")

    add_executable(TerminalHostLayoutTest tests/TerminalHostLayoutTest.cpp)
    target_link_libraries(TerminalHostLayoutTest PRIVATE Qt6::Core)
    target_compile_definitions(TerminalHostLayoutTest PRIVATE
        FASTMD_TERMINAL_CSS_PATH="${CMAKE_SOURCE_DIR}/resources/terminal/terminal.css"
        FASTMD_STYLESHEET_PATH="${CMAKE_SOURCE_DIR}/src/Stylesheet.h")

    add_executable(TerminalConPtyInputTest tests/TerminalConPtyInputTest.cpp)

    add_executable(ReadOnlyItemViewCursorTest tests/ReadOnlyItemViewCursorTest.cpp)
    target_include_directories(ReadOnlyItemViewCursorTest PRIVATE src)
    target_link_libraries(ReadOnlyItemViewCursorTest PRIVATE Qt6::Widgets)

    add_executable(NativeWindowCursorSyncTest tests/NativeWindowCursorSyncTest.cpp)
    target_include_directories(NativeWindowCursorSyncTest PRIVATE src)
    target_link_libraries(NativeWindowCursorSyncTest PRIVATE Qt6::Widgets)

    add_executable(ProportionalSplitterGeometryTest tests/ProportionalSplitterGeometryTest.cpp)
    target_include_directories(ProportionalSplitterGeometryTest PRIVATE src)
    target_link_libraries(ProportionalSplitterGeometryTest PRIVATE Qt6::Core)

    add_executable(WindowChromeTest tests/WindowChromeTest.cpp src/NativeWindowFrame.cpp)
    target_include_directories(WindowChromeTest PRIVATE src)
    target_link_libraries(WindowChromeTest PRIVATE Qt6::Core)

    add_executable(MenuShortcutContextTest tests/MenuShortcutContextTest.cpp src/WindowTitleBar.cpp)
    target_include_directories(MenuShortcutContextTest PRIVATE src)
    target_link_libraries(MenuShortcutContextTest PRIVATE Qt6::Widgets Qt6::Test)

    add_executable(DialogKeyTest tests/DialogKeyTest.cpp src/ApplicationTheme.cpp)
    target_include_directories(DialogKeyTest PRIVATE src)
    target_link_libraries(DialogKeyTest PRIVATE Qt6::Widgets Qt6::Test dwmapi)

    add_executable(ApplicationThemeSwitchTest tests/ApplicationThemeSwitchTest.cpp src/ApplicationTheme.cpp)
    target_include_directories(ApplicationThemeSwitchTest PRIVATE src)
    target_link_libraries(ApplicationThemeSwitchTest PRIVATE Qt6::Widgets)

    add_executable(TerminalDialogThemeTest tests/TerminalDialogThemeTest.cpp src/ApplicationTheme.cpp)
    target_include_directories(TerminalDialogThemeTest PRIVATE src)
    target_link_libraries(TerminalDialogThemeTest PRIVATE Qt6::Widgets dwmapi)

    add_executable(MarkdownExtensionsTest tests/MarkdownExtensionsTest.cpp
        src/MarkdownExtensions.cpp src/MarkdownHighlighter.cpp src/FenceLanguageAliases.cpp
        src/SyntaxHighlightingRepository.cpp src/MarkdownToc.cpp)
    target_include_directories(MarkdownExtensionsTest PRIVATE src ${md4c_SOURCE_DIR}/src)
    target_link_libraries(MarkdownExtensionsTest PRIVATE Qt6::Core Qt6::Gui md4c-html KF6SyntaxHighlighting)
    target_compile_definitions(MarkdownExtensionsTest PRIVATE
        FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR="${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting")

    add_executable(SyntaxDefinitionResolverTest tests/SyntaxDefinitionResolverTest.cpp
        src/SyntaxDefinitionResolver.cpp src/SyntaxHighlightingRepository.cpp)
    target_include_directories(SyntaxDefinitionResolverTest PRIVATE src)
    target_link_libraries(SyntaxDefinitionResolverTest PRIVATE Qt6::Core Qt6::Gui KF6SyntaxHighlighting)
    target_compile_definitions(SyntaxDefinitionResolverTest PRIVATE
        FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR="${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting")

    add_executable(EditorLanguageSwitchTest tests/EditorLanguageSwitchTest.cpp
        src/SourceHighlighter.cpp src/SyntaxDefinitionResolver.cpp
        src/SyntaxHighlightingRepository.cpp)
    target_include_directories(EditorLanguageSwitchTest PRIVATE src)
    target_link_libraries(EditorLanguageSwitchTest PRIVATE Qt6::Core Qt6::Gui KF6SyntaxHighlighting)
    target_compile_definitions(EditorLanguageSwitchTest PRIVATE
        FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR="${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting")

    add_executable(SyntaxRepositoryCoverageTest tests/SyntaxRepositoryCoverageTest.cpp
        src/SyntaxHighlightingRepository.cpp)
    target_include_directories(SyntaxRepositoryCoverageTest PRIVATE src)
    target_link_libraries(SyntaxRepositoryCoverageTest PRIVATE Qt6::Core Qt6::Gui KF6SyntaxHighlighting)
    target_compile_definitions(SyntaxRepositoryCoverageTest PRIVATE
        FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR="${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting")

    add_executable(FenceLanguageAliasTest tests/FenceLanguageAliasTest.cpp
        src/FenceLanguageAliases.cpp src/SyntaxHighlightingRepository.cpp)
    target_include_directories(FenceLanguageAliasTest PRIVATE src)
    target_link_libraries(FenceLanguageAliasTest PRIVATE Qt6::Core Qt6::Gui KF6SyntaxHighlighting)
    target_compile_definitions(FenceLanguageAliasTest PRIVATE
        FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR="${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting")

    add_executable(MarkdownCompatibilityTest tests/MarkdownCompatibilityTest.cpp
        src/ExportManager.cpp src/HeaderFooter.cpp
        src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp src/MarkdownToc.cpp
        src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp src/PreviewPatch.cpp)
    target_include_directories(MarkdownCompatibilityTest PRIVATE src ${md4c_SOURCE_DIR}/src)
    target_link_libraries(MarkdownCompatibilityTest PRIVATE Qt6::Core Qt6::Gui md4c md4c-html)
    target_compile_definitions(MarkdownCompatibilityTest PRIVATE
        FASTMD_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

    # Kept separate from MarkdownCompatibilityTest: ExportManagerPdf.cpp drags in
    # Widgets/PrintSupport, and this test drives a real headless browser.
    add_executable(PdfMetadataTest tests/PdfMetadataTest.cpp
        src/ExportManager.cpp src/ExportManagerPdf.cpp src/PdfExportHost.cpp src/HeaderFooter.cpp
        src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp src/MarkdownToc.cpp
        src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp src/ApplicationTheme.cpp)
    target_include_directories(PdfMetadataTest PRIVATE src ${md4c_SOURCE_DIR}/src ${WEBVIEW2_INCLUDE_DIR})
    target_link_libraries(PdfMetadataTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets
        Qt6::PrintSupport md4c md4c-html dwmapi ole32 oleaut32 runtimeobject
        "${WEBVIEW2_LOADER_LIB}" version)
    target_compile_definitions(PdfMetadataTest PRIVATE
        FASTMD_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
        FASTMD_PDF_TEST_HOOKS)

    add_executable(HeaderFooterTest tests/HeaderFooterTest.cpp
        src/HeaderFooter.cpp src/StartupSettings.cpp
        src/ExportManager.cpp src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp
        src/MarkdownToc.cpp src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp
        src/TextTransforms.cpp)
    target_include_directories(HeaderFooterTest PRIVATE src ${md4c_SOURCE_DIR}/src)
    target_link_libraries(HeaderFooterTest PRIVATE Qt6::Core Qt6::Gui md4c md4c-html)

    add_executable(SearchableComboBoxTest tests/SearchableComboBoxTest.cpp
        src/SearchableComboBox.cpp src/ApplicationTheme.cpp)
    target_include_directories(SearchableComboBoxTest PRIVATE src)
    target_compile_definitions(SearchableComboBoxTest PRIVATE
        FASTMD_QT_PLATFORMS_DIR="$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
    target_link_libraries(SearchableComboBoxTest PRIVATE Qt6::Widgets Qt6::Test)

    add_executable(FontPreferencesTest tests/FontPreferencesTest.cpp
        src/StartupSettings.cpp src/HeaderFooter.cpp
        src/ExportManager.cpp src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp
        src/MarkdownToc.cpp src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp
        src/TextTransforms.cpp)
    target_include_directories(FontPreferencesTest PRIVATE src ${md4c_SOURCE_DIR}/src)
    target_compile_definitions(FontPreferencesTest PRIVATE
        FASTMD_PREVIEW_CSS="${CMAKE_SOURCE_DIR}/resources/preview/preview.css")
    target_link_libraries(FontPreferencesTest PRIVATE Qt6::Core Qt6::Gui md4c md4c-html)

    add_executable(DocumentSpacingTest tests/DocumentSpacingTest.cpp
        src/StartupSettings.cpp src/HeaderFooter.cpp
        src/ExportManager.cpp src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp
        src/MarkdownToc.cpp src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp
        src/TextTransforms.cpp)
    target_include_directories(DocumentSpacingTest PRIVATE src ${md4c_SOURCE_DIR}/src)
    target_compile_definitions(DocumentSpacingTest PRIVATE
        FASTMD_PREVIEW_CSS="${CMAKE_SOURCE_DIR}/resources/preview/preview.css")
    target_link_libraries(DocumentSpacingTest PRIVATE Qt6::Core Qt6::Gui md4c md4c-html)

    # Writes real WebView2 PDFs (and print HTML for a header/footer-enabled
    # browser) for tests/pdf/headerfootercases.py to inspect.
    add_executable(PdfHeaderFooterTest tests/PdfHeaderFooterTest.cpp
        src/ExportManager.cpp src/ExportManagerPdf.cpp src/PdfExportHost.cpp src/HeaderFooter.cpp
        src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp src/MarkdownToc.cpp
        src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp src/ApplicationTheme.cpp)
    target_include_directories(PdfHeaderFooterTest PRIVATE src ${md4c_SOURCE_DIR}/src ${WEBVIEW2_INCLUDE_DIR})
    target_link_libraries(PdfHeaderFooterTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets
        Qt6::PrintSupport md4c md4c-html dwmapi ole32 oleaut32 runtimeobject
        "${WEBVIEW2_LOADER_LIB}" version)

    # Writes real PDFs from each export path for tests/pdf/outlinecases.py,
    # which checks their bookmarks against the rendered headings.
    add_executable(PdfOutlineTest tests/PdfOutlineTest.cpp
        src/ExportManager.cpp src/ExportManagerPdf.cpp src/PdfExportHost.cpp src/HeaderFooter.cpp
        src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp src/MarkdownToc.cpp
        src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp src/ApplicationTheme.cpp)
    target_include_directories(PdfOutlineTest PRIVATE src ${md4c_SOURCE_DIR}/src ${WEBVIEW2_INCLUDE_DIR})
    target_link_libraries(PdfOutlineTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets
        Qt6::PrintSupport md4c md4c-html dwmapi ole32 oleaut32 runtimeobject
        "${WEBVIEW2_LOADER_LIB}" version)
    target_compile_definitions(PdfOutlineTest PRIVATE FASTMD_PDF_TEST_HOOKS)

    add_executable(MarkdownSoftBreakTest tests/MarkdownSoftBreakTest.cpp
        src/ExportManager.cpp src/HeaderFooter.cpp src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp
        src/MarkdownToc.cpp src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp
        src/TextTransforms.cpp)
    target_include_directories(MarkdownSoftBreakTest PRIVATE src ${md4c_SOURCE_DIR}/src)
    target_link_libraries(MarkdownSoftBreakTest PRIVATE Qt6::Core Qt6::Gui md4c md4c-html)

    add_executable(DocumentSummaryTest tests/DocumentSummaryTest.cpp
        src/DocumentSummary.cpp src/MathRenderer.cpp)
    target_include_directories(DocumentSummaryTest PRIVATE src ${md4c_SOURCE_DIR}/src)
    target_link_libraries(DocumentSummaryTest PRIVATE Qt6::Core Qt6::Gui md4c)

    add_executable(ImageFormatSupportTest tests/ImageFormatSupportTest.cpp
        src/ImageFormats.cpp src/DocumentModel.cpp)
    target_include_directories(ImageFormatSupportTest PRIVATE src)
    target_link_libraries(ImageFormatSupportTest PRIVATE Qt6::Core Qt6::Gui)

    add_executable(QtGpuProbeTest tests/QtGpuProbeTest.cpp src/QtGpuProbe.cpp)
    target_include_directories(QtGpuProbeTest PRIVATE src)
    target_link_libraries(QtGpuProbeTest PRIVATE Qt6::Widgets)

    add_executable(IconHelperDpiTest tests/IconHelperDpiTest.cpp)
    target_include_directories(IconHelperDpiTest PRIVATE src)
    target_compile_definitions(IconHelperDpiTest PRIVATE
        FASTMD_FONT_PATH="${CMAKE_SOURCE_DIR}/resources/fonts/MaterialIcons-Regular.ttf")
    target_link_libraries(IconHelperDpiTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

    # WorkspaceFileOps (and its ImageFormats dependency) come along because the
    # import destination reuses uniqueDestinationPath rather than growing its
    # own duplicate-name logic.
    add_executable(ImportManagerTest tests/ImportManagerTest.cpp src/ImportManager.cpp
        src/WorkspaceFileOps.cpp src/ImageFormats.cpp)
    target_include_directories(ImportManagerTest PRIVATE src)
    target_link_libraries(ImportManagerTest PRIVATE Qt6::Core Qt6::Gui)
    if(WIN32)
        target_link_libraries(ImportManagerTest PRIVATE shell32 ole32)
    endif()

    add_executable(DocumentSaveCoordinatorTest tests/DocumentSaveCoordinatorTest.cpp)
    target_include_directories(DocumentSaveCoordinatorTest PRIVATE src)
    target_link_libraries(DocumentSaveCoordinatorTest PRIVATE Qt6::Core)

    add_executable(CliExportArgumentsTest tests/CliExportArgumentsTest.cpp src/CliExportArguments.cpp)
    target_include_directories(CliExportArgumentsTest PRIVATE src)
    target_link_libraries(CliExportArgumentsTest PRIVATE Qt6::Core)

    add_executable(WhatsNewTest tests/WhatsNewTest.cpp src/WhatsNew.cpp)
    target_include_directories(WhatsNewTest PRIVATE src)
    target_link_libraries(WhatsNewTest PRIVATE Qt6::Core)

    add_executable(CommandShortcutRegistryTest tests/CommandShortcutRegistryTest.cpp src/CommandShortcutRegistry.cpp)
    target_include_directories(CommandShortcutRegistryTest PRIVATE src)
    target_link_libraries(CommandShortcutRegistryTest PRIVATE Qt6::Core Qt6::Gui)

    add_executable(TerminalCommandsTest tests/TerminalCommandsTest.cpp
        src/TerminalCommands.cpp src/CommandRegistry.cpp)
    target_include_directories(TerminalCommandsTest PRIVATE src)
    target_link_libraries(TerminalCommandsTest PRIVATE Qt6::Widgets)

    add_executable(CommandPaletteChoicesTest tests/CommandPaletteChoicesTest.cpp
        src/CommandPalettePopup.cpp src/CommandPaletteLauncher.cpp src/CommandRegistry.cpp)
    target_include_directories(CommandPaletteChoicesTest PRIVATE src)
    target_link_libraries(CommandPaletteChoicesTest PRIVATE Qt6::Widgets Qt6::Test)

    # Profiling harness for the editor -> markdown -> WebView2 push pipeline.
    add_executable(PreviewPipelineBench tests/PreviewPipelineBench.cpp
        src/ExportManager.cpp src/HeaderFooter.cpp
        src/ExportManagerMarkdown.cpp src/MarkdownExtensions.cpp src/MarkdownToc.cpp
        src/MarkdownPreviewRenderer.cpp src/MathRenderer.cpp src/DocumentSummary.cpp
        src/PreviewPatch.cpp)
    target_include_directories(PreviewPipelineBench PRIVATE src ${md4c_SOURCE_DIR}/src)
    target_link_libraries(PreviewPipelineBench PRIVATE Qt6::Core Qt6::Gui md4c md4c-html)
endif()

target_include_directories(FastMD PRIVATE
    src
    ${md4c_SOURCE_DIR}/src   # MD4C headers (md4c.h, md4c-html.h)
    ${WEBVIEW2_INCLUDE_DIR}  # WebView2.h, WebView2EnvironmentOptions.h
)

target_link_libraries(FastMD PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Network
    Qt6::Concurrent
    Qt6::CorePrivate
    md4c
    md4c-html
    KF6SyntaxHighlighting
)

if(WIN32)
    target_link_libraries(FastMD PRIVATE dwmapi shell32 ole32 oleaut32 runtimeobject
        "${WEBVIEW2_LOADER_LIB}" version)
    target_link_libraries(FastMDLauncher PRIVATE shell32)
endif()

if(MSVC)
    target_compile_options(FastMD PRIVATE /W3 /utf-8 $<$<CONFIG:Release>:/O2>)
    target_compile_definitions(FastMD PRIVATE _UNICODE UNICODE)
    target_compile_options(FastMDLauncher PRIVATE /W3 /utf-8 $<$<CONFIG:Release>:/O2>)
    target_compile_definitions(FastMDLauncher PRIVATE _UNICODE UNICODE)
endif()
fastmd_public_symbols(FastMD)
fastmd_public_symbols(FastMDLauncher)
if(TARGET FastMDShellExtension)
    fastmd_public_symbols(FastMDShellExtension)
endif()

# Bundle KaTeX assets next to the executable for offline math rendering in
# Browser Preview / HTML export / PDF export. The in-app preview never uses these.
add_custom_command(TARGET FastMD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/katex"
            "$<TARGET_FILE_DIR:FastMD>/katex"
    COMMENT "Bundling KaTeX assets"
)

# Bundle the WebView2 preview shell (HTML/CSS/JS + highlight.js) next to the
# executable. WebView2 serves these from a virtual-host folder mapping, so they
# must exist on disk (Qt :/resources are not reachable by the WebView2 process).
# Everything here is offline; the preview never needs network access.
file(GLOB_RECURSE FASTMD_PREVIEW_ASSETS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/preview/*")
add_custom_target(FastMDPreviewAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/preview"
            "$<TARGET_FILE_DIR:FastMD>/preview"
    DEPENDS ${FASTMD_PREVIEW_ASSETS}
    COMMENT "Bundling WebView2 preview shell"
)
add_dependencies(FastMD FastMDPreviewAssets)

# Bundle the integrated terminal's shell (xterm.js + addons + the FastMD page)
# next to the executable, for the same reason as the preview: WebView2 serves it
# through a virtual-host folder mapping and cannot read Qt :/resources. Fully
# offline -- no CDN, and no Node/npm at runtime.
file(GLOB_RECURSE FASTMD_TERMINAL_ASSETS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/terminal/*")
add_custom_target(FastMDTerminalAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/terminal"
            "$<TARGET_FILE_DIR:FastMD>/terminal"
    DEPENDS ${FASTMD_TERMINAL_ASSETS}
    COMMENT "Bundling WebView2 terminal shell"
)
add_dependencies(FastMD FastMDTerminalAssets)

# Bundle Pandoc (external tool, invoked via QProcess for "Export DOCX"; never
# linked as a library) next to the executable. tools/pandoc/pandoc.exe itself
# is not checked into source control - see tools/pandoc/NOTICE.txt.
# Plain copy_directory, deliberately: copy_directory_if_different was measured
# at 0.76s vs 0.35s here, because hashing ~230MB of Pandoc costs more than the
# filesystem's block copy. Do not "optimize" this to the _if_different form.
add_custom_command(TARGET FastMD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/tools/pandoc"
            "$<TARGET_FILE_DIR:FastMD>/tools/pandoc"
    COMMENT "Bundling Pandoc"
)

# Bundle the native AnyDoc wrapper used only by File > Import as Markdown. Like
# Pandoc, the executable is built by scripts/sync-bundled-tools.ps1 rather than
# checked in, so this copies nothing until that has run.
add_custom_command(TARGET FastMD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/tools/anydoc/${FASTMD_TARGET_ARCH}"
            "$<TARGET_FILE_DIR:FastMD>/tools/anydoc"
    COMMENT "Bundling AnyDoc importer"
)

# Bundle the ODT reference document (Pandoc's default reference.odt plus a
# FastMDCenteredMath paragraph style) used for --reference-doc on ODT export,
# so display math renders centered even in viewers (e.g. Microsoft Word) that
# flatten Pandoc's centered math frame back to a plain left-aligned paragraph.
add_custom_command(TARGET FastMD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/odt"
            "$<TARGET_FILE_DIR:FastMD>/odt"
    COMMENT "Bundling ODT reference document"
)

# Bundle the DOCX reference document (Pandoc's default reference.docx with the
# "Table" style given a bordered grid + header shading) used for
# --reference-doc on DOCX export, so exported tables match the app's HTML/PDF
# table styling instead of Pandoc's borderless default.
add_custom_command(TARGET FastMD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/docx"
            "$<TARGET_FILE_DIR:FastMD>/docx"
    COMMENT "Bundling DOCX reference document"
)

# Bundle the FastMD export-styling assets next to the executable. Each gives the
# corresponding Pandoc format the app's visual identity (see ExportManager):
#   epub/  -> --css stylesheet         latex/ -> --include-in-header
#   rtf/   -> --template
# (odt/ and docx/ are bundled just above.) All are optional at runtime:
# ExportManager falls back to Pandoc's defaults if an asset is missing.
foreach(_fmtdir epub latex rtf)
    add_custom_command(TARGET FastMD POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resources/${_fmtdir}"
                "$<TARGET_FILE_DIR:FastMD>/${_fmtdir}"
        COMMENT "Bundling ${_fmtdir} export styling")
endforeach()

# Bundle KSyntaxHighlighting syntax definitions (FastMD's own Markdown
# definition plus the upstream per-language definitions used for fenced code
# block highlighting) and theme files next to the executable. This must be a
# build dependency rather than a POST_BUILD action: otherwise editing only a
# syntax/theme file leaves an already-built executable using stale copied data.
# KSyntaxHighlighting resolves these via Repository::addCustomSearchPath at
# runtime, not via the Qt resource system.
file(GLOB_RECURSE FASTMD_KSYNTAX_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting/syntax/*"
    "${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting/themes/*")
set(FASTMD_KSYNTAX_RESOURCES_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/fastmd-ksyntax-resources.stamp")
add_custom_command(
    OUTPUT "${FASTMD_KSYNTAX_RESOURCES_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:FastMD>/ksyntaxhighlighting"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:FastMD>/ksyntaxhighlighting"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting/syntax"
            "$<TARGET_FILE_DIR:FastMD>/ksyntaxhighlighting/syntax"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/ksyntaxhighlighting/themes"
            "$<TARGET_FILE_DIR:FastMD>/ksyntaxhighlighting/themes"
    DEPENDS ${FASTMD_KSYNTAX_RESOURCES}
    COMMAND ${CMAKE_COMMAND} -E touch "${FASTMD_KSYNTAX_RESOURCES_STAMP}"
    COMMENT "Bundling KSyntaxHighlighting syntax/theme data")
add_custom_target(FastMDKsyntaxResources
    DEPENDS "${FASTMD_KSYNTAX_RESOURCES_STAMP}")
add_dependencies(FastMD FastMDKsyntaxResources)

# Deploy Qt DLLs next to the executable on Windows. The Arm64 Qt deployment
# tool cannot run on this x64 build host; its explicit Arm64 deployment lives
# in scripts/build-and-create-portable-app.ps1 instead.
if(WIN32 AND NOT FASTMD_TARGET_ARCH STREQUAL "arm64")
    add_custom_command(TARGET FastMD POST_BUILD
        # --no-opengl-sw drops opengl32sw.dll (20.6 MB) and
        # --no-system-dxc-compiler drops dxcompiler.dll + dxil.dll (15.8 MB).
        # FastMD is Qt Widgets only: it never creates a QOpenGLWidget and never
        # initializes Qt's D3D12 RHI backend, and the preview's rendering happens
        # inside the WebView2 runtime's own processes, which bring their own
        # graphics stack. Nothing in FastMD loads these three libraries.
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
                --no-translations --no-system-d3d-compiler --no-quick-import
                --no-opengl-sw --no-system-dxc-compiler
                "$<TARGET_FILE:FastMD>"
        COMMENT "Deploying Qt DLLs"
    )
endif()

# Exercise the real lazy panel/controller lifecycle without starting MainWindow
# or WebView2. Reuse the app's source/dependency authorities for this integration
# test so it cannot silently substitute a model of the production controller.
if(FASTMD_BUILD_TESTS)
    set(_workspace_test_sources ${SOURCES})
    list(FILTER _workspace_test_sources EXCLUDE REGEX "(^|/)main\\.cpp$|\\.rc$")
    qt_add_executable(WorkspaceOutlinePersistenceTest
        tests/WorkspaceOutlinePersistenceTest.cpp
        ${_workspace_test_sources})
    target_include_directories(WorkspaceOutlinePersistenceTest PRIVATE
        $<TARGET_PROPERTY:FastMD,INCLUDE_DIRECTORIES>)
    target_compile_definitions(WorkspaceOutlinePersistenceTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_DEFINITIONS>
        FASTMD_QT_PLATFORMS_DIR="$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
    target_compile_options(WorkspaceOutlinePersistenceTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_OPTIONS>)
    target_link_libraries(WorkspaceOutlinePersistenceTest PRIVATE
        $<TARGET_PROPERTY:FastMD,LINK_LIBRARIES> Qt6::Test)

    qt_add_executable(MarkdownEditorHighlightingTest
        tests/MarkdownEditorHighlightingTest.cpp
        ${_workspace_test_sources})
    target_include_directories(MarkdownEditorHighlightingTest PRIVATE
        $<TARGET_PROPERTY:FastMD,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MarkdownEditorHighlightingTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_DEFINITIONS>)
    target_compile_options(MarkdownEditorHighlightingTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_OPTIONS>)
    target_link_libraries(MarkdownEditorHighlightingTest PRIVATE
        $<TARGET_PROPERTY:FastMD,LINK_LIBRARIES>)

    qt_add_executable(MarkdownEditorColumnSelectionTest
        tests/MarkdownEditorColumnSelectionTest.cpp
        ${_workspace_test_sources})
    target_include_directories(MarkdownEditorColumnSelectionTest PRIVATE
        $<TARGET_PROPERTY:FastMD,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MarkdownEditorColumnSelectionTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_DEFINITIONS>)
    target_compile_options(MarkdownEditorColumnSelectionTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_OPTIONS>)
    target_link_libraries(MarkdownEditorColumnSelectionTest PRIVATE
        $<TARGET_PROPERTY:FastMD,LINK_LIBRARIES> Qt6::Test)

    qt_add_executable(MarkdownEditorPageBreakTest
        tests/MarkdownEditorPageBreakTest.cpp
        ${_workspace_test_sources})
    target_include_directories(MarkdownEditorPageBreakTest PRIVATE
        $<TARGET_PROPERTY:FastMD,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MarkdownEditorPageBreakTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_DEFINITIONS>)
    target_compile_options(MarkdownEditorPageBreakTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_OPTIONS>)
    target_link_libraries(MarkdownEditorPageBreakTest PRIVATE
        $<TARGET_PROPERTY:FastMD,LINK_LIBRARIES> Qt6::Test)

    qt_add_executable(PreferencesNavigationTest
        tests/PreferencesNavigationTest.cpp
        ${_workspace_test_sources})
    target_include_directories(PreferencesNavigationTest PRIVATE
        $<TARGET_PROPERTY:FastMD,INCLUDE_DIRECTORIES>)
    target_compile_definitions(PreferencesNavigationTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_DEFINITIONS>
        FASTMD_QT_PLATFORMS_DIR="$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
    target_compile_options(PreferencesNavigationTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_OPTIONS>)
    target_link_libraries(PreferencesNavigationTest PRIVATE
        $<TARGET_PROPERTY:FastMD,LINK_LIBRARIES>)

    qt_add_executable(MarkdownEditorHeadingTest
        tests/MarkdownEditorHeadingTest.cpp
        ${_workspace_test_sources})
    target_include_directories(MarkdownEditorHeadingTest PRIVATE
        $<TARGET_PROPERTY:FastMD,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MarkdownEditorHeadingTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_DEFINITIONS>)
    target_compile_options(MarkdownEditorHeadingTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_OPTIONS>)
    target_link_libraries(MarkdownEditorHeadingTest PRIVATE
        $<TARGET_PROPERTY:FastMD,LINK_LIBRARIES> Qt6::Test)

    qt_add_executable(PdfFindOverlayTest
        tests/PdfFindOverlayTest.cpp
        ${_workspace_test_sources})
    target_include_directories(PdfFindOverlayTest PRIVATE
        $<TARGET_PROPERTY:FastMD,INCLUDE_DIRECTORIES>)
    target_compile_definitions(PdfFindOverlayTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_DEFINITIONS>)
    target_compile_options(PdfFindOverlayTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_OPTIONS>)
    target_link_libraries(PdfFindOverlayTest PRIVATE
        $<TARGET_PROPERTY:FastMD,LINK_LIBRARIES> Qt6::Test)

    qt_add_executable(SourceControlPanelTeardownTest
        tests/SourceControlPanelTeardownTest.cpp
        ${_workspace_test_sources})
    target_include_directories(SourceControlPanelTeardownTest PRIVATE
        $<TARGET_PROPERTY:FastMD,INCLUDE_DIRECTORIES>)
    target_compile_definitions(SourceControlPanelTeardownTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_DEFINITIONS>
        FASTMD_QT_PLATFORMS_DIR="$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
    target_compile_options(SourceControlPanelTeardownTest PRIVATE
        $<TARGET_PROPERTY:FastMD,COMPILE_OPTIONS>)
    target_link_libraries(SourceControlPanelTeardownTest PRIVATE
        $<TARGET_PROPERTY:FastMD,LINK_LIBRARIES> Qt6::Test)
endif()

