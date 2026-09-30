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

qt_add_executable(microscope main.cpp AppContext.cpp)
target_link_libraries(microscope PRIVATE microscope_core Qt6::Quick)

qt_add_qml_module(microscope
    URI microscope
    VERSION 1.0
    QML_FILES ui/Main.qml ui/VideoView.qml ui/ControlBar.qml ui/StatusBanner.qml
    SOURCES
        AppContext.h AppContext.cpp
        view/ViewTransformModel.h view/ViewTransformModel.cpp
)

# The generated microscope_qmltyperegistrations.cpp guards each QML_ELEMENT
# header with `#if __has_include(<HeaderName.h>)` using the bare filename, so
# ViewTransformModel.h -- nested under view/, unlike AppContext.h -- needs its
# directory on this target's include path or that guard silently fails and
# the type is registered against an incomplete forward declaration.
target_include_directories(microscope PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/view)

if(APPLE)
    # Without NSCameraUsageDescription in the bundle's Info.plist, macOS
    # kills the process on first camera access with no dialog and no error
    # the user can act on (packaging/macos/Info.plist.in).
    set_target_properties(microscope PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in)
endif()
