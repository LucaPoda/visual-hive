cmake_minimum_required(VERSION 3.10)
project(VisualHive)

# Set the C++ standard to C++17 to enable the filesystem library
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Fetch the nlohmann/json library ---
# This makes the project self-contained and avoids system-wide dependencies.
include(FetchContent)
FetchContent_Declare(
  json_library
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json_library)
# ----------------------------------------

# Find OpenCV
find_package(OpenCV REQUIRED)

# Add platform-specific macOS frameworks
if(APPLE)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices)
endif()

# Define the ConfigManager library
add_library(ConfigManager STATIC src/ConfigManager.cpp src/ConfigManager.h)
target_link_libraries(ConfigManager PUBLIC nlohmann_json::nlohmann_json)
# ADD THIS LINE to allow ConfigManager to find OpenCV headers
target_include_directories(ConfigManager PRIVATE ${OpenCV_INCLUDE_DIRS})


# Define the AssetManager library
add_library(AssetManager STATIC src/AssetManager.cpp src/AssetManager.h)
target_include_directories(AssetManager PUBLIC ${OpenCV_INCLUDE_DIRS})
# This was added to fix the previous json.hpp issue if AssetManager included ConfigManager.h
target_link_libraries(AssetManager PRIVATE nlohmann_json::nlohmann_json)
if(APPLE)
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES} ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
else()
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES})
endif()

# Define the main executable
add_executable(VisualHive src/main.cpp)

# Link the executable with all libraries
target_link_libraries(VisualHive PRIVATE ConfigManager AssetManager nlohmann_json::nlohmann_json)
if(APPLE)
    target_link_libraries(VisualHive PRIVATE ${OpenCV_LIBRARIES} ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
else()
    target_link_libraries(VisualHive PRIVATE ${OpenCV_LIBRARIES})
endif()

# The main executable needs the same include directories as the library
target_include_directories(VisualHive PRIVATE ${OpenCV_INCLUDE_DIRS})