cmake_minimum_required(VERSION 3.24)
project(Still VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INSTALL_BINDIR ".")
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network Concurrent QuickWidgets QuickControls2)

set(STILL_QML_FILES
    src/qml/Theme.qml src/qml/Main.qml src/qml/GlassSurface.qml src/qml/UiButton.qml src/qml/UiField.qml
    src/qml/UiSwitch.qml src/qml/ClipboardPage.qml src/qml/SettingRow.qml
    src/qml/SettingsPage.qml src/qml/StartupRow.qml src/qml/HistoryPanel.qml src/qml/UiComboBox.qml src/qml/UiSpinBox.qml
    src/qml/AppearancePicker.qml src/qml/LanguagePicker.qml)
list(TRANSFORM STILL_QML_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
function(still_qml target)
    foreach(qml_file IN LISTS STILL_QML_FILES)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
        if(qml_name STREQUAL "Theme.qml")
            set_source_files_properties("${qml_file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
        endif()
    endforeach()
    target_link_libraries(${target} PRIVATE Qt6::QuickWidgets Qt6::QuickControls2)
    qt_add_qml_module(${target} URI Still VERSION 1.0 RESOURCE_PREFIX /qt/qml
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/${target}/Still"
        QML_FILES ${STILL_QML_FILES})
endfunction()

add_library(still_core STATIC
    src/app/instance_request.cpp src/app/instance_request.h
    src/imaging/image.cpp src/imaging/html_document.cpp src/imaging/html_document.h
    src/storage/store.cpp src/storage/history.cpp src/storage/history.h src/storage/image_export.cpp src/storage/pin_session.cpp
    src/storage/region_presets.cpp src/storage/region_presets.h
    src/capture/scroll_session.cpp src/capture/scroll_session.h
    src/ui/toolicons.cpp src/ui/i18n.cpp src/ui/i18n.h)
qt_add_resources(still_core brand PREFIX "/brand" BASE assets FILES assets/en.json assets/still.png assets/still-tray.png)
target_sources(still_core PRIVATE
    src/clipboard/clipboard_data.cpp src/clipboard/clipboard_data.h
    src/clipboard/image_copy.cpp src/clipboard/image_copy.h
    src/clipboard/clipboard_archive.cpp src/clipboard/clipboard_archive.h
    src/clipboard/clipboard_browse.cpp src/clipboard/clipboard_browse.h)
target_include_directories(still_core PUBLIC src)
target_link_libraries(still_core PUBLIC Qt6::Gui Qt6::Concurrent)

set(PLATFORM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_stub.cpp")
set(STARTUP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/startup_stub.cpp")
set(API_CREDENTIAL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/api_credentials_stub.cpp")
if(WIN32)
    set(PLATFORM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_win.cpp")
    set(STARTUP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/startup_win.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/startup_task.cpp")
    set(API_CREDENTIAL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/api_credentials_win.cpp")
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(PLATFORM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_mac.mm")
    set(STARTUP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/startup_mac.mm")
    set(API_CREDENTIAL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/api_credentials_mac.mm")
endif()

set(STILL_API_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/imaging/api_proofreader.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/imaging/api_proofreader.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/imaging/api_proofread_dialog.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/imaging/api_proofread_dialog.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/settings/api_profile.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/settings/api_profile.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/settings/api_settings_dialog.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/settings/api_settings_dialog.h")

# QML import scanning starts at the target's source directory, excluding .tools and build.
add_subdirectory(src)
if(MSVC)
    target_compile_options(still_core PUBLIC /utf-8 /W4)
else()
    target_compile_options(still_core PUBLIC -Wall -Wextra)
endif()
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(still_instance_request_tests tests/instance_request_test.cpp)
    target_link_libraries(still_instance_request_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_instance_request COMMAND still_instance_request_tests -o "${CMAKE_CURRENT_BINARY_DIR}/instance-request-results.txt,txt")
    qt_add_executable(still_api_client_tests tests/api_proofreader_test.cpp src/imaging/api_proofreader.cpp src/imaging/api_proofreader.h)
    target_link_libraries(still_api_client_tests PRIVATE still_core Qt6::Network Qt6::Test)
    add_test(NAME still_api_client COMMAND still_api_client_tests -o "${CMAKE_CURRENT_BINARY_DIR}/api-client-results.txt,txt")
    qt_add_executable(still_api_profile_tests tests/api_profile_test.cpp src/settings/api_profile.cpp src/imaging/api_proofreader.cpp src/imaging/api_proofreader.h)
    target_link_libraries(still_api_profile_tests PRIVATE still_core Qt6::Network Qt6::Test)
    add_test(NAME still_api_profile COMMAND still_api_profile_tests -o "${CMAKE_CURRENT_BINARY_DIR}/api-profile-results.txt,txt")
    qt_add_executable(still_api_settings_tests tests/api_settings_dialog_test.cpp tests/api_credentials_fixture.cpp ${STILL_API_SOURCES})
    target_link_libraries(still_api_settings_tests PRIVATE still_core Qt6::Widgets Qt6::Network Qt6::Test)
    add_test(NAME still_api_settings COMMAND still_api_settings_tests -o "${CMAKE_CURRENT_BINARY_DIR}/api-settings-results.txt,txt")
    set_tests_properties(still_api_settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_api_credentials_tests tests/api_credentials_test.cpp ${API_CREDENTIAL_SOURCE})
    target_link_libraries(still_api_credentials_tests PRIVATE still_core Qt6::Test)
    if(APPLE)
        target_compile_options(still_api_credentials_tests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
        target_link_libraries(still_api_credentials_tests PRIVATE "-framework Security")
    elseif(WIN32)
        target_link_libraries(still_api_credentials_tests PRIVATE advapi32)
    endif()
    add_test(NAME still_api_credentials COMMAND still_api_credentials_tests -o "${CMAKE_CURRENT_BINARY_DIR}/api-credentials-results.txt,txt")
    set_tests_properties(still_api_credentials PROPERTIES LABELS integration RUN_SERIAL TRUE)
    qt_add_executable(still_startup_task_tests tests/startup_task_test.cpp src/platform/startup_task.cpp src/platform/startup_task.h)
    target_link_libraries(still_startup_task_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_startup_task COMMAND still_startup_task_tests -o "${CMAKE_CURRENT_BINARY_DIR}/startup-task-results.txt,txt")
    qt_add_executable(still_tests tests/core_test.cpp)
    target_link_libraries(still_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_core COMMAND still_tests -o "${CMAKE_CURRENT_BINARY_DIR}/core-results.txt,txt")
    set_tests_properties(still_core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_export_tests tests/image_export_test.cpp)
    target_link_libraries(still_export_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_export COMMAND still_export_tests -o "${CMAKE_CURRENT_BINARY_DIR}/export-results.txt,txt")
    set_tests_properties(still_export PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_pin_session_tests tests/pin_session_test.cpp)
    target_link_libraries(still_pin_session_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_pin_session COMMAND still_pin_session_tests -o "${CMAKE_CURRENT_BINARY_DIR}/pin-session-results.txt,txt")
    set_tests_properties(still_pin_session PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_region_presets_tests tests/region_presets_test.cpp)
    target_link_libraries(still_region_presets_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_region_presets COMMAND still_region_presets_tests -o "${CMAKE_CURRENT_BINARY_DIR}/region-presets-results.txt,txt")
    set_tests_properties(still_region_presets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_capture_presets_tests tests/capture_presets_test.cpp src/capture/capture_presets.cpp src/capture/capture_presets.h)
    target_link_libraries(still_capture_presets_tests PRIVATE still_core Qt6::Widgets Qt6::Test)
    add_test(NAME still_capture_presets COMMAND still_capture_presets_tests -o "${CMAKE_CURRENT_BINARY_DIR}/capture-presets-results.txt,txt")
    set_tests_properties(still_capture_presets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_ocr_result_tests tests/ocr_result_dialog_test.cpp src/imaging/ocr_result_dialog.cpp src/imaging/ocr_result_dialog.h)
    target_sources(still_ocr_result_tests PRIVATE ${STILL_API_SOURCES} tests/api_credentials_fixture.cpp)
    target_link_libraries(still_ocr_result_tests PRIVATE still_core Qt6::Widgets Qt6::Network Qt6::Test)
    add_test(NAME still_ocr_result COMMAND still_ocr_result_tests -o "${CMAKE_CURRENT_BINARY_DIR}/ocr-result-results.txt,txt")
    set_tests_properties(still_ocr_result PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_clipboard_tests tests/clipboard_test.cpp)
    target_link_libraries(still_clipboard_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_clipboard COMMAND still_clipboard_tests -o "${CMAKE_CURRENT_BINARY_DIR}/clipboard-results.txt,txt")
    set_tests_properties(still_clipboard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_clipboard_browse_tests tests/clipboard_browse_test.cpp)
    target_link_libraries(still_clipboard_browse_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_clipboard_browse COMMAND still_clipboard_browse_tests -o "${CMAKE_CURRENT_BINARY_DIR}/clipboard-browse-results.txt,txt")
    set_tests_properties(still_clipboard_browse PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_clipboard_ui_tests tests/clipboard_ui_test.cpp tests/platform_fixture.cpp
        src/clipboard/clipboard_controller.cpp src/clipboard/clipboard_controller.h src/platform/hotkeys.cpp src/platform/platform.h)
    target_link_libraries(still_clipboard_ui_tests PRIVATE still_core Qt6::Widgets Qt6::Test)
    add_test(NAME still_clipboard_ui COMMAND still_clipboard_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/clipboard-ui-results.txt,txt")
    set_tests_properties(still_clipboard_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_history_tests tests/history_test.cpp)
    target_link_libraries(still_history_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_history COMMAND still_history_tests -o "${CMAKE_CURRENT_BINARY_DIR}/history-results.txt,txt")
    qt_add_executable(still_scroll_tests tests/scroll_session_test.cpp)
    target_link_libraries(still_scroll_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_scroll COMMAND still_scroll_tests -o "${CMAKE_CURRENT_BINARY_DIR}/scroll-results.txt,txt")
    qt_add_executable(still_settings_apply_tests tests/settings_apply_test.cpp tests/platform_fixture.cpp
        src/settings/settings_apply.cpp src/settings/settings_apply.h src/platform/hotkeys.cpp src/platform/platform.h)
    target_link_libraries(still_settings_apply_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_settings_apply COMMAND still_settings_apply_tests -o "${CMAKE_CURRENT_BINARY_DIR}/settings-apply-results.txt,txt")
    qt_add_executable(still_settings_draft_tests tests/settings_draft_test.cpp tests/platform_fixture.cpp
        src/settings/settings_draft.cpp src/settings/settings_draft.h
        src/settings/settings_apply.cpp src/settings/settings_apply.h src/platform/hotkeys.cpp src/platform/platform.h)
    target_link_libraries(still_settings_draft_tests PRIVATE still_core Qt6::Test)
    add_test(NAME still_settings_draft COMMAND still_settings_draft_tests -o "${CMAKE_CURRENT_BINARY_DIR}/settings-draft-results.txt,txt")
    set_tests_properties(still_settings_draft PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_backup_tests tests/settings_backup_test.cpp tests/platform_fixture.cpp
        src/settings/settings_backup.cpp src/settings/settings_backup.h
        src/settings/settings_draft.cpp src/settings/settings_draft.h
        src/settings/settings_apply.cpp src/settings/settings_apply.h src/platform/hotkeys.cpp src/platform/platform.h)
    target_link_libraries(still_backup_tests PRIVATE still_core Qt6::Widgets Qt6::Test)
    add_test(NAME still_backup COMMAND still_backup_tests -o "${CMAKE_CURRENT_BINARY_DIR}/backup-results.txt,txt")
    set_tests_properties(still_backup PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_backup_dialog_tests tests/settings_backup_dialog_test.cpp tests/platform_fixture.cpp
        src/settings/settings_backup_dialog.cpp src/settings/settings_backup_dialog.h
        src/settings/settings_backup.cpp src/settings/settings_backup.h
        src/settings/settings_draft.cpp src/settings/settings_draft.h
        src/settings/settings_apply.cpp src/settings/settings_apply.h src/platform/hotkeys.cpp src/platform/platform.h)
    target_link_libraries(still_backup_dialog_tests PRIVATE still_core Qt6::Widgets Qt6::Test)
    add_test(NAME still_backup_dialog COMMAND still_backup_dialog_tests -o "${CMAKE_CURRENT_BINARY_DIR}/backup-dialog-results.txt,txt")
    set_tests_properties(still_backup_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_ui_tests tests/ui_test.cpp tests/ocr_fixture.cpp
        src/imaging/ocr_result_dialog.cpp src/imaging/ocr_result_dialog.h
        src/imaging/pin_window.cpp src/imaging/pin_window.h src/imaging/text_pin.cpp src/imaging/text_pin.h
        src/imaging/editor.cpp src/imaging/editor.h src/imaging/pin.cpp src/imaging/pin.h
        src/capture/overlay.cpp src/capture/overlay.h src/imaging/ocr.cpp src/imaging/ocr.h)
    target_sources(still_ui_tests PRIVATE ${STILL_API_SOURCES} tests/api_credentials_fixture.cpp)
    target_link_libraries(still_ui_tests PRIVATE still_core Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
    add_test(NAME still_ui COMMAND still_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/ui-results.txt,txt")
    set_tests_properties(still_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_delay_tests tests/capture_delay_test.cpp src/capture/capture_delay.cpp src/capture/capture_delay.h)
    target_link_libraries(still_delay_tests PRIVATE still_core Qt6::Widgets Qt6::Test)
    add_test(NAME still_delay COMMAND still_delay_tests -o "${CMAKE_CURRENT_BINARY_DIR}/delay-results.txt,txt")
    set_tests_properties(still_delay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(still_settings_tests tests/settings_test.cpp tests/platform_fixture.cpp
        src/settings/settings_backup.cpp src/settings/settings_backup.h src/settings/settings_backup_dialog.cpp src/settings/settings_backup_dialog.h
        src/imaging/ocr_result_dialog.cpp src/imaging/ocr_result_dialog.h
        src/settings/settings_apply.cpp src/settings/settings_apply.h src/platform/hotkeys.cpp
        src/ui/main_window.cpp src/ui/main_window.h src/clipboard/clipboard_controller.cpp src/clipboard/clipboard_controller.h
        src/settings/settings_draft.cpp src/settings/settings_draft.h
        src/app/application.cpp src/app/application.h src/platform/platform.h
        src/capture/scroll_capture_ui.cpp src/capture/scroll_capture_ui.h src/capture/overlay.cpp src/capture/overlay.h
        src/capture/capture_delay.cpp src/capture/capture_delay.h
        src/capture/capture_presets.cpp src/capture/capture_presets.h
        src/imaging/pin_window.cpp src/imaging/pin_window.h src/imaging/text_pin.cpp src/imaging/text_pin.h
        src/imaging/editor.cpp src/imaging/editor.h src/imaging/pin.cpp src/imaging/pin.h src/imaging/ocr.cpp src/imaging/ocr.h)
    target_link_libraries(still_settings_tests PRIVATE still_core Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
    target_sources(still_settings_tests PRIVATE ${STILL_API_SOURCES} tests/api_credentials_fixture.cpp)
    if(APPLE)
        set_source_files_properties(tests/startup_move_native_test.cpp PROPERTIES LANGUAGE OBJCXX)
        qt_add_executable(still_startup_move_tests tests/startup_move_native_test.cpp src/platform/startup_mac.mm src/platform/startup.h)
        target_link_libraries(still_startup_move_tests PRIVATE still_core Qt6::Test "-framework Foundation" "-framework ServiceManagement")
        target_compile_options(still_startup_move_tests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
        add_test(NAME still_mac_startup_move COMMAND still_startup_move_tests -o "${CMAKE_CURRENT_BINARY_DIR}/startup-move-results.txt,txt")
        set_tests_properties(still_mac_startup_move PROPERTIES LABELS integration RUN_SERIAL TRUE)
        set_source_files_properties(tests/startup_native_test.cpp PROPERTIES LANGUAGE OBJCXX)
        qt_add_executable(still_startup_native_tests MACOSX_BUNDLE tests/startup_native_test.cpp
            src/platform/startup_mac.mm src/platform/startup.h)
        target_link_libraries(still_startup_native_tests PRIVATE still_core Qt6::Test "-framework ServiceManagement")
        target_compile_options(still_startup_native_tests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
        set_target_properties(still_startup_native_tests PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER local.still.startup-regression
            MACOSX_BUNDLE_BUNDLE_NAME still_startup_native_tests
            MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/native/StartupTest-Info.plist.in")
        if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
            set_property(TARGET still_startup_native_tests APPEND PROPERTY LINK_DEPENDS "$<TARGET_BUNDLE_DIR:still_startup_native_tests>/Contents/Info.plist")
            add_custom_command(TARGET still_startup_native_tests POST_BUILD COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:still_startup_native_tests>" VERBATIM)
        endif()
        add_test(NAME still_mac_startup COMMAND still_startup_native_tests -o "${CMAKE_CURRENT_BINARY_DIR}/startup-native-results.txt,txt")
        set_tests_properties(still_mac_startup PROPERTIES LABELS integration RUN_SERIAL TRUE)
        set_source_files_properties(tests/settings_test.cpp src/ui/main_window.cpp PROPERTIES LANGUAGE OBJCXX)
        target_link_libraries(still_settings_tests PRIVATE "-framework Cocoa")
        add_test(NAME still_mac_window COMMAND still_settings_tests mainWindowUsesNormalMacWindow mainWindowDockFollowsVisibility -o "${CMAKE_CURRENT_BINARY_DIR}/mac-window-results.txt,txt")
        set_tests_properties(still_mac_window PROPERTIES LABELS integration)
        # Cocoa activation is process-global; isolate this from the second-Application lifetime test.
        add_test(NAME still_mac_delay COMMAND still_settings_tests delayedCaptureOnlyHidesWindowsWhenItStarts delayedCaptureStaysVisibleWhenTheApplicationIsInactive -o "${CMAKE_CURRENT_BINARY_DIR}/mac-delay-results.txt,txt")
        set_tests_properties(still_mac_delay PROPERTIES LABELS integration RUN_SERIAL TRUE ENVIRONMENT "QT_QUICK_BACKEND=software")
        add_test(NAME still_mac_pin_activation COMMAND still_settings_tests pinsStayVisibleWhenTheApplicationIsInactive -o "${CMAKE_CURRENT_BINARY_DIR}/mac-pin-activation-results.txt,txt")
        set_tests_properties(still_mac_pin_activation PROPERTIES LABELS integration RUN_SERIAL TRUE ENVIRONMENT "QT_QUICK_BACKEND=software")
    endif()
    if(WIN32)
        target_link_libraries(still_settings_tests PRIVATE dwmapi)
    endif()
    still_qml(still_settings_tests)
    add_test(NAME still_settings COMMAND still_settings_tests -o "${CMAKE_CURRENT_BINARY_DIR}/settings-results.txt,txt")
    set_tests_properties(still_settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    if(WIN32 OR APPLE)
        qt_add_executable(still_native_tests tests/native_test.cpp src/platform/hotkeys.cpp src/platform/platform.h ${PLATFORM_SOURCE}
            src/clipboard/clipboard_controller.cpp src/clipboard/clipboard_controller.h src/capture/scroll_capture_ui.cpp src/capture/scroll_capture_ui.h)
        target_link_libraries(still_native_tests PRIVATE still_core Qt6::Widgets Qt6::Test)
        if(WIN32)
            qt_add_executable(still_windows_startup_tests tests/startup_windows_test.cpp
                src/platform/startup_win.cpp src/platform/startup_win.h src/platform/startup_task.cpp src/platform/startup_task.h)
            target_link_libraries(still_windows_startup_tests PRIVATE still_core Qt6::Test taskschd ole32 oleaut32 advapi32 uuid)
            add_test(NAME still_windows_startup COMMAND still_windows_startup_tests -o "${CMAKE_CURRENT_BINARY_DIR}/windows-startup-results.txt,txt")
            set_tests_properties(still_windows_startup PROPERTIES LABELS integration RUN_SERIAL TRUE)
            target_link_libraries(still_native_tests PRIVATE user32 dwmapi)
        else()
            set_source_files_properties(tests/native_test.cpp PROPERTIES LANGUAGE OBJCXX)
            target_compile_options(still_native_tests PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
            target_link_libraries(still_native_tests PRIVATE "-framework Cocoa" "-framework Vision" "-framework Carbon" "-framework CoreGraphics" "-framework ApplicationServices")
        endif()
        # Native tests use actual OS fonts and OCR, and are opt-in on developer machines.
        add_test(NAME still_native COMMAND still_native_tests -o "${CMAKE_CURRENT_BINARY_DIR}/native-results.txt,txt")
        set_tests_properties(still_native PROPERTIES LABELS integration)
    endif()
endif()

install(TARGETS Still BUNDLE DESTINATION . RUNTIME DESTINATION .)
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
qt_generate_deploy_qml_app_script(TARGET Still OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR ${DEPLOY_OPTIONS})
install(SCRIPT ${deploy_script})
