# Set the minimum required version of CMake
# Using a higher version to support C++17 features and modern CMake practices.
cmake_minimum_required(VERSION 3.16)

# Define the project name and language
project(VisualHive CXX)

# Set the build type to Release for optimization
set(CMAKE_BUILD_TYPE Release)

# Set the C++ standard to C++17 to enable the filesystem library and other modern features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the Homebrew prefix path for Aubio
set(HOMEBREW_PREFIX "/opt/homebrew")

# --- Include PortAudio from source ---
# This command adds the 'portaudio' sub-project and its build targets.
add_subdirectory(portaudio)

# --- Find Aubio ---
# Manually locate the Aubio header file.
find_path(AUBIO_INCLUDE_DIR aubio/aubio.h
  HINTS ${HOMEBREW_PREFIX}/include
  NO_DEFAULT_PATH
)

# Manually locate the Aubio library file.
find_library(AUBIO_LIBRARY aubio
  HINTS ${HOMEBREW_PREFIX}/lib
  NO_DEFAULT_PATH
)

# --- Check if the libraries were found ---
if(NOT AUBIO_LIBRARY OR NOT AUBIO_INCLUDE_DIR)
  message(FATAL_ERROR "Aubio was not found. Please ensure it's installed with Homebrew.")
endif()

# --- Fetch the nlohmann/json library ---
# This block handles downloading and building the JSON library automatically.
include(FetchContent)
FetchContent_Declare(
  json_library
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json_library)

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

# Define the library target for your platform-specific code.
add_library(PlatformSpecificCode STATIC src/PlatformSpecificCode.cpp src/PlatformSpecificCode.h)

# Add platform-specific macOS frameworks to the library itself.
if(APPLE)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices)
    target_link_libraries(PlatformSpecificCode PRIVATE ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
endif()

# Define the beat detection library
add_library(BpmDetector STATIC src/BpmDetector.cpp src/BpmDetector.h)
target_link_libraries(BpmDetector PRIVATE portaudio ${AUBIO_LIBRARY} "-framework CoreAudio" "-framework AudioToolbox" "-framework Accelerate")
target_include_directories(BpmDetector PRIVATE ${AUBIO_INCLUDE_DIR})

# --- Define the main executable and explicitly list all source files ---
# This now includes the `VisualHive` executable and its source files.
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
        PlatformSpecificCode
        BpmDetector # Add the new library here
        ${OpenCV_LIBRARIES}
        portaudio
        ${AUBIO_LIBRARY}
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework Accelerate"
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
        PlatformSpecificCode
        BpmDetector # Add the new library here
        ${OpenCV_LIBRARIES}
        portaudio
        ${AUBIO_LIBRARY}
    )
endif()

# Explicitly add the include directories for the main executable
target_include_directories(VisualHive PRIVATE
    ${json_library_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${AUBIO_INCLUDE_DIR}
    src/ # To find all headers
)
