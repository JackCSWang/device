add_library(microscope_core STATIC)
target_include_directories(microscope_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(microscope_core PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia)

qt_add_executable(microscope main.cpp)
target_link_libraries(microscope PRIVATE microscope_core Qt6::Quick)
