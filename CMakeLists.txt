cmake_minimum_required(VERSION 3.10)
project(VisualHive)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wno-deprecated-declarations)

# Silenzia il warning per il fetch di repository senza CMakeLists (come ImGui)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

include(FetchContent)

# --- 1. Fetch nlohmann/json ---
FetchContent_Declare(
  json_library
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json_library)

# --- 2. Find OpenCV e OpenGL ---
find_package(OpenCV REQUIRED)
find_package(OpenGL REQUIRED)

# --- 3. Fetch GLFW ---
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE) # Evita il conflitto con target 'uninstall'
FetchContent_MakeAvailable(glfw)

# --- 4. Fetch Dear ImGui ---
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.90.4
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

# Compila ImGui e i suoi backend in una libreria statica
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC 
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PRIVATE glfw OpenGL::GL)

# --- 5. Fetch RtAudio ---
FetchContent_Declare(
  rtaudio
  GIT_REPOSITORY https://github.com/thestk/rtaudio.git
  GIT_TAG        6.0.1 
)
# Configura CMake per RtAudio
set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE) # Compila statico
set(RTAUDIO_INSTALL OFF CACHE BOOL "" FORCE)           # Evita conflitto 'uninstall'

# Forza la disabilitazione di API audio non necessarie
set(RTAUDIO_API_JACK OFF CACHE BOOL "" FORCE)
set(RTAUDIO_API_PULSE OFF CACHE BOOL "" FORCE)
set(RTAUDIO_API_ALSA OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(rtaudio)

# --- 6. Fetch BTrack (Beat Tracking) ---
FetchContent_Declare(
  btrack
  GIT_REPOSITORY https://github.com/adamstark/BTrack.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(btrack)

# --- macOS Frameworks ---
if(APPLE)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(APPLICATIONSERVICES_LIBRARY ApplicationServices)
    find_library(COREAUDIO_LIBRARY CoreAudio)       
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox) 
    find_library(SCREENCAPTUREKIT_LIBRARY ScreenCaptureKit)
    find_library(COREMEDIA_LIBRARY CoreMedia)
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

# --- Audio DSP ---
add_library(AudioAnalyzer STATIC src/AudioAnalyzer.cpp src/AudioAnalyzer.h)
target_link_libraries(AudioAnalyzer PRIVATE rtaudio BTrack)
target_include_directories(AudioAnalyzer PUBLIC 
    ${rtaudio_SOURCE_DIR} 
    ${btrack_SOURCE_DIR}/src
    ${btrack_SOURCE_DIR}/libs/kiss_fft130
)

# --- KNN Classifier (NUOVO MODULO) ---
add_library(KNNClassifier STATIC src/KNNClassifier.cpp src/KNNClassifier.h)
target_link_libraries(KNNClassifier PUBLIC AssetManager AudioAnalyzer)
target_include_directories(KNNClassifier PRIVATE ${OpenCV_INCLUDE_DIRS})

# --- AppGraphics (GraphicsManager + RuntimeState) ---
add_library(AppGraphics STATIC 
    src/GraphicsManager.cpp 
    src/GraphicsManager.h
    src/RuntimeState.cpp
    src/RuntimeState.h
)

# AppGraphics ora richiede anche KNNClassifier
target_link_libraries(AppGraphics 
    PUBLIC
    AssetManager
    ConfigManager
    AudioAnalyzer
    KNNClassifier
    PlatformSpecificCode
    imgui                 
    glfw                  
    OpenGL::GL            
    ${OpenCV_LIBRARIES}
)

# --- Main Executable ---
add_executable(VisualHive src/main.cpp)

# Rimosse dipendenze AbletonLinkManager e Ableton::Link, aggiunto KNNClassifier
target_link_libraries(VisualHive PRIVATE
    ConfigManager
    AssetManager
    PlatformSpecificCode
    AppGraphics          
    AudioAnalyzer      
    KNNClassifier
    ${OpenCV_LIBRARIES}
)

target_include_directories(VisualHive PRIVATE
    ${json_library_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    src 
)

if(APPLE)
    target_sources(PlatformSpecificCode PRIVATE src/PlatformSpecificCode_mac.mm)
    set_source_files_properties(src/PlatformSpecificCode_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(PlatformSpecificCode PRIVATE 
        ${OpenCV_LIBRARIES}
        "-framework CoreGraphics" 
        "-framework ApplicationServices"
        "-framework ScreenCaptureKit"
        "-framework CoreMedia"
        "-framework CoreVideo"
    )
    
    # RtAudio su Mac necessita di questi frameworks
    target_link_libraries(AudioAnalyzer PRIVATE 
        "-framework CoreAudio" 
        "-framework CoreFoundation"
    )
endif()