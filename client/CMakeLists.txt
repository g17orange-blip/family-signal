cmake_minimum_required(VERSION 3.21)
project(signal-client VERSION 0.3.6 LANGUAGES C CXX)   # C for the vendored monocypher

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- Qt6 ------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    WebSockets
    Sql
)

# --- GStreamer (via pkg-config; install via your package manager) ----------
# Arch:    sudo pacman -S gstreamer gst-plugins-base gst-plugins-good \
#                       gst-plugins-bad gst-plugins-ugly gst-libav
# macOS:   brew install gstreamer  (Homebrew installs all the plugin subsets as deps)
#          — or use the official installer from gstreamer.freedesktop.org and
#          export PKG_CONFIG_PATH=/Library/Frameworks/GStreamer.framework/Versions/1.0/lib/pkgconfig
# Windows: use the MSYS2 mingw64 packages, or the official gstreamer.freedesktop.org installers
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-sdp-1.0
    gstreamer-webrtc-1.0
    gstreamer-video-1.0
)

# --- Sources --------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/Config.h
    src/SignalingClient.cpp
    src/SignalingClient.h
    src/WebRtcSession.cpp
    src/WebRtcSession.h
    src/Contact.h
    src/ContactItemDelegate.cpp
    src/ContactItemDelegate.h
    src/ContactsModel.cpp
    src/ContactsModel.h
    src/HistoryCipher.cpp
    src/HistoryCipher.h
    src/Logging.cpp
    src/Logging.h
    src/Message.h
    src/MessageHistory.cpp
    src/MessageHistory.h
    third_party/monocypher/monocypher.c
    third_party/monocypher/monocypher.h
    src/ChatModel.cpp
    src/ChatModel.h
    src/MessageBubbleDelegate.cpp
    src/MessageBubbleDelegate.h
    src/ChatView.cpp
    src/ChatView.h
    src/ChatHeader.cpp
    src/ChatHeader.h
    src/MessageInputBar.cpp
    src/MessageInputBar.h
    src/CallWindow.cpp
    src/CallWindow.h
    src/Ringtone.cpp
    src/Ringtone.h
    src/FirstRunDialog.cpp
    src/FirstRunDialog.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/MainWindow.cpp
    src/MainWindow.h
)

qt6_add_resources(SOURCES resources/resources.qrc)

add_executable(signal-client ${SOURCES})

# Shown in the settings dialog so users can tell which build they run.
# Prefer the full git tag (e.g. 0.1.0-rc19) over the bare project version;
# local builds get the -N-g<hash>[-dirty] suffix, which is exactly what we
# want to see in a bug report.
execute_process(
    COMMAND git describe --tags --dirty --always
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE SIGNAL_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT SIGNAL_GIT_VERSION)
    set(SIGNAL_GIT_VERSION "${PROJECT_VERSION}")
endif()
string(REGEX REPLACE "^v" "" SIGNAL_GIT_VERSION "${SIGNAL_GIT_VERSION}")
target_compile_definitions(signal-client PRIVATE
    SIGNAL_VERSION="${SIGNAL_GIT_VERSION}")

target_link_libraries(signal-client PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::Sql
    PkgConfig::GST
)

if(WIN32)
    set_target_properties(signal-client PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(signal-client PRIVATE crypt32)   # DPAPI for HistoryCipher
    target_sources(signal-client PRIVATE packaging/signal.rc)   # exe icon
endif()
if(APPLE)
    # Custom Info.plist: macOS TCC kills any process touching the camera or
    # microphone unless the bundle declares usage descriptions.
    set_target_properties(signal-client PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Signal"
        MACOSX_BUNDLE_GUI_IDENTIFIER "ru.family-signal.client"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "signal.icns")
    target_sources(signal-client PRIVATE resources/signal.icns)
    set_source_files_properties(resources/signal.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
endif()
