cmake_minimum_required(VERSION 3.16)
project(VisualHive)

# Set the C++ standard to C++17 to enable the filesystem library
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find Boost as a dependency of Ableton Link ---
# The Ableton Link library requires the Boost system component.
find_package(Boost REQUIRED COMPONENTS system)
# --------------------------------------------------

# --- Fetch the nlohmann/json library ---
include(FetchContent)
FetchContent_Declare(
  json_library
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json_library)
# ----------------------------------------

# --- Link to Ableton Link using the local installation ---
# This method directly includes the AbletonLink configuration file.
include(../link/AbletonLinkConfig.cmake)
# -------------------------------------------------------

# Find OpenCV
find_package(OpenCV REQUIRED)

# Add platform-specific macOS frameworks
if(APPLE)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices)
endif()

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
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES} ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
else()
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES})
endif()

# Assuming you have a target for your AbletonLinkManager
add_library(AbletonLinkManager src/AbletonLinkManager.cpp src/AbletonLinkManager.h)
target_link_libraries(AbletonLinkManager PRIVATE Ableton::Link) # Assuming you have this
target_include_directories(AbletonLinkManager PRIVATE ${json_library_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})

# Define the library target for your platform-specific code.
add_library(PlatformSpecificCode STATIC src/PlatformSpecificCode.cpp src/PlatformSpecificCode.h)

# Add platform-specific macOS frameworks to the library itself.
if(APPLE)
    target_link_libraries(PlatformSpecificCode PRIVATE ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
endif()
# Define the main executable
add_executable(VisualHive src/main.cpp)

# Link the executable with all libraries
target_link_libraries(VisualHive PRIVATE
    ConfigManager
    AssetManager
    AbletonLinkManager
    PlatformSpecificCode
    ${OpenCV_LIBRARIES} # Corrected linking for OpenCV
    Ableton::Link
)

# Explicitly add the include directory for the main executable
target_include_directories(VisualHive PRIVATE
    ${json_library_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

if(APPLE)
    target_link_libraries(VisualHive PRIVATE ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
endif()
