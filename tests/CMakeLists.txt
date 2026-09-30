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

qt_add_executable(test_view_transform_model
    test_view_transform_model.cpp
    ${CMAKE_SOURCE_DIR}/src/view/ViewTransformModel.cpp)
target_link_libraries(test_view_transform_model
    PRIVATE microscope_core Qt6::Test Qt6::Qml)
add_test(NAME test_view_transform_model COMMAND test_view_transform_model)
