cmake_minimum_required(VERSION 3.16)
project(VisualHive)

# Set the C++ standard to C++17 to enable the filesystem library
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Ableton Link library requires the Boost system component.
find_package(Boost REQUIRED COMPONENTS system)

# --- Fetch the nlohmann/json library ---
include(FetchContent)
FetchContent_Declare(
  json_library
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json_library)

# --- Link to Ableton Link using the local installation ---
# This method directly includes the AbletonLink configuration file.
include(../link/AbletonLinkConfig.cmake)

# Find OpenCV
find_package(OpenCV REQUIRED)

# Define the ConfigManager library
add_library(ConfigManager STATIC src/ConfigManager.cpp src/ConfigManager.h)
target_include_directories(ConfigManager PRIVATE ${json_library_SOURCE_DIR}/include)
target_include_directories(ConfigManager PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(ConfigManager PRIVATE nlohmann_json::nlohmann_json)

# Define the AssetManager library
add_library(AssetManager STATIC src/AssetManager.cpp src/AssetManager.h)
target_include_directories(AssetManager PUBLIC ${OpenCV_INCLUDE_DIRS})
target_include_directories(AssetManager PRIVATE ${json_library_SOURCE_DIR}/include)
if(APPLE)
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES})
endif()

# Define the AbletonLinkManager library
add_library(AbletonLinkManager src/AbletonLinkManager.cpp src/AbletonLinkManager.h)
target_link_libraries(AbletonLinkManager PRIVATE Ableton::Link)
target_include_directories(AbletonLinkManager PRIVATE ${json_library_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})

# Define the library target for your platform-specific code.
add_library(PlatformSpecificCode STATIC src/PlatformSpecificCode.cpp src/PlatformSpecificCode.h)

# Add platform-specific macOS frameworks to the library itself.
if(APPLE)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices)
    target_link_libraries(PlatformSpecificCode PRIVATE ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
endif()

# --- Define the main executable and explicitly list all source files ---
add_executable(VisualHive
    src/main.cpp
    src/VideoPlayerFacade.mm
)

set_source_files_properties(src/VideoPlayerFacade.mm PROPERTIES
    COMPILE_FLAGS "-x objective-c++ -ObjC"
)

if(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(APPKIT_FRAMEWORK AppKit)
    find_library(METAL_FRAMEWORK Metal)
    find_library(METALKIT_FRAMEWORK MetalKit)
    
    # Check if frameworks were found to provide a helpful error message
    if(NOT FOUNDATION_FRAMEWORK OR NOT APPKIT_FRAMEWORK OR NOT METAL_FRAMEWORK OR NOT METALKIT_FRAMEWORK)
        message(FATAL_ERROR "One or more required macOS frameworks not found.")
    endif()
    
    # Link the executable with all libraries and the found framework variables
    target_link_libraries(VisualHive PRIVATE
        ConfigManager
        AssetManager
        AbletonLinkManager
        PlatformSpecificCode
        ${OpenCV_LIBRARIES}
        Ableton::Link
        ${FOUNDATION_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${METALKIT_FRAMEWORK}
    )

else()
    # Link the executable with all libraries (non-Apple case)
    target_link_libraries(VisualHive PRIVATE
        ConfigManager
        AssetManager
        AbletonLinkManager
        PlatformSpecificCode
        ${OpenCV_LIBRARIES}
        Ableton::Link
    )
endif()

# Explicitly add the include directory for the main executable
target_include_directories(VisualHive PRIVATE
    ${json_library_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    src/ # To find all headers
)