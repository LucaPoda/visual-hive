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

# --- DISABILITA LA COMPILAZIONE DI ESEMPI E TEST DI ABLETON ---
set(LINK_BUILD_QT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LINK_BUILD_AUDIO_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LINK_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Usa MakeAvailable invece del vecchio Populate per evitare il warning
FetchContent_MakeAvailable(ableton_link)

# Setup Ableton::Link
include(${ableton_link_SOURCE_DIR}/AbletonLinkConfig.cmake)
target_compile_definitions(Ableton::Link INTERFACE ASIO_STANDALONE LINK_PLATFORM_ASIO=1)
target_include_directories(Ableton::Link INTERFACE ${asio_SOURCE_DIR}/asio/include)

# --- 4. Find OpenCV e OpenGL ---
find_package(OpenCV REQUIRED)
find_package(OpenGL REQUIRED)

# --- 5. Fetch GLFW ---
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

# --- 6. Fetch Dear ImGui ---
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

# --- 7. Fetch RtAudio ---
FetchContent_Declare(
  rtaudio
  GIT_REPOSITORY https://github.com/thestk/rtaudio.git
  GIT_TAG        6.0.1 
)
# Configura CMake per RtAudio
set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE) # Compila statico
set(RTAUDIO_INSTALL OFF CACHE BOOL "" FORCE)           # Evita conflitto 'uninstall'

# Forza la disabilitazione di API audio non necessarie (risolve l'errore 'jack non trovato')
set(RTAUDIO_API_JACK OFF CACHE BOOL "" FORCE)
set(RTAUDIO_API_PULSE OFF CACHE BOOL "" FORCE)
set(RTAUDIO_API_ALSA OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(rtaudio)

# --- 8. Fetch BTrack (Beat Tracking) ---
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

# --- AbletonLinkManager ---
add_library(AbletonLinkManager src/AbletonLinkManager.cpp src/AbletonLinkManager.h)
target_link_libraries(AbletonLinkManager 
    PRIVATE 
    Ableton::Link
    ConfigManager
) 
target_include_directories(AbletonLinkManager PRIVATE ${OpenCV_INCLUDE_DIRS})

# --- Audio DSP ---
add_library(AudioAnalyzer STATIC src/AudioAnalyzer.cpp src/AudioAnalyzer.h)
# Colleghiamo RtAudio e BTrack all'Analyzer
target_link_libraries(AudioAnalyzer PRIVATE rtaudio BTrack)
target_include_directories(AudioAnalyzer PUBLIC 
    ${rtaudio_SOURCE_DIR} 
    ${btrack_SOURCE_DIR}/src
    ${btrack_SOURCE_DIR}/libs/kiss_fft130
)

# --- AppGraphics (GraphicsManager + RuntimeState) ---
add_library(AppGraphics STATIC 
    src/GraphicsManager.cpp 
    src/GraphicsManager.h
    src/RuntimeState.cpp
    src/RuntimeState.h
)

# AppGraphics ha bisogno di AssetManager, Config, Platform code, OpenCV e ImGui
target_link_libraries(AppGraphics 
    PUBLIC
    AssetManager
    ConfigManager
    AudioAnalyzer
    PlatformSpecificCode
    imgui                 
    glfw                  
    OpenGL::GL            
    ${OpenCV_LIBRARIES}
)

# --- Main Executable ---
add_executable(VisualHive src/main.cpp)

target_link_libraries(VisualHive PRIVATE
    ConfigManager
    AssetManager
    AbletonLinkManager
    PlatformSpecificCode
    AppGraphics          
    AudioAnalyzer      
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