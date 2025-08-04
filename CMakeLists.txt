cmake_minimum_required(VERSION 3.10)
project(VisualHive)

# Set the C++ standard to C++17 to enable the filesystem library
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCV
find_package(OpenCV REQUIRED)

# Add platform-specific macOS frameworks
if(APPLE)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices)
endif()

# Define the AssetManager library
add_library(AssetManager STATIC src/AssetManager.cpp src/AssetManager.h)
target_include_directories(AssetManager PUBLIC ${OpenCV_INCLUDE_DIRS})
if(APPLE)
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES} ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
else()
    target_link_libraries(AssetManager PRIVATE ${OpenCV_LIBRARIES})
endif()

# Define the main executable
add_executable(VisualHive src/main.cpp)

# Link the executable with the AssetManager library and OpenCV
target_link_libraries(VisualHive PRIVATE AssetManager)
if(APPLE)
    target_link_libraries(VisualHive PRIVATE ${OpenCV_LIBRARIES} ${COREGRAPHICS_LIBRARY} ${APPLICATIONSERVICES_LIBRARY})
else()
    target_link_libraries(VisualHive PRIVATE ${OpenCV_LIBRARIES})
endif()

# The main executable needs the same include directories as the library
target_include_directories(VisualHive PRIVATE ${OpenCV_INCLUDE_DIRS})
