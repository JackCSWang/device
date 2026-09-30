add_library(microscope_core STATIC)
target_include_directories(microscope_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(microscope_core PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia)
target_sources(microscope_core PRIVATE view/ViewTransform.cpp storage/DiskPolicy.cpp
    storage/CaptureNaming.cpp
    storage/OutputLocation.cpp capture/FormatPreference.cpp
    core/Frame.cpp core/FakeCaptureSource.cpp
    core/ICaptureSource.h
    output/SnapshotWriter.cpp output/IRecorder.h output/QtRecorder.cpp
    capture/CaptureController.cpp
    device/DeviceRegistry.cpp
    capture/QtCaptureSource.cpp)

qt_add_executable(microscope main.cpp)
target_link_libraries(microscope PRIVATE microscope_core Qt6::Quick)
