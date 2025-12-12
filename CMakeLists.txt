cmake_minimum_required(VERSION 3.5)
project(VisualHive)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wno-deprecated-declarations)

# --- 1. Fetch nlohmann/json ---
include(FetchContent)
FetchContent_Declare(
  json_library
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json_library)

# --- 2. Fetch Asio ---
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-24-0
)
FetchContent_MakeAvailable(asio)

# --- 3. Fetch Ableton Link ---
FetchContent_Declare(
  ableton_link
  GIT_REPOSITORY https://github.com/Ableton/link.git
  GIT_TAG        Link-3.0.6
)
FetchContent_GetProperties(ableton_link)
if(NOT ableton_link_POPULATED)
  FetchContent_Populate(ableton_link)
endif()

# Setup Ableton::Link
include(${ableton_link_SOURCE_DIR}/AbletonLinkConfig.cmake)
target_compile_definitions(Ableton::Link INTERFACE ASIO_STANDALONE LINK_PLATFORM_ASIO=1)
target_include_directories(Ableton::Link INTERFACE ${asio_SOURCE_DIR}/asio/include)

# Find OpenCV
find_package(OpenCV REQUIRED)

# macOS Frameworks
if(APPLE)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices)
    find_library(COREAUDIO_LIBRARY CoreAudio)       
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox) 
endif()

# --- ConfigManager ---
add_library(ConfigManager STATIC src/ConfigManager.cpp src/ConfigManager.h)
target_link_libraries(ConfigManager PUBLIC nlohmann_json::nlohmann_json)
target_include_directories(ConfigManager PRIVATE ${OpenCV_INCLUDE_DIRS})

# --- AssetManager ---
add_library(AssetManager STATIC src/AssetManager.cpp src/AssetManager.h)
target_link_libraries(AssetManager PUBLIC nlohmann_json::nlohmann_json)
if(APPLE)
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES} ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
else()
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES})
endif()

# --- PlatformSpecificCode ---
add_library(PlatformSpecificCode STATIC src/PlatformSpecificCode.cpp src/PlatformSpecificCode.h)
if(APPLE)
    target_link_libraries(PlatformSpecificCode PRIVATE ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
endif()

# --- AbletonLinkManager ---
add_library(AbletonLinkManager src/AbletonLinkManager.cpp src/AbletonLinkManager.h)
target_link_libraries(AbletonLinkManager 
    PRIVATE 
    Ableton::Link
    ConfigManager
) 
target_include_directories(AbletonLinkManager PRIVATE ${OpenCV_INCLUDE_DIRS})

# --- NEW: AppGraphics (GraphicsManager + RuntimeState) ---
add_library(AppGraphics STATIC 
    src/GraphicsManager.cpp 
    src/GraphicsManager.h
    src/RuntimeState.cpp
    src/RuntimeState.h
)

# AppGraphics needs access to AssetManager, Config, Platform code and OpenCV
target_link_libraries(AppGraphics 
    PRIVATE
    AssetManager
    ConfigManager
    PlatformSpecificCode
    ${OpenCV_LIBRARIES}
)

# --- Main Executable ---
add_executable(VisualHive src/main.cpp)

target_link_libraries(VisualHive PRIVATE
    ConfigManager
    AssetManager
    AbletonLinkManager
    PlatformSpecificCode
    AppGraphics          # Link the new library
    ${OpenCV_LIBRARIES}
    Ableton::Link
)

target_include_directories(VisualHive PRIVATE
    ${json_library_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${ableton_link_SOURCE_DIR}/include
    src 
)

if(APPLE)
    target_link_libraries(VisualHive PRIVATE 
        ${COREGRAPHICS_LIBRARY} 
        ${APPLICATIONSERVICES_LIBRARY}
        ${COREAUDIO_LIBRARY} 
        ${AUDIOTOOLBOX_LIBRARY}
    )
endif()