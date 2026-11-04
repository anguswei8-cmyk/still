cmake_minimum_required(VERSION 3.24)
project(PixClone VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INSTALL_BINDIR ".")
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network Concurrent QuickWidgets QuickControls2)

set(PIXCLONE_QML_FILES
    src/qml/Theme.qml src/qml/Main.qml src/qml/GlassSurface.qml src/qml/UiButton.qml src/qml/UiField.qml
    src/qml/UiSwitch.qml src/qml/ClipboardPage.qml src/qml/SettingRow.qml
    src/qml/SettingsPage.qml src/qml/HistoryPanel.qml src/qml/UiComboBox.qml src/qml/UiSpinBox.qml
    src/qml/AppearancePicker.qml src/qml/LanguagePicker.qml)
list(TRANSFORM PIXCLONE_QML_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
function(pixclone_qml target)
    foreach(qml_file IN LISTS PIXCLONE_QML_FILES)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
        if(qml_name STREQUAL "Theme.qml")
            set_source_files_properties("${qml_file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
        endif()
    endforeach()
    target_link_libraries(${target} PRIVATE Qt6::QuickWidgets Qt6::QuickControls2)
    qt_add_qml_module(${target} URI PixClone VERSION 1.0 RESOURCE_PREFIX /qt/qml
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/${target}/PixClone"
        QML_FILES ${PIXCLONE_QML_FILES})
endfunction()

add_library(pixclone_core STATIC
    src/imaging/image.cpp
    src/storage/store.cpp src/storage/history.cpp src/storage/history.h
    src/capture/scroll_session.cpp src/capture/scroll_session.h
    src/ui/toolicons.cpp src/ui/i18n.cpp src/ui/i18n.h)
qt_add_resources(pixclone_core brand PREFIX "/brand" BASE assets FILES assets/en.json assets/still.png)
target_sources(pixclone_core PRIVATE
    src/clipboard/clipboard_data.cpp src/clipboard/clipboard_data.h
    src/clipboard/clipboard_archive.cpp src/clipboard/clipboard_archive.h)
target_include_directories(pixclone_core PUBLIC src)
target_link_libraries(pixclone_core PUBLIC Qt6::Gui Qt6::Concurrent)

set(PLATFORM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_stub.cpp")
if(WIN32)
    set(PLATFORM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_win.cpp")
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(PLATFORM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_mac.mm")
endif()

# QML import scanning starts at the target's source directory, excluding .tools and build.
add_subdirectory(src)
if(MSVC)
    target_compile_options(pixclone_core PUBLIC /utf-8 /W4)
else()
    target_compile_options(pixclone_core PUBLIC -Wall -Wextra)
endif()
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(pixclone_tests tests/core_test.cpp)
    target_link_libraries(pixclone_tests PRIVATE pixclone_core Qt6::Test)
    add_test(NAME pixclone_core COMMAND pixclone_tests -o "${CMAKE_CURRENT_BINARY_DIR}/core-results.txt,txt")
    set_tests_properties(pixclone_core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(pixclone_clipboard_tests tests/clipboard_test.cpp)
    target_link_libraries(pixclone_clipboard_tests PRIVATE pixclone_core Qt6::Test)
    add_test(NAME pixclone_clipboard COMMAND pixclone_clipboard_tests -o "${CMAKE_CURRENT_BINARY_DIR}/clipboard-results.txt,txt")
    set_tests_properties(pixclone_clipboard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(pixclone_clipboard_ui_tests tests/clipboard_ui_test.cpp tests/platform_fixture.cpp
        src/clipboard/clipboard_controller.cpp src/clipboard/clipboard_controller.h src/platform/hotkeys.cpp src/platform/platform.h)
    target_link_libraries(pixclone_clipboard_ui_tests PRIVATE pixclone_core Qt6::Widgets Qt6::Test)
    add_test(NAME pixclone_clipboard_ui COMMAND pixclone_clipboard_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/clipboard-ui-results.txt,txt")
    set_tests_properties(pixclone_clipboard_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(pixclone_history_tests tests/history_test.cpp)
    target_link_libraries(pixclone_history_tests PRIVATE pixclone_core Qt6::Test)
    add_test(NAME pixclone_history COMMAND pixclone_history_tests -o "${CMAKE_CURRENT_BINARY_DIR}/history-results.txt,txt")
    qt_add_executable(pixclone_scroll_tests tests/scroll_session_test.cpp)
    target_link_libraries(pixclone_scroll_tests PRIVATE pixclone_core Qt6::Test)
    add_test(NAME pixclone_scroll COMMAND pixclone_scroll_tests -o "${CMAKE_CURRENT_BINARY_DIR}/scroll-results.txt,txt")
    qt_add_executable(pixclone_settings_apply_tests tests/settings_apply_test.cpp tests/platform_fixture.cpp
        src/settings/settings_apply.cpp src/settings/settings_apply.h src/platform/hotkeys.cpp src/platform/platform.h)
    target_link_libraries(pixclone_settings_apply_tests PRIVATE pixclone_core Qt6::Test)
    add_test(NAME pixclone_settings_apply COMMAND pixclone_settings_apply_tests -o "${CMAKE_CURRENT_BINARY_DIR}/settings-apply-results.txt,txt")
    qt_add_executable(pixclone_ui_tests tests/ui_test.cpp tests/ocr_fixture.cpp
        src/imaging/editor.cpp src/imaging/editor.h src/imaging/pin.cpp src/imaging/pin.h
        src/capture/overlay.cpp src/capture/overlay.h src/imaging/ocr.cpp src/imaging/ocr.h)
    target_link_libraries(pixclone_ui_tests PRIVATE pixclone_core Qt6::Widgets Qt6::Concurrent Qt6::Test)
    add_test(NAME pixclone_ui COMMAND pixclone_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/ui-results.txt,txt")
    set_tests_properties(pixclone_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(pixclone_settings_tests tests/settings_test.cpp tests/platform_fixture.cpp
        src/settings/settings_apply.cpp src/settings/settings_apply.h src/platform/hotkeys.cpp
        src/ui/main_window.cpp src/ui/main_window.h src/clipboard/clipboard_controller.cpp src/clipboard/clipboard_controller.h
        src/app/application.cpp src/app/application.h src/platform/platform.h
        src/capture/scroll_capture_ui.cpp src/capture/scroll_capture_ui.h src/capture/overlay.cpp src/capture/overlay.h
        src/imaging/editor.cpp src/imaging/editor.h src/imaging/pin.cpp src/imaging/pin.h src/imaging/ocr.cpp src/imaging/ocr.h)
    target_link_libraries(pixclone_settings_tests PRIVATE pixclone_core Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
    if(WIN32)
        target_link_libraries(pixclone_settings_tests PRIVATE dwmapi)
    endif()
    pixclone_qml(pixclone_settings_tests)
    add_test(NAME pixclone_settings COMMAND pixclone_settings_tests -o "${CMAKE_CURRENT_BINARY_DIR}/settings-results.txt,txt")
    set_tests_properties(pixclone_settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    if(WIN32 OR APPLE)
        qt_add_executable(pixclone_native_tests tests/native_test.cpp src/platform/hotkeys.cpp src/platform/platform.h ${PLATFORM_SOURCE}
            src/clipboard/clipboard_controller.cpp src/clipboard/clipboard_controller.h src/capture/scroll_capture_ui.cpp src/capture/scroll_capture_ui.h)
        target_link_libraries(pixclone_native_tests PRIVATE pixclone_core Qt6::Widgets Qt6::Test)
        if(WIN32)
            target_link_libraries(pixclone_native_tests PRIVATE user32 dwmapi)
        else()
            target_compile_options(pixclone_native_tests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
            target_link_libraries(pixclone_native_tests PRIVATE "-framework Cocoa" "-framework Vision" "-framework Carbon" "-framework CoreGraphics" "-framework ApplicationServices")
        endif()
        # Native tests use actual OS fonts and OCR, and are opt-in on developer machines.
        add_test(NAME pixclone_native COMMAND pixclone_native_tests -o "${CMAKE_CURRENT_BINARY_DIR}/native-results.txt,txt")
        set_tests_properties(pixclone_native PROPERTIES LABELS integration)
    endif()
endif()

install(TARGETS PixClone BUNDLE DESTINATION . RUNTIME DESTINATION .)
if(WIN32)
    install(FILES native/Invoke-Ocr.ps1 DESTINATION native)
endif()
set(DEPLOY_OPTIONS)
if(MINGW)
    get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    # windeployqt runs inside the destination and can load these DLLs itself.
    # Copy them before starting it, so a repeat deployment never overwrites a loaded DLL.
    install(FILES "${compiler_bin}/libgcc_s_seh-1.dll" "${compiler_bin}/libstdc++-6.dll"
                  "${compiler_bin}/libwinpthread-1.dll" DESTINATION .)
    list(APPEND DEPLOY_OPTIONS NO_COMPILER_RUNTIME)
endif()
qt_generate_deploy_qml_app_script(TARGET PixClone OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR ${DEPLOY_OPTIONS})
install(SCRIPT ${deploy_script})
