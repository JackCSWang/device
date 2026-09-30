function(microscope_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE microscope_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

microscope_test(test_harness)
microscope_test(test_view_transform)
