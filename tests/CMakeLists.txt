function(microscope_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE microscope_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

microscope_test(test_harness)
microscope_test(test_view_transform)
microscope_test(test_disk_policy)
microscope_test(test_capture_naming)
microscope_test(test_format_preference)
microscope_test(test_fake_capture_source)
microscope_test(test_snapshot_writer)
microscope_test(test_qt_recorder)
microscope_test(test_capture_controller)
microscope_test(test_qt_capture_source)
microscope_test(test_format_fallback)
microscope_test(test_failure_modes)
microscope_test(test_camera_access_policy)
microscope_test(test_capture_session)
microscope_test(test_device_selection)
microscope_test(test_frame_relay)

qt_add_executable(test_view_transform_model
    test_view_transform_model.cpp
    ${CMAKE_SOURCE_DIR}/src/view/ViewTransformModel.cpp)
target_link_libraries(test_view_transform_model
    PRIVATE microscope_core Qt6::Test Qt6::Qml)
add_test(NAME test_view_transform_model COMMAND test_view_transform_model)

# Headless QML smoke test: instantiates Main.qml exactly as main.cpp does and
# fails on any QML warning. This is its own qt_add_qml_module registration of
# the same "microscope" URI over the same source files as the microscope
# executable's -- qt_add_qml_module on an executable target compiles its QML
# module directly into that one binary (see src/CMakeLists.txt), so there is
# no separate library to link against; this test independently registers the
# same sources into its own binary instead, the same way test_view_transform_model
# above already compiles ViewTransformModel.cpp directly rather than sharing
# microscope's build of it.
qt_add_executable(test_qml_smoke
    test_qml_smoke.cpp
    ${CMAKE_SOURCE_DIR}/src/AppContext.cpp)
target_link_libraries(test_qml_smoke PRIVATE microscope_core Qt6::Test Qt6::Quick)
# qt_add_qml_module's internal qmlcachegen path derivation cannot cope with
# QML_FILES living outside this directory via ".." (it tried to mkdir a
# literal "test_qml_smoke_../src" and failed) -- copy the same files (kept
# in sync: configure_file(COPYONLY) re-triggers CMake's own reconfigure
# whenever the source changes) into this target's own binary directory
# first, so its qml module sees a normal, self-contained ui/ tree.
set(_qml_smoke_src ${CMAKE_CURRENT_BINARY_DIR}/qml_smoke_src)
foreach(qml_file Main VideoView ControlBar StatusBanner)
    configure_file(${CMAKE_SOURCE_DIR}/src/ui/${qml_file}.qml
        ${_qml_smoke_src}/ui/${qml_file}.qml COPYONLY)
    set_source_files_properties(${_qml_smoke_src}/ui/${qml_file}.qml
        PROPERTIES QT_RESOURCE_ALIAS ui/${qml_file}.qml)
endforeach()
qt_add_qml_module(test_qml_smoke
    URI microscope
    VERSION 1.0
    NO_CACHEGEN
    QML_FILES
        ${_qml_smoke_src}/ui/Main.qml
        ${_qml_smoke_src}/ui/VideoView.qml
        ${_qml_smoke_src}/ui/ControlBar.qml
        ${_qml_smoke_src}/ui/StatusBanner.qml
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/AppContext.h ${CMAKE_SOURCE_DIR}/src/AppContext.cpp
        ${CMAKE_SOURCE_DIR}/src/view/ViewTransformModel.h ${CMAKE_SOURCE_DIR}/src/view/ViewTransformModel.cpp
)
# Same fix as src/CMakeLists.txt's Defect 1: the generated type-registration
# file's __has_include(<ViewTransformModel.h>) guard needs that header's own
# directory on this target's include path too.
target_include_directories(test_qml_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src/view)
add_test(NAME test_qml_smoke COMMAND test_qml_smoke)
set_tests_properties(test_qml_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
