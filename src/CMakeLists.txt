qt_add_executable(Still WIN32 MACOSX_BUNDLE
    app/main.cpp app/application.cpp app/application.h
    settings/settings_apply.cpp settings/settings_apply.h
    ui/main_window.cpp ui/main_window.h clipboard/clipboard_controller.cpp clipboard/clipboard_controller.h
    imaging/editor.cpp imaging/editor.h imaging/pin.cpp imaging/pin.h imaging/ocr.cpp imaging/ocr.h
    capture/overlay.cpp capture/overlay.h capture/scroll_capture_ui.cpp capture/scroll_capture_ui.h
    platform/hotkeys.cpp platform/platform.h ${PLATFORM_SOURCE})
target_link_libraries(Still PRIVATE still_core Qt6::Widgets Qt6::Network Qt6::Concurrent)
still_qml(Still)
set_target_properties(Still PROPERTIES
    OUTPUT_NAME Still
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    MACOSX_BUNDLE_GUI_IDENTIFIER local.still.app
    MACOSX_BUNDLE_BUNDLE_NAME Still
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/native/Info.plist.in")
if(WIN32)
    target_sources(Still PRIVATE "${PROJECT_SOURCE_DIR}/assets/still.rc")
    target_include_directories(Still PRIVATE "${PROJECT_SOURCE_DIR}/assets")
    target_link_libraries(Still PRIVATE user32 dwmapi)
    add_custom_target(copy_native ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Still>/native"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/native/Invoke-Ocr.ps1" "$<TARGET_FILE_DIR:Still>/native/")
    add_dependencies(Still copy_native)
elseif(APPLE)
    target_sources(Still PRIVATE "${PROJECT_SOURCE_DIR}/assets/still.icns")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/assets/still.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Still PROPERTIES MACOSX_BUNDLE_ICON_FILE still.icns)
    target_compile_options(Still PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_link_libraries(Still PRIVATE "-framework Cocoa" "-framework Vision" "-framework Carbon" "-framework CoreGraphics" "-framework ApplicationServices")
endif()
