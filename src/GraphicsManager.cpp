// GraphicsManager.cpp
#include "GraphicsManager.h"
#include <cmath>
#include <iostream>
#include <filesystem>
#include <ctime>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;

extern KNNClassifier knnClassifier;

GraphicsManager::GraphicsManager(const AppConfig& config, const DisplayInfo& display) 
: windowName(config.windowName), displayWidth(display.width), displayHeight(display.height), strobeDurationMs(config.strobeDurationMs), projectorWindow(nullptr)
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
    cv::resize(firstFrame, resized, cv::Size(160, 90));
    
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
    cv::cvtColor(mat, rgbaMat, cv::COLOR_BGR2RGBA);
    
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

void GraphicsManager::beginDashboard() {
    glfwMakeContextCurrent(uiWindow);
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GraphicsManager::endDashboard(const cv::Mat& liveFrame, const cv::Mat& cueFrame, RuntimeState& state, double bpm, AudioAnalyzer& audioAnalyzer, KNNPrediction pred, AssetManager& assetMgr) {
    glfwMakeContextCurrent(uiWindow);
    
    UpdateGLTexture(liveFrame, liveTextureID);
    UpdateGLTexture(cueFrame, cueTextureID);
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    ImVec2 contentAvail = ImGui::GetContentRegionAvail();
    float topHeight = contentAvail.y * 0.40f;
    float bottomHeight = contentAvail.y - topHeight - 10.0f;
    
    // ================= TOP UI ================= //
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
    float currentBpm = (audioData.bpm > 0.0f) ? audioData.bpm : static_cast<float>(bpm);
    
    ImGui::BeginChild("MonitoringAuto", ImVec2(0, 0), true);
    
    // ================= SEZIONE OUTPUT & PROIEZIONE ================= //
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
    ImGui::Checkbox("BLACKOUT (Oscura Proiettore)", &state.isBlackout);
    ImGui::PopStyleColor();
    
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    
    // ================= SEZIONE AUDIO INPUT & MONITORING ================= //
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
    
    ImGui::SameLine();
    if (audioAnalyzer.isRecordingActive()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Stop Rec")) audioAnalyzer.stopRecording();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[%s]", audioAnalyzer.getRecordingTimeFormatted().c_str());
    } else {
        if (ImGui::Button("Rec .WAV")) {
            auto t = std::time(nullptr);
            auto tm = *std::localtime(&t);
            char timeBuf[64];
            std::strftime(timeBuf, sizeof(timeBuf), "rec_%Y%m%d_%H%M%S", &tm);
            audioAnalyzer.startRecording(timeBuf);
        }
    }

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
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "CONFIGURAZIONE LIVE");
    ImGui::Separator();
    
    ImGui::Text("Fase Evento:");
    int ep = static_cast<int>(state.currentEventPhase);
    if (ImGui::RadioButton("Ambient", &ep, AMBIENT)) state.currentEventPhase = AMBIENT; ImGui::SameLine();
    if (ImGui::RadioButton("Low", &ep, LOW)) state.currentEventPhase = LOW; ImGui::SameLine();
    if (ImGui::RadioButton("Mid", &ep, MID)) state.currentEventPhase = MID; ImGui::SameLine();
    if (ImGui::RadioButton("High", &ep, HIGH)) state.currentEventPhase = HIGH;
    
    ImGui::Text("Colori Attivi:");
    const char* colors[] = {"Bianco", "Nero", "Rosso", "Verde", "Blu", "Viola", "Giallo", "Arancione"};
    for (int i = 0; i < 8; i++) {
        bool active = std::find(state.activePalettes.begin(), state.activePalettes.end(), colors[i]) != state.activePalettes.end();
        if (ImGui::Checkbox(colors[i], &active)) {
            if (active) state.activePalettes.push_back(colors[i]);
            else state.activePalettes.erase(std::remove(state.activePalettes.begin(), state.activePalettes.end(), colors[i]), state.activePalettes.end());
        }
        if ((i + 1) % 4 != 0) ImGui::SameLine();
    }
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "AUTO MODE & MACHINE LEARNING");
    ImGui::Separator();
    ImGui::Checkbox("Auto Mode (M)", &state.isAutoMode);
    ImGui::EndChild();
    
    ImGui::EndChild(); // TopArea
    
    ImGui::Columns(1);
    
    // ================= BOTTOM UI: LIBRERIE DECK E TESTO ================= //
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::Separator();
    
    ImGui::BeginChild("BottomArea", ImVec2(0, bottomHeight), false);
    ImGui::Columns(2, "BottomColumns", false);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.70f);
    
    // SEZIONE BACKGROUND (Sinistra - 70%)
    ImGui::BeginChild("BackgroundDeckArea", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "BACKGROUND DECK");
    
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
                            ImGui::Text("Applicazione Target: %s", bg.get_target_app_name().c_str());
                            ImGui::Text("Finestra Target: %s", bg.get_target_window_name().c_str());
                            
                            if (ImGui::Button("Invia in CUE", ImVec2(200, 30))) {
                                state.switchCueBackground(bg);
                            }
                            ImGui::Dummy(ImVec2(0, 5));
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
                            std::string pal = bg.get_palette_color().empty() ? "Nessuna Palette" : bg.get_palette_color();
                            if (dyn == VERSE || dyn == MANUAL) verseMap[pal].push_back(key);
                            else if (dyn == BUILD) buildMap[pal].push_back(key);
                            else if (dyn == CHORUS) chorusMap[pal].push_back(key);
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
                                if (cacheIt != thumbnailCache.end()) {
                                    tex = cacheIt->second;
                                } else {
                                    tex = getOrCreateThumbnail(key, bg.get_first_frame(), !enabled);
                                }
                                
                                ImGui::PushID(key.c_str());
                                if (itemCount > 0 && itemCount % 2 != 0) ImGui::SameLine();
                                
                                if (ImGui::ImageButton((void*)(intptr_t)tex, ImVec2(160, 90))) {
                                    state.switchCueBackground(bg);
                                }
                                
                                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                                    bg.set_is_enabled_global(!enabled);
                                }
                                
                                if (!enabled) {
                                    ImVec2 p_min = ImGui::GetItemRectMin();
                                    ImVec2 p_max = ImGui::GetItemRectMax();
                                    ImVec2 center = ImVec2((p_min.x + p_max.x) * 0.5f, (p_min.y + p_max.y) * 0.5f);
                                    float radius = 30.0f;
                                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                                    drawList->AddCircle(center, radius, IM_COL32(255, 0, 0, 200), 0, 4.0f);
                                    drawList->AddLine(ImVec2(center.x - radius*0.7f, center.y - radius*0.7f), 
                                                      ImVec2(center.x + radius*0.7f, center.y + radius*0.7f), 
                                                      IM_COL32(255, 0, 0, 200), 4.0f);
                                }
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
    
    // SEZIONE FOREGROUND & TEXT (Destra - 30%)
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
    
    auto& mutableFgs = assetMgr.getMutableAssets().get_mutable_foregrounds();
    
    int itemCount = 0;
    for (auto& [key, fg] : mutableFgs) {
        bool enabled = fg.get_is_enabled_global();
        
        std::string cacheKey = key + (!enabled ? "_gray" : "_color");
        GLuint tex = 0;
        auto cacheIt = thumbnailCache.find(cacheKey);
        
        if (cacheIt != thumbnailCache.end()) {
            tex = cacheIt->second;
        } else {
            cv::Mat firstFrame = fg.get_first_frame();
            if (firstFrame.empty()) continue; 
            tex = getOrCreateThumbnail(key, firstFrame, !enabled);
        }
        
        ImGui::PushID(key.c_str());
        if (itemCount > 0 && itemCount % 2 != 0) ImGui::SameLine();
        
        if (ImGui::ImageButton((void*)(intptr_t)tex, ImVec2(160, 90))) {
            state.switchCueForeground(fg);
        }
        
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            fg.set_is_enabled_global(!enabled);
        }
        
        if (!enabled) {
            ImVec2 p_min = ImGui::GetItemRectMin();
            ImVec2 p_max = ImGui::GetItemRectMax();
            ImVec2 center = ImVec2((p_min.x + p_max.x) * 0.5f, (p_min.y + p_max.y) * 0.5f);
            float radius = 25.0f;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddCircle(center, radius, IM_COL32(255, 0, 0, 200), 0, 4.0f);
            drawList->AddLine(ImVec2(center.x - radius*0.7f, center.y - radius*0.7f), 
                              ImVec2(center.x + radius*0.7f, center.y + radius*0.7f), 
                              IM_COL32(255, 0, 0, 200), 4.0f);
        }
        ImGui::PopID();
        itemCount++;
    }
    ImGui::EndChild();
    
    ImGui::EndChild(); // BottomArea
    
    ImGui::Columns(1);
    
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

    // Se LIVE e CUE condividono la stessa sorgente, CUE riutilizza il frame corrente senza farlo avanzare
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