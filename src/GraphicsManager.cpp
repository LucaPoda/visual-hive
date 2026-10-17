#include "GraphicsManager.h"
#include <cmath>
#include <iostream>
#include <filesystem>
#include <ctime>
#include <algorithm>
#include <set>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;

extern KNNClassifier knnClassifier;

GraphicsManager::GraphicsManager(const AppConfig& config, const DisplayInfo& display) 
: windowName(config.windowName), displayWidth(display.width), displayHeight(display.height), strobeDurationMs(config.strobeDurationMs), localConfig(config), projectorWindow(nullptr), targetSettingsTab("")
{
    if (!glfwInit()) {
        std::cerr << "[ERROR] Impossibile inizializzare GLFW!" << std::endl;
        exit(1);
    }
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); 
    #endif
    
    uiWindow = glfwCreateWindow(config.uiWindowWidth, config.uiWindowHeight, "Visual-Hive - Regia / Dashboard", NULL, NULL);
    if (!uiWindow) {
        glfwTerminate();
        std::cerr << "[ERROR] Impossibile creare la finestra di regia GLFW!" << std::endl;
        exit(1);
    }
    
    glfwMakeContextCurrent(uiWindow);
    glfwSwapInterval(1);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    
    ImGui_ImplGlfw_InitForOpenGL(uiWindow, true);
    ImGui_ImplOpenGL3_Init("#version 150"); 
}

GraphicsManager::~GraphicsManager() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    
    if (liveTextureID != 0) glDeleteTextures(1, &liveTextureID);
    if (cueTextureID != 0) glDeleteTextures(1, &cueTextureID);
    if (projectorTexID != 0) glDeleteTextures(1, &projectorTexID);
    
    for (auto& pair : thumbnailCache) {
        glDeleteTextures(1, &pair.second);
    }
    thumbnailCache.clear();
    
    if (projectorWindow) {
        glDeleteVertexArrays(1, &quadVAO);
        glDeleteBuffers(1, &quadVBO);
        glDeleteProgram(shaderProgram);
        glfwDestroyWindow(projectorWindow);
    }
    
    glfwDestroyWindow(uiWindow);
    glfwTerminate();
}

GLuint GraphicsManager::getOrCreateThumbnail(const std::string& key, const cv::Mat& firstFrame, bool isGrayscale) {
    std::string cacheKey = key + (isGrayscale ? "_gray" : "_color");
    if (thumbnailCache.find(cacheKey) != thumbnailCache.end()) {
        return thumbnailCache[cacheKey];
    }
    
    cv::Mat resized;
    if(!firstFrame.empty()) {
        cv::resize(firstFrame, resized, cv::Size(160, 90));
    } else {
        resized = cv::Mat(90, 160, CV_8UC3, cv::Scalar(0,0,0));
    }
    
    if (isGrayscale) {
        cv::Mat gray;
        if (resized.channels() == 4) cv::cvtColor(resized, gray, cv::COLOR_BGRA2GRAY);
        else if (resized.channels() == 3) cv::cvtColor(resized, gray, cv::COLOR_BGR2GRAY);
        else gray = resized.clone();
        cv::cvtColor(gray, resized, cv::COLOR_GRAY2RGBA);
    } else {
        if (resized.channels() == 3) {
            cv::cvtColor(resized, resized, cv::COLOR_BGR2RGBA);
        } else if (resized.channels() == 4) {
            cv::cvtColor(resized, resized, cv::COLOR_BGRA2RGBA);
        } else if (resized.channels() == 1) {
            cv::cvtColor(resized, resized, cv::COLOR_GRAY2RGBA);
        }
    }
    
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, resized.cols, resized.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, resized.ptr());
    
    thumbnailCache[cacheKey] = texID;
    return texID;
}

void GraphicsManager::createProjectorWindow(int monitorIndex) {
    int count;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (monitorIndex < 0 || monitorIndex >= count) return;
    
    if (projectorWindow) {
        glfwDestroyWindow(projectorWindow);
        projectorWindow = nullptr;
    }

    GLFWmonitor* monitor = monitors[monitorIndex];
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    displayWidth = mode->width;
    displayHeight = mode->height;

    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    projectorWindow = glfwCreateWindow(displayWidth, displayHeight, windowName.c_str(), monitor, uiWindow);
    if (!projectorWindow) {
        std::cerr << "[ERROR] Impossibile creare la finestra del proiettore GLFW!" << std::endl;
        return;
    }

    setupProjectorQuad();
    glfwMakeContextCurrent(uiWindow);
}

void GraphicsManager::setupProjectorQuad() {
    glfwMakeContextCurrent(projectorWindow);
    
    const char* vShader = "#version 150\n in vec2 pos; in vec2 tex; out vec2 vTex; void main() { vTex=tex; gl_Position=vec4(pos,0.0,1.0); }";
    const char* fShader = "#version 150\n in vec2 vTex; out vec4 color; uniform sampler2D texMap; void main() { color=texture(texMap,vec2(vTex.x, 1.0-vTex.y)); }";
    
    auto compile = [](GLuint type, const char* src) {
        GLuint s = glCreateShader(type); 
        glShaderSource(s, 1, &src, nullptr); 
        glCompileShader(s); 
        return s;
    };
    
    GLuint vs = compile(GL_VERTEX_SHADER, vShader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fShader);
    shaderProgram = glCreateProgram(); 
    glAttachShader(shaderProgram, vs); 
    glAttachShader(shaderProgram, fs); 
    glLinkProgram(shaderProgram);
    
    float vertices[] = { 
        -1.0f, -1.0f,  0.0f, 0.0f,  
        1.0f, -1.0f,  1.0f, 0.0f,  
        1.0f,  1.0f,  1.0f, 1.0f,  
        -1.0f,  1.0f,  0.0f, 1.0f 
    }; 
    
    glGenVertexArrays(1, &quadVAO); 
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO); 
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    GLint posAttrib = glGetAttribLocation(shaderProgram, "pos");
    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    
    GLint texAttrib = glGetAttribLocation(shaderProgram, "tex");
    glEnableVertexAttribArray(texAttrib);
    glVertexAttribPointer(texAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}

void GraphicsManager::UpdateGLTexture(const cv::Mat& mat, GLuint& textureID) {
    if (mat.empty()) return;
    
    cv::Mat rgbaMat;
    if(mat.channels() == 3) {
        cv::cvtColor(mat, rgbaMat, cv::COLOR_BGR2RGBA);
    } else if (mat.channels() == 4) {
        cv::cvtColor(mat, rgbaMat, cv::COLOR_BGRA2RGBA);
    } else {
        cv::cvtColor(mat, rgbaMat, cv::COLOR_GRAY2RGBA);
    }
    
    if (textureID == 0) {
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgbaMat.cols, rgbaMat.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaMat.ptr());
    } else {
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rgbaMat.cols, rgbaMat.rows, GL_RGBA, GL_UNSIGNED_BYTE, rgbaMat.ptr());
    }
}

void GraphicsManager::drawPaletteSquare(const std::string& primary, const std::string& secondary, float size) {
    auto getCol32 = [](const std::string& name) -> ImU32 {
        if (name == "Bianco") return IM_COL32(255, 255, 255, 255);
        if (name == "Nero") return IM_COL32(0, 0, 0, 255);
        if (name == "Rosso") return IM_COL32(255, 0, 0, 255);
        if (name == "Verde") return IM_COL32(0, 255, 0, 255);
        if (name == "Blu") return IM_COL32(0, 0, 255, 255);
        if (name == "Viola") return IM_COL32(128, 0, 128, 255);
        if (name == "Giallo") return IM_COL32(255, 255, 0, 255);
        if (name == "Arancione") return IM_COL32(255, 165, 0, 255);
        return IM_COL32(128, 128, 128, 255);
    };

    ImU32 col1 = getCol32(primary);
    ImU32 col2 = secondary.empty() ? col1 : getCol32(secondary);

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddTriangleFilled(p, ImVec2(p.x + size, p.y), ImVec2(p.x, p.y + size), col1);
    drawList->AddTriangleFilled(ImVec2(p.x + size, p.y), ImVec2(p.x + size, p.y + size), ImVec2(p.x, p.y + size), col2);
    
    // Draw Border
    drawList->AddRect(p, ImVec2(p.x + size, p.y + size), IM_COL32(200, 200, 200, 255));
    
    ImGui::Dummy(ImVec2(size, size));
}

void GraphicsManager::beginDashboard() {
    glfwMakeContextCurrent(uiWindow);
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GraphicsManager::endDashboard(const cv::Mat& liveFrame, const cv::Mat& cueFrame, RuntimeState& state, double bpm, AudioAnalyzer& audioAnalyzer, KNNPrediction pred, AssetManager& assetMgr) {
    glfwMakeContextCurrent(uiWindow);
    
    bool openSettingsModal = false;
    
    UpdateGLTexture(liveFrame, liveTextureID);
    UpdateGLTexture(cueFrame, cueTextureID);
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    ImVec2 contentAvail = ImGui::GetContentRegionAvail();
    float topHeight = contentAvail.y * 0.40f + 15.0f;
    float bottomHeight = contentAvail.y - topHeight - 32.0f;
    
    ImGui::Text("=== SISTEMA DI REGIA VJ ===");
    ImGui::Separator();
    
    ImGui::BeginChild("TopArea", ImVec2(0, topHeight), false);
    ImGui::Columns(2, "TopColumns", false);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.70f);
    
    ImGui::BeginChild("PreviewsArea", ImVec2(0, 0), true);
    
    float availWidth = ImGui::GetContentRegionAvail().x;
    float previewWidth = (availWidth / 2.0f) - 10.0f;
    float previewHeight = previewWidth * (9.0f / 16.0f);
    
    ImGui::Columns(2, "CueLiveCols", false);
    
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "CUE (PREVIEW)");
    if (cueTextureID != 0) ImGui::Image((void*)(intptr_t)cueTextureID, ImVec2(previewWidth, previewHeight));
    
    ImGui::NextColumn();
    
    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "LIVE (ON AIR)");
    if (liveTextureID != 0) ImGui::Image((void*)(intptr_t)liveTextureID, ImVec2(previewWidth, previewHeight));
    
    ImGui::Columns(1);
    
    ImGui::Dummy(ImVec2(0, 10));
    if (ImGui::Button("Transizione CUE -> LIVE (INVIO)", ImVec2(-1, 30))) state.transitionCueToLive();
    
    ImGui::EndChild();
    
    ImGui::NextColumn();
    
    AudioMetrics audioData = audioAnalyzer.getMetrics();
    
    ImGui::BeginChild("MonitoringAuto", ImVec2(0, 0), true);
    
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "OUTPUT & PROIEZIONE");
    ImGui::Separator();
    
    static int monitorCount = 0;
    static GLFWmonitor** availableMonitors = glfwGetMonitors(&monitorCount);
    static int currentMonitorIdx = -1;
    
    std::string monitorPreviewText = (currentMonitorIdx >= 0 && currentMonitorIdx < monitorCount) 
                                      ? glfwGetMonitorName(availableMonitors[currentMonitorIdx]) 
                                      : "Seleziona Monitor...";
    
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##MonitorOutput", monitorPreviewText.c_str())) {
        for (int i = 0; i < monitorCount; ++i) {
            bool isSelected = (currentMonitorIdx == i);
            if (ImGui::Selectable(glfwGetMonitorName(availableMonitors[i]), isSelected)) {
                currentMonitorIdx = i;
                createProjectorWindow(i);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Refresh##Monitors")) {
        availableMonitors = glfwGetMonitors(&monitorCount);
    }
    
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::Checkbox("BLACKOUT", &state.isBlackout);
    ImGui::PopStyleColor();
    
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::Separator();
    
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "AUDIO INPUT & MONITORING");
    ImGui::Separator();
    
    static std::vector<AudioDeviceInfo> audioDevices = audioAnalyzer.getInputDevices();
    static std::string currentDeviceName = "Seleziona Dispositivo...";
    if (currentDeviceName == "Seleziona Dispositivo..." && !audioDevices.empty()) {
        unsigned int activeId = audioAnalyzer.getCurrentDeviceId();
        for (const auto& dev : audioDevices) {
            if (dev.id == activeId) currentDeviceName = dev.name;
        }
    }
    
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##AudioInterface", currentDeviceName.c_str())) {
        for (const auto& device : audioDevices) {
            bool isSelected = (currentDeviceName == device.name);
            if (ImGui::Selectable(device.name.c_str(), isSelected)) {
                currentDeviceName = device.name;
                audioAnalyzer.stop();
                audioAnalyzer.start(device.id);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) audioDevices = audioAnalyzer.getInputDevices();

    static float peakHoldL = 0.0f, peakHoldR = 0.0f;
    if (audioData.peakL > peakHoldL) peakHoldL = audioData.peakL; else peakHoldL *= 0.999f;
    if (audioData.peakR > peakHoldR) peakHoldR = audioData.peakR; else peakHoldR *= 0.999f;

    auto drawDbMeter = [](const char* label, float linearPeak, float linearHold) {
        float db = (linearPeak > 0.0001f) ? 20.0f * std::log10(linearPeak) : -60.0f;
        float fraction = std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
        ImVec4 color = (db >= -0.1f) ? ImVec4(1.0f, 0.1f, 0.1f, 1.0f) : (db > -6.0f) ? ImVec4(1.0f, 0.8f, 0.1f, 1.0f) : ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        ImGui::Text("%s", label); ImGui::SameLine(25.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 8.0f), ""); 
        ImGui::PopStyleColor();
    };
    drawDbMeter("L", audioData.peakL, peakHoldL);
    drawDbMeter("R", audioData.peakR, peakHoldR);

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    static bool isRecording = false;
    static std::chrono::steady_clock::time_point recStart;
    if (!isRecording) {
        if (ImGui::Button("Inizia Registrazione")) {
            isRecording = true;
            recStart = std::chrono::steady_clock::now();
        }
    } else {
        if (ImGui::Button("Stop Registrazione")) {
            isRecording = false;
        }
        ImGui::SameLine();
        auto diff = std::chrono::steady_clock::now() - recStart;
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(diff).count();
        ImGui::Text("Rec: %02lld:%02lld", secs / 60, secs % 60);
    }
    ImGui::SameLine();
    if (ImGui::Button("Apri Cartella")) {
        #ifdef __APPLE__
        system("open recordings/");
        #endif
    }
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "CONFIGURAZIONE LIVE");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("LIVE SETTINGS").x - 30.0f);
    if (ImGui::Button("LIVE SETTINGS")) { 
        targetSettingsTab = "Live & System Settings"; 
        openSettingsModal = true; 
    }
    ImGui::Separator();
    
    ImGui::Text("Fase Evento:");
    int ep = static_cast<int>(state.currentEventPhase);
    if (ImGui::RadioButton("Ambient", &ep, AMBIENT)) state.currentEventPhase = AMBIENT; ImGui::SameLine();
    if (ImGui::RadioButton("Low", &ep, LOW)) state.currentEventPhase = LOW; ImGui::SameLine();
    if (ImGui::RadioButton("Mid", &ep, MID)) state.currentEventPhase = MID; ImGui::SameLine();
    if (ImGui::RadioButton("High", &ep, HIGH)) state.currentEventPhase = HIGH;
    
    static int colorMode = 0; // 0 = Color, 1 = Palette
    ImGui::RadioButton("COLOR MODE", &colorMode, 0); ImGui::SameLine();
    ImGui::RadioButton("PALETTE MODE", &colorMode, 1);

    ImGui::Text("Colori/Palette Attivi:");
    if (colorMode == 0) {
        const char* colors[] = {"Bianco", "Nero", "Rosso", "Verde", "Blu", "Viola", "Giallo", "Arancione"};
        for (int i = 0; i < 8; i++) {
            bool active = std::find(state.activePalettes.begin(), state.activePalettes.end(), colors[i]) != state.activePalettes.end();
            if (ImGui::Checkbox(colors[i], &active)) {
                if (active) state.activePalettes.push_back(colors[i]);
                else state.activePalettes.erase(std::remove(state.activePalettes.begin(), state.activePalettes.end(), colors[i]), state.activePalettes.end());
            }
            if ((i + 1) % 4 != 0) ImGui::SameLine();
        }
    } else {
        std::set<std::pair<std::string, std::string>> uniquePalettes;
        for (auto& [key, bg] : assetMgr.getMutableAssets().get_backgrounds()) {
            if (!bg.get_primary_color().empty()) {
                uniquePalettes.insert({bg.get_primary_color(), bg.get_secondary_color()});
            }
        }
        
        int count = 0;
        for (const auto& pal : uniquePalettes) {
            std::string palKey = pal.first + (pal.second.empty() ? "" : "-" + pal.second);
            bool isActive = std::find(state.activePalettes.begin(), state.activePalettes.end(), palKey) != state.activePalettes.end();
            
            ImVec2 p = ImGui::GetCursorScreenPos();
            drawPaletteSquare(pal.first, pal.second, 30.0f);
            
            if (isActive) {
                ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + 30.0f, p.y + 30.0f), IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
            }
            
            ImGui::SetCursorScreenPos(p);
            if (ImGui::InvisibleButton(palKey.c_str(), ImVec2(30.0f, 30.0f))) {
                if (isActive) state.activePalettes.erase(std::remove(state.activePalettes.begin(), state.activePalettes.end(), palKey), state.activePalettes.end());
                else state.activePalettes.push_back(palKey);
            }
            
            if ((count + 1) % 6 != 0) ImGui::SameLine();
            count++;
        }
        if (count % 6 != 0) ImGui::NewLine();
    }
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "AUTO MODE & MACHINE LEARNING");
    ImGui::Separator();
    ImGui::Checkbox("Auto Mode (M)", &state.isAutoMode);
    ImGui::EndChild();
    
    ImGui::EndChild();
    
    ImGui::Columns(1);
    
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::Separator();
    
    ImGui::BeginChild("BottomArea", ImVec2(0, bottomHeight), false);
    ImGui::Columns(2, "BottomColumns", false);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.70f);
    
    ImGui::BeginChild("BackgroundDeckArea", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "BACKGROUND DECK");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("BACKGROUND IMPORT / MANAGE").x - 30.0f);
    if (ImGui::Button("BACKGROUND IMPORT / MANAGE")) { 
        targetSettingsTab = "Backgrounds"; 
        openSettingsModal = true; 
    }
    
    if (ImGui::BeginTabBar("BgTabs")) {
        const char* tabNames[] = {"TUTTI", "AMBIENT", "LOW", "MID", "HIGH", "CATTURE FINESTRA"};
        for (int t = 0; t < 6; ++t) {
            if (ImGui::BeginTabItem(tabNames[t])) {
                auto& bgs = assetMgr.getMutableAssets().get_mutable_backgrounds();
                
                if (t == 5) {
                    for (auto& [key, bg] : bgs) {
                        if (bg.get_type() == WINDOW_CAPTURE) {
                            ImGui::PushID(key.c_str());
                            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Sorgente: %s", key.c_str());
                            if (ImGui::Button("Invia in CUE", ImVec2(200, 30))) state.switchCueBackground(bg);
                            ImGui::Separator();
                            ImGui::PopID();
                        }
                    }
                } else {
                    ImGui::Columns(3, "BgCols", true);
                    ImGui::Text("VERSE"); ImGui::NextColumn();
                    ImGui::Text("BUILD"); ImGui::NextColumn();
                    ImGui::Text("CHORUS"); ImGui::NextColumn();
                    ImGui::Separator();
                    
                    std::map<std::string, std::vector<std::string>> verseMap, buildMap, chorusMap;
                    
                    for (auto& [key, bg] : bgs) {
                        if (bg.get_type() == WINDOW_CAPTURE) continue; 
                        
                        bool match = (t == 0);
                        if (!match) {
                            EventPhase phase = bg.get_event_phase();
                            if (t == 1 && phase == AMBIENT) match = true;
                            if (t == 2 && phase == LOW) match = true;
                            if (t == 3 && phase == MID) match = true;
                            if (t == 4 && phase == HIGH) match = true;
                        }
                        if (match) {
                            SongDynamic dyn = bg.get_song_dynamic();
                            std::string combinedColor = bg.get_primary_color();
                            if (!bg.get_secondary_color().empty()) combinedColor += " - " + bg.get_secondary_color();
                            if (combinedColor.empty()) combinedColor = "Nessuna Palette";

                            if (dyn == VERSE || dyn == MANUAL) verseMap[combinedColor].push_back(key);
                            else if (dyn == BUILD) buildMap[combinedColor].push_back(key);
                            else if (dyn == CHORUS) chorusMap[combinedColor].push_back(key);
                        }
                    }
                    
                    auto renderGroup = [&](std::map<std::string, std::vector<std::string>>& groupMap) {
                        for (auto& [pal, keys] : groupMap) {
                            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "-- %s --", pal.c_str());
                            int itemCount = 0;
                            for (const auto& key : keys) {
                                Background& bg = bgs[key];
                                bool enabled = bg.get_is_enabled_global();
                                std::string cacheKey = key + (!enabled ? "_gray" : "_color");
                                GLuint tex = 0;
                                auto cacheIt = thumbnailCache.find(cacheKey);
                                if (cacheIt != thumbnailCache.end()) tex = cacheIt->second;
                                else tex = getOrCreateThumbnail(key, bg.get_first_frame(), !enabled);
                                
                                ImGui::PushID(key.c_str());
                                if (itemCount > 0 && itemCount % 2 != 0) ImGui::SameLine();
                                if (ImGui::ImageButton((void*)(intptr_t)tex, ImVec2(160, 90))) state.switchCueBackground(bg);
                                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) bg.set_is_enabled_global(!enabled);
                                ImGui::PopID();
                                itemCount++;
                            }
                        }
                    };
                    
                    renderGroup(verseMap); ImGui::NextColumn();
                    renderGroup(buildMap); ImGui::NextColumn();
                    renderGroup(chorusMap); ImGui::NextColumn();
                    ImGui::Columns(1);
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    
    ImGui::NextColumn();
    
    ImGui::BeginChild("TextGenArea", ImVec2(0, 110), true);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "GENERATORE TESTO (LIVE)");
    static char textBuffer[256] = "";
    static int fontSize = 5;
    ImGui::InputText("Testo", textBuffer, sizeof(textBuffer));
    ImGui::SliderInt("Size", &fontSize, 1, 10);
    if (ImGui::Button("Invia Testo in CUE", ImVec2(-1, 30))) {
        Foreground textFg = assetMgr.createTextForeground(std::string(textBuffer), fontSize);
        state.switchCueForeground(textFg);
    }
    ImGui::EndChild();
    
    ImGui::BeginChild("ForegroundDeckArea", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "FOREGROUND DECK");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("FOREGROUND IMPORT / MANAGE").x - 30.0f);
    if (ImGui::Button("FOREGROUND IMPORT / MANAGE")) { 
        targetSettingsTab = "Foregrounds"; 
        openSettingsModal = true; 
    }
    
    auto& mutableFgs = assetMgr.getMutableAssets().get_mutable_foregrounds();
    int itemCount = 0;
    for (auto& [key, fg] : mutableFgs) {
        bool enabled = fg.get_is_enabled_global();
        std::string cacheKey = key + (!enabled ? "_gray" : "_color");
        GLuint tex = 0;
        auto cacheIt = thumbnailCache.find(cacheKey);
        
        if (cacheIt != thumbnailCache.end()) tex = cacheIt->second;
        else {
            cv::Mat firstFrame = fg.get_first_frame();
            if (firstFrame.empty()) continue; 
            tex = getOrCreateThumbnail(key, firstFrame, !enabled);
        }
        
        ImGui::PushID(key.c_str());
        if (itemCount > 0 && itemCount % 2 != 0) ImGui::SameLine();
        if (ImGui::ImageButton((void*)(intptr_t)tex, ImVec2(160, 90))) state.switchCueForeground(fg);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) fg.set_is_enabled_global(!enabled);
        ImGui::PopID();
        itemCount++;
    }
    ImGui::EndChild();
    
    ImGui::EndChild(); 
    ImGui::Columns(1);
    
    // ================= HELPER DROPDOWN COLORI ================= //
    const char* colorNames[] = {"Nessuno", "Bianco", "Nero", "Rosso", "Verde", "Blu", "Viola", "Giallo", "Arancione"};
    auto renderColorDropdown = [&](const char* label, std::string& currentColor) {
        int currentIdx = 0;
        for (int i = 1; i < 9; i++) {
            if (currentColor == colorNames[i]) { currentIdx = i; break; }
        }
        if (ImGui::Combo(label, &currentIdx, "Nessuno\0Bianco\0Nero\0Rosso\0Verde\0Blu\0Viola\0Giallo\0Arancione\0")) {
            currentColor = (currentIdx == 0) ? "" : colorNames[currentIdx];
        }
    };

    // ================= MODALE UNIFICATA IMPOSTAZIONI ================= //
    if (openSettingsModal) {
        ImGui::OpenPopup("Settings");
    }
    
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * 0.9f, ImGui::GetIO().DisplaySize.y * 0.9f));
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    
    if (ImGui::BeginPopupModal("Settings", NULL, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        
        if (ImGui::BeginTabBar("SettingsTabs")) {
            
            // TAB BACKGROUNDS
            ImGuiTabItemFlags bgFlags = (targetSettingsTab == "Backgrounds") ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Backgrounds", nullptr, bgFlags)) {
                if (targetSettingsTab == "Backgrounds") targetSettingsTab = "";
                
                // Unica scroll area per questa tab, lascia 50px sul fondo liberi per i bottoni "chiudi"
                if (ImGui::BeginChild("BgTabScrollArea", ImVec2(0, ImGui::GetContentRegionAvail().y - 50.0f), false)) {
                    if (ImGui::Button("Importa Background")) {
                        auto files = openMultipleFilesDialog("Seleziona Video Background", {"mp4", "mov", "avi", "mkv"});
                        for (const auto& file : files) {
                            Background newBg;
                            newBg.set_source(VIDEO_LOOP, fs::path(file).filename().string()); 
                            auto task = assetMgr.analyzeVideoAsync(file);
                            assetMgr.getStagingBackgrounds().push_back({newBg, task});
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Aggiungi Colore Fisso")) {
                        Background newBg;
                        newBg.set_source(SOLID_COLOR, "#000000"); 
                        newBg.set_primary_color("Nero");
                        auto task = std::make_shared<VideoAnalysisTask>();
                        task->filepath = "#000000";
                        task->isDone = true;
                        task->primaryColor = "Nero";
                        assetMgr.getStagingBackgrounds().push_back({newBg, task});
                    }
                    
                    if (!assetMgr.getStagingBackgrounds().empty()) {
                        ImGui::Dummy(ImVec2(0, 10));
                        ImGui::Text("Staging Area");
                        bool allDone = true;
                        if (ImGui::BeginTable("StagingTableBg", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("File Name");
                            ImGui::TableSetupColumn("Preview");
                            ImGui::TableSetupColumn("Colori (Prim/Sec)", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                            ImGui::TableSetupColumn("Fase");
                            ImGui::TableSetupColumn("Dinamica");
                            ImGui::TableSetupColumn("Force non foreground");
                            ImGui::TableSetupColumn("Comando Tastiera");
                            ImGui::TableSetupColumn("Azioni");
                            ImGui::TableHeadersRow();
                            
                            auto it = assetMgr.getStagingBackgrounds().begin();
                            while (it != assetMgr.getStagingBackgrounds().end()) {
                                auto& [bg, task] = *it;

                                // FIX BUG THUMBNAIL: Inizializza PRIMA di mostrare la riga per evitare un cache errato
                                if (task->isDone && bg.get_primary_color().empty() && !task->primaryColor.empty()) {
                                    bg.set_primary_color(task->primaryColor);
                                    bg.set_secondary_color(task->secondaryColor);
                                    bg.set_thumbnail_frame_index(task->bestFrameIndex);
                                    
                                    std::string cacheKey = bg.get_source() + "_color";
                                    if (thumbnailCache.find(cacheKey) != thumbnailCache.end()) {
                                        glDeleteTextures(1, &thumbnailCache[cacheKey]);
                                        thumbnailCache.erase(cacheKey);
                                    }
                                }

                                ImGui::TableNextRow();
                                
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%s", fs::path(task->filepath).filename().string().c_str());

                                ImGui::TableSetColumnIndex(1);
                                if(task->isDone) {
                                    if (bg.get_source().find('#') == 0) {
                                        ImGui::Text("Colore Fisso");
                                    } else {
                                        std::string cacheKey = bg.get_source() + "_color";
                                        auto cacheIt = thumbnailCache.find(cacheKey);
                                        GLuint tex = 0;
                                        if (cacheIt != thumbnailCache.end()) tex = cacheIt->second;
                                        else tex = getOrCreateThumbnail(bg.get_source(), bg.get_first_frame(), false);
                                        if(tex != 0) ImGui::Image((void*)(intptr_t)tex, ImVec2(80, 45));
                                    }
                                } else {
                                    ImGui::ProgressBar(task->progress, ImVec2(-1, 0));
                                    allDone = false;
                                }
                                
                                ImGui::TableSetColumnIndex(2);
                                if (task->isDone) {
                                    ImGui::PushID(task->filepath.c_str());
                                    std::string prim = bg.get_primary_color();
                                    std::string sec = bg.get_secondary_color();
                                    
                                    drawPaletteSquare(prim, sec, 45.0f);
                                    ImGui::SameLine();
                                    
                                    ImGui::BeginGroup();
                                    ImGui::PushItemWidth(110);
                                    renderColorDropdown("##P", prim);
                                    renderColorDropdown("##S", sec);
                                    ImGui::PopItemWidth();
                                    ImGui::EndGroup();
                                    
                                    bg.set_primary_color(prim);
                                    bg.set_secondary_color(sec);
                                    ImGui::PopID();
                                }
                                
                                ImGui::TableSetColumnIndex(3);
                                if (task->isDone) {
                                    ImGui::PushID(task->filepath.c_str());
                                    int ep = bg.get_event_phase();
                                    ImGui::PushItemWidth(100);
                                    if (ImGui::Combo("##Fase", &ep, "AMBIENT\0LOW\0MID\0HIGH\0")) bg.set_event_phase(static_cast<EventPhase>(ep));
                                    ImGui::PopItemWidth();
                                    ImGui::PopID();
                                }

                                ImGui::TableSetColumnIndex(4);
                                if (task->isDone) {
                                    ImGui::PushID(task->filepath.c_str());
                                    int sd = bg.get_song_dynamic();
                                    ImGui::PushItemWidth(100);
                                    if (ImGui::Combo("##Din", &sd, "VERSE\0BUILD\0CHORUS\0MANUAL\0")) bg.set_song_dynamic(static_cast<SongDynamic>(sd));
                                    ImGui::PopItemWidth();
                                    ImGui::PopID();
                                }
                                
                                ImGui::TableSetColumnIndex(5);
                                if (task->isDone) {
                                    ImGui::PushID(task->filepath.c_str());
                                    bool fnf = bg.get_force_no_foreground();
                                    if (ImGui::Checkbox("##fnf", &fnf)) bg.set_force_no_foreground(fnf);
                                    ImGui::PopID();
                                }

                                ImGui::TableSetColumnIndex(6);
                                if (task->isDone) {
                                    ImGui::PushID((task->filepath + "_btn").c_str());
                                    char keyBuf[2] = {0};
                                    if(!bg.get_key().empty()) keyBuf[0] = bg.get_key()[0];
                                    ImGui::PushItemWidth(30);
                                    if (ImGui::InputText("##Tasto", keyBuf, 2)) bg.set_key(std::string(keyBuf));
                                    ImGui::PopItemWidth();
                                    ImGui::PopID();
                                }

                                ImGui::TableSetColumnIndex(7);
                                if (ImGui::Button(("Rimuovi##" + task->filepath).c_str())) {
                                    it = assetMgr.getStagingBackgrounds().erase(it);
                                    continue;
                                }
                                ++it;
                            }
                            ImGui::EndTable();
                        }

                        if (allDone) {
                            if (ImGui::Button("Conferma e Salva in Libreria##Bg")) {
                                for (auto& [bg, task] : assetMgr.getStagingBackgrounds()) {
                                    std::string saveKey = bg.get_source();
                                    if(saveKey.find('#') != std::string::npos) saveKey = bg.get_key() + "_" + saveKey;
                                    assetMgr.getMutableAssets().get_mutable_backgrounds()[saveKey] = bg;
                                }
                                assetMgr.getStagingBackgrounds().clear();
                            }
                        } else {
                            ImGui::BeginDisabled();
                            ImGui::Button("Conferma e Salva in Libreria##Bg");
                            ImGui::EndDisabled();
                        }
                    }

                    ImGui::Dummy(ImVec2(0, 15));
                    ImGui::Text("Libreria Principale");
                    if (ImGui::BeginTable("MainLibraryTableBg", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                        ImGui::TableSetupColumn("File Name");
                        ImGui::TableSetupColumn("Preview");
                        ImGui::TableSetupColumn("Colori (Prim/Sec)", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                        ImGui::TableSetupColumn("Fase");
                        ImGui::TableSetupColumn("Dinamica");
                        ImGui::TableSetupColumn("Force non foreground");
                        ImGui::TableSetupColumn("Comando Tastiera");
                        ImGui::TableSetupColumn("Azioni");
                        ImGui::TableHeadersRow();
                        
                        std::string toRemove = "";
                        for (auto& [key, bg] : assetMgr.getMutableAssets().get_mutable_backgrounds()) {
                            ImGui::PushID(key.c_str());
                            ImGui::TableNextRow();
                            
                            ImGui::TableSetColumnIndex(0);
                            bool isDefault = (assetMgr.getMutableAssets().get_mutable_default_config().get_background() == key);
                            if (ImGui::RadioButton("##Def", isDefault)) assetMgr.getMutableAssets().get_mutable_default_config().set_background(key);
                            
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%s", key.c_str());

                            ImGui::TableSetColumnIndex(2);
                            if(bg.get_type() != WINDOW_CAPTURE && bg.get_source().find('#') != 0) {
                                std::string cacheKey = key + "_color";
                                auto cacheIt = thumbnailCache.find(cacheKey);
                                GLuint tex = 0;
                                if (cacheIt != thumbnailCache.end()) tex = cacheIt->second;
                                else tex = getOrCreateThumbnail(key, bg.get_first_frame(), false);
                                if(tex != 0) ImGui::Image((void*)(intptr_t)tex, ImVec2(80, 45));
                            }
                            
                            ImGui::TableSetColumnIndex(3);
                            std::string prim = bg.get_primary_color();
                            std::string sec = bg.get_secondary_color();
                            
                            drawPaletteSquare(prim, sec, 45.0f);
                            ImGui::SameLine();
                            
                            ImGui::BeginGroup();
                            ImGui::PushItemWidth(110);
                            renderColorDropdown("##Prim", prim);
                            renderColorDropdown("##Sec", sec);
                            ImGui::PopItemWidth();
                            ImGui::EndGroup();
                            
                            bg.set_primary_color(prim);
                            bg.set_secondary_color(sec);

                            ImGui::TableSetColumnIndex(4);
                            int ep = bg.get_event_phase();
                            ImGui::PushItemWidth(100);
                            if (ImGui::Combo("##Fase", &ep, "AMBIENT\0LOW\0MID\0HIGH\0")) bg.set_event_phase(static_cast<EventPhase>(ep));
                            ImGui::PopItemWidth();
                            
                            ImGui::TableSetColumnIndex(5);
                            int sd = bg.get_song_dynamic();
                            ImGui::PushItemWidth(100);
                            if (ImGui::Combo("##Dinamica", &sd, "VERSE\0BUILD\0CHORUS\0MANUAL\0")) bg.set_song_dynamic(static_cast<SongDynamic>(sd));
                            ImGui::PopItemWidth();
                            
                            ImGui::TableSetColumnIndex(6);
                            bool fnf = bg.get_force_no_foreground();
                            if (ImGui::Checkbox("##fnf", &fnf)) bg.set_force_no_foreground(fnf);
                            
                            ImGui::TableSetColumnIndex(7);
                            char keyBuf[2] = {0};
                            if(!bg.get_key().empty()) keyBuf[0] = bg.get_key()[0];
                            ImGui::PushItemWidth(30);
                            if (ImGui::InputText("##Tasto", keyBuf, 2)) bg.set_key(std::string(keyBuf));
                            ImGui::PopItemWidth();

                            ImGui::TableSetColumnIndex(8);
                            if (ImGui::Button("Rimuovi")) {
                                toRemove = key;
                            }
                            ImGui::PopID();
                        }
                        if (!toRemove.empty()) {
                            assetMgr.getMutableAssets().get_mutable_backgrounds().erase(toRemove);
                            if(assetMgr.getMutableAssets().get_mutable_default_config().get_background() == toRemove) {
                                assetMgr.getMutableAssets().get_mutable_default_config().set_background("");
                            }
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // TAB FOREGROUNDS
            ImGuiTabItemFlags fgFlags = (targetSettingsTab == "Foregrounds") ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Foregrounds", nullptr, fgFlags)) {
                if (targetSettingsTab == "Foregrounds") targetSettingsTab = "";

                if (ImGui::BeginChild("FgTabScrollArea", ImVec2(0, ImGui::GetContentRegionAvail().y - 50.0f), false)) {
                    if (ImGui::Button("Importa Foreground")) {
                        auto files = openMultipleFilesDialog("Seleziona Immagine/Video Foreground", {"png", "jpg", "mp4", "mov"});
                        for (const auto& file : files) {
                            Foreground newFg;
                            newFg.set_source(fs::path(file).filename().string()); 
                            assetMgr.getStagingForegrounds().push_back(newFg);
                        }
                    }
                    
                    if (!assetMgr.getStagingForegrounds().empty()) {
                        ImGui::Dummy(ImVec2(0, 10));
                        ImGui::Text("Staging Area");
                        if (ImGui::BeginTable("StagingTableFg", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("File Name");
                            ImGui::TableSetupColumn("Preview");
                            ImGui::TableSetupColumn("Colori (Prim/Sec)", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                            ImGui::TableSetupColumn("Scala");
                            ImGui::TableSetupColumn("Force original colors");
                            ImGui::TableSetupColumn("Comando Tastiera");
                            ImGui::TableSetupColumn("Azioni");
                            ImGui::TableHeadersRow();
                            
                            auto it = assetMgr.getStagingForegrounds().begin();
                            while (it != assetMgr.getStagingForegrounds().end()) {
                                Foreground& fg = *it;
                                ImGui::PushID(fg.get_source().c_str());
                                ImGui::TableNextRow();
                                
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%s", fg.get_source().c_str());

                                ImGui::TableSetColumnIndex(1);
                                cv::Mat frame = fg.get_first_frame();
                                if(!frame.empty()) {
                                    GLuint tex = getOrCreateThumbnail(fg.get_source() + "_stage", frame, false);
                                    if(tex != 0) ImGui::Image((void*)(intptr_t)tex, ImVec2(80, 45));
                                }
                                
                                ImGui::TableSetColumnIndex(2);
                                std::string prim = fg.get_primary_color();
                                std::string sec = fg.get_secondary_color();
                                
                                drawPaletteSquare(prim, sec, 45.0f);
                                ImGui::SameLine();
                                
                                ImGui::BeginGroup();
                                ImGui::PushItemWidth(110);
                                renderColorDropdown("##PrimS", prim);
                                renderColorDropdown("##SecS", sec);
                                ImGui::PopItemWidth();
                                ImGui::EndGroup();
                                
                                fg.set_primary_color(prim);
                                fg.set_secondary_color(sec);
                                
                                ImGui::TableSetColumnIndex(3);
                                double s = fg.get_scale();
                                ImGui::PushItemWidth(80);
                                if (ImGui::InputDouble("##ScaleS", &s, 0.1, 1.0)) fg.set_scale(s);
                                ImGui::PopItemWidth();
                                
                                ImGui::TableSetColumnIndex(4);
                                bool foc = fg.get_force_original_colors();
                                if (ImGui::Checkbox("##focS", &foc)) fg.set_force_original_colors(foc);

                                ImGui::TableSetColumnIndex(5);
                                char keyBuf[2] = {0};
                                if(!fg.get_key().empty()) keyBuf[0] = fg.get_key()[0];
                                ImGui::PushItemWidth(30);
                                if (ImGui::InputText("##TastoS", keyBuf, 2)) fg.set_key(std::string(keyBuf));
                                ImGui::PopItemWidth();

                                ImGui::TableSetColumnIndex(6);
                                if (ImGui::Button("Rimuovi")) {
                                    it = assetMgr.getStagingForegrounds().erase(it);
                                    ImGui::PopID();
                                    continue;
                                }
                                ImGui::PopID();
                                ++it;
                            }
                            ImGui::EndTable();
                        }

                        if (ImGui::Button("Conferma e Salva in Libreria##Fg")) {
                            for (auto& fg : assetMgr.getStagingForegrounds()) {
                                assetMgr.getMutableAssets().get_mutable_foregrounds()[fg.get_source()] = fg;
                            }
                            assetMgr.getStagingForegrounds().clear();
                        }
                    }

                    ImGui::Dummy(ImVec2(0, 15));
                    ImGui::Text("Libreria Principale");
                    if (ImGui::BeginTable("MainLibraryTableFg", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                        ImGui::TableSetupColumn("File Name");
                        ImGui::TableSetupColumn("Preview");
                        ImGui::TableSetupColumn("Colori (Prim/Sec)", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                        ImGui::TableSetupColumn("Scala");
                        ImGui::TableSetupColumn("Force original colors");
                        ImGui::TableSetupColumn("Comando Tastiera");
                        ImGui::TableSetupColumn("Azioni");
                        ImGui::TableHeadersRow();
                        
                        std::string toRemove = "";
                        for (auto& [key, fg] : assetMgr.getMutableAssets().get_mutable_foregrounds()) {
                            ImGui::PushID(key.c_str());
                            ImGui::TableNextRow();
                            
                            ImGui::TableSetColumnIndex(0);
                            bool isDefault = (assetMgr.getMutableAssets().get_mutable_default_config().get_foreground() == key);
                            if (ImGui::RadioButton("##DefF", isDefault)) assetMgr.getMutableAssets().get_mutable_default_config().set_foreground(key);
                            
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%s", key.c_str());

                            ImGui::TableSetColumnIndex(2);
                            std::string cacheKey = key + "_color";
                            auto cacheIt = thumbnailCache.find(cacheKey);
                            GLuint tex = 0;
                            if (cacheIt != thumbnailCache.end()) {
                                tex = cacheIt->second;
                            } else {
                                cv::Mat frame = fg.get_first_frame();
                                if(!frame.empty()) {
                                    tex = getOrCreateThumbnail(key, frame, false);
                                }
                            }
                            if(tex != 0) ImGui::Image((void*)(intptr_t)tex, ImVec2(80, 45));
                            
                            ImGui::TableSetColumnIndex(3);
                            std::string prim = fg.get_primary_color();
                            std::string sec = fg.get_secondary_color();
                            
                            drawPaletteSquare(prim, sec, 45.0f);
                            ImGui::SameLine();
                            
                            ImGui::BeginGroup();
                            ImGui::PushItemWidth(110);
                            renderColorDropdown("##PrimF", prim);
                            renderColorDropdown("##SecF", sec);
                            ImGui::PopItemWidth();
                            ImGui::EndGroup();
                            
                            fg.set_primary_color(prim);
                            fg.set_secondary_color(sec);
                            
                            ImGui::TableSetColumnIndex(4);
                            double s = fg.get_scale();
                            ImGui::PushItemWidth(80);
                            if (ImGui::InputDouble("##Scale", &s, 0.1, 1.0)) fg.set_scale(s);
                            ImGui::PopItemWidth();
                            
                            ImGui::TableSetColumnIndex(5);
                            bool foc = fg.get_force_original_colors();
                            if (ImGui::Checkbox("##foc", &foc)) fg.set_force_original_colors(foc);

                            ImGui::TableSetColumnIndex(6);
                            char keyBuf[2] = {0};
                            if(!fg.get_key().empty()) keyBuf[0] = fg.get_key()[0];
                            ImGui::PushItemWidth(30);
                            if (ImGui::InputText("##TastoF", keyBuf, 2)) fg.set_key(std::string(keyBuf));
                            ImGui::PopItemWidth();

                            ImGui::TableSetColumnIndex(7);
                            if (ImGui::Button("Rimuovi")) {
                                toRemove = key;
                            }
                            ImGui::PopID();
                        }
                        if (!toRemove.empty()) {
                            assetMgr.getMutableAssets().get_mutable_foregrounds().erase(toRemove);
                            if(assetMgr.getMutableAssets().get_mutable_default_config().get_foreground() == toRemove) {
                                assetMgr.getMutableAssets().get_mutable_default_config().set_foreground("");
                            }
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // TAB LIVE & SYSTEM SETTINGS
            ImGuiTabItemFlags lsFlags = (targetSettingsTab == "Live & System Settings") ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Live & System Settings", nullptr, lsFlags)) {
                if (targetSettingsTab == "Live & System Settings") targetSettingsTab = "";

                if (ImGui::BeginChild("SysTabScrollArea", ImVec2(0, ImGui::GetContentRegionAvail().y - 50.0f), false)) {
                    ImGui::Text("Impostazioni Generali di Sistema");
                    ImGui::Separator();
                    
                    if (ImGui::BeginTable("SysSettingsTable", 2)) {
                        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                        ImGui::TableSetupColumn("Input");
                        
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::Text("Auto Mode Start");
                        ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##autoStart", &state.isAutoMode);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::Text("Oscura Proiettore allo Start");
                        ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##blackout", &state.isBlackout);
                        
                        ImGui::EndTable();
                    }
                    
                    ImGui::Dummy(ImVec2(0, 15));
                    ImGui::Text("Configurazioni Avanzate (AppConfig)");
                    ImGui::Separator();
                    
                    if (ImGui::BeginTable("AdvSettingsTable", 2)) {
                        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
                        
                        auto drawSettingInt = [](const char* label, const char* id, int& val) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("%s", label);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(150.0f);
                            ImGui::InputInt(id, &val);
                        };

                        auto drawSettingDouble = [](const char* label, const char* id, double& val) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("%s", label);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(150.0f);
                            ImGui::InputDouble(id, &val);
                        };

                        int sRate = localConfig.audioSampleRate; drawSettingInt("Audio Sample Rate", "##sRate", sRate); localConfig.audioSampleRate = sRate;
                        int bufF = localConfig.audioBufferFrames; drawSettingInt("Audio Buffer Frames", "##bufF", bufF); localConfig.audioBufferFrames = bufF;
                        int debV = localConfig.debounceVerse; drawSettingInt("Debounce Verse", "##debV", debV); localConfig.debounceVerse = debV;
                        int debB = localConfig.debounceBuild; drawSettingInt("Debounce Build", "##debB", debB); localConfig.debounceBuild = debB;
                        int debC = localConfig.debounceChorus; drawSettingInt("Debounce Chorus", "##debC", debC); localConfig.debounceChorus = debC;
                        
                        int hSize = localConfig.knnHistorySize; drawSettingInt("KNN History Size", "##knnHist", hSize); localConfig.knnHistorySize = hSize;
                        int knnK = localConfig.knnKNeighbors; drawSettingInt("KNN K Neighbors", "##knnK", knnK); localConfig.knnKNeighbors = knnK;
                        
                        int strobo = strobeDurationMs; drawSettingInt("Strobe Duration (ms)", "##strobo", strobo); strobeDurationMs = strobo;
                        
                        int pLen = localConfig.phraseLength; drawSettingInt("Phrase Length (Ableton)", "##pLen", pLen); localConfig.phraseLength = pLen;
                        double defBpm = localConfig.default_bpm; drawSettingDouble("Default BPM", "##defBpm", defBpm); localConfig.default_bpm = defBpm;

                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        // ================= BOTTOM BUTTONS ================= //
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 250.0f, ImGui::GetWindowHeight() - 40.0f));
        
        if (ImGui::Button("Chiudi", ImVec2(100, 30))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Salva e Chiudi", ImVec2(130, 30))) {
            assetMgr.saveAssetsConfig();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End(); 
    ImGui::Render();
    
    int display_w, display_h;
    glfwGetFramebufferSize(uiWindow, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(uiWindow);
}

void GraphicsManager::showLive(const cv::Mat& frame, bool isBlackout) {
    if (!projectorWindow) return;

    glfwMakeContextCurrent(projectorWindow);
    int display_w, display_h;
    glfwGetFramebufferSize(projectorWindow, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);

    if (isBlackout) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    } else {
        if (frame.empty()) return;
        UpdateGLTexture(frame, projectorTexID);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, projectorTexID);
        glUniform1i(glGetUniformLocation(shaderProgram, "texMap"), 0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    
    glfwSwapBuffers(projectorWindow);
    glfwMakeContextCurrent(uiWindow);
}

bool GraphicsManager::shouldCloseUI() const {
    return glfwWindowShouldClose(uiWindow) || (projectorWindow && glfwWindowShouldClose(projectorWindow));
}

cv::Mat GraphicsManager::scaleToFit(const cv::Mat& src, int targetWidth, int targetHeight, const cv::Scalar& bgColor) {
    if (src.empty()) return cv::Mat(targetHeight, targetWidth, CV_8UC3, bgColor);
    int srcWidth = src.cols;
    int srcHeight = src.rows;
    if (srcWidth == 0 || srcHeight == 0) return cv::Mat(targetHeight, targetWidth, src.type(), bgColor);
    double scaleWidth = static_cast<double>(targetWidth) / srcWidth;
    double scaleFactor = scaleWidth;
    int newWidth = static_cast<int>(std::ceil(srcWidth * scaleFactor));
    int newHeight = static_cast<int>(std::ceil(srcHeight * scaleFactor));
    cv::Mat resizedFrame;
    cv::resize(src, resizedFrame, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);
    int xOffset = (newWidth - targetWidth) / 2;
    int yOffset = (newHeight - targetHeight) / 2;
    if (xOffset < 0) xOffset = 0;
    if (yOffset < 0) yOffset = 0;
    int safeWidth = std::min(targetWidth, resizedFrame.cols - xOffset);
    int safeHeight = std::min(targetHeight, resizedFrame.rows - yOffset);
    cv::Rect cropRegion(xOffset, yOffset, safeWidth, safeHeight);
    cv::Mat cropped = resizedFrame(cropRegion).clone();
    if (cropped.cols != targetWidth || cropped.rows != targetHeight) {
        cv::resize(cropped, cropped, cv::Size(targetWidth, targetHeight));
    }
    return cropped;
}

cv::Mat GraphicsManager::composeFrame(RuntimeState& state, AssetManager& assetMgr, double scale) {
    cv::Mat frame = state.activeBackground.get_next_frame();
    if (state.activeBackground.get_type() == WINDOW_CAPTURE) {
        if (frame.empty()) {
            state.captureFailCount++;
            if (state.captureFailCount < 3) {
                state.activeBackground.close();
                state.activeBackground.open();
                frame = state.activeBackground.get_next_frame();
            }
            if (frame.empty() && state.captureFailCount >= 3) {
                state.activeBackground = assetMgr.getDefaultBackground().value();
                state.activeForeground = assetMgr.getDefaultForeground().value();
                state.captureFailCount = 0;
                frame = state.activeBackground.get_next_frame();
            }
        } else {
            state.captureFailCount = 0;
        }
    }
    cv::Mat outputFrame = scaleToFit(frame, displayWidth, displayHeight);
    if (!state.activeBackground.get_force_no_foreground()) {
        outputFrame = assetMgr.blend(
            outputFrame,
            state.activeForeground.get_next_frame(),
            displayWidth,
            displayHeight,
            state.activeForeground.get_scale() * scale,
            state.activeBackground.get_foreground_color(),
            state.activeForeground.get_force_original_colors()
        );
    }
    return outputFrame;
}

cv::Mat GraphicsManager::composeCueFrame(RuntimeState& state, AssetManager& assetMgr, double scale) {
    if (!state.cueBackground.has_value()) {
        return cv::Mat(displayHeight, displayWidth, CV_8UC3, cv::Scalar(0, 0, 0));
    }

    Background& bgToUse = state.cueBackground.value();
    cv::Mat bgFrame;

    if (state.activeBackground.get_source() == bgToUse.get_source()) {
        bgFrame = state.activeBackground.get_current_frame();
    } else {
        bgFrame = bgToUse.get_next_frame();
    }

    if (bgFrame.empty()) {
        bgFrame = cv::Mat(displayHeight, displayWidth, CV_8UC3, cv::Scalar(0, 0, 0));
    }
    cv::Mat outputFrame = scaleToFit(bgFrame, displayWidth, displayHeight);

    if (!bgToUse.get_force_no_foreground() && state.cueForeground.has_value()) {
        Foreground& fgToUse = state.cueForeground.value();
        cv::Mat fgFrame;
        if (state.activeForeground.get_foreground_path() == fgToUse.get_foreground_path()) {
            fgFrame = state.activeForeground.get_current_frame();
        } else {
            fgFrame = fgToUse.get_next_frame();
        }
        outputFrame = assetMgr.blend(
            outputFrame,
            fgFrame,
            displayWidth,
            displayHeight,
            fgToUse.get_scale() * scale,
            bgToUse.get_foreground_color(),
            fgToUse.get_force_original_colors()
        );
    }
    return outputFrame;
}

cv::Mat GraphicsManager::applyStrobeEffect(cv::Mat composedFrame, RuntimeState& state, double bpm, bool isSpacePressed) {
    if (!isSpacePressed) {
        state.nextStrobeTime = std::nullopt;
        return composedFrame;
    }
    auto now = std::chrono::steady_clock::now();
    if (!state.nextStrobeTime.has_value()) {
        state.nextStrobeTime = now;
        state.strobeFrameToggle = true; 
    }
    if (now >= state.nextStrobeTime.value()) {
        state.strobeFrameToggle = !state.strobeFrameToggle;
        state.nextStrobeTime = now + std::chrono::milliseconds(strobeDurationMs);
    }
    if (state.strobeFrameToggle) {
        return cv::Mat(displayHeight, displayWidth, CV_8UC3, cv::Scalar(255, 255, 255));
    }
    return composedFrame;
}

double GraphicsManager::calculateBounceScale(RuntimeState& state, bool isBeat, double tempo, std::chrono::microseconds now) {
    if (tempo <= 0.0) {
        state.isAnimating = false;
        return 1.0;
    }
    double scale = 1.0;
    if (isBeat && state.isBounceActive) {
        state.isAnimating = true;
        state.animationStartTime = now;
    }
    if (state.isAnimating) {
        auto elapsed = now - state.animationStartTime;
        double beatDuration = (60.0 / tempo) * 1000000.0; 
        double progress = static_cast<double>(elapsed.count()) / beatDuration;
        if (progress < 1.0) {
            scale = 1.0 + 0.2 * std::sin((progress + 0.75) * (M_PI));
        } else {
            state.isAnimating = false;
        }
    }
    return scale;
}