// GraphicsManager.cpp
#include "GraphicsManager.h"
#include <cmath>
#include <iostream>
#include <filesystem>
#include <ctime>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;

extern KNNClassifier knnClassifier;

GraphicsManager::GraphicsManager(const AppConfig& config, const DisplayInfo& display) 
: windowName(config.windowName), displayWidth(display.width), displayHeight(display.height), strobeDurationMs(config.strobeDurationMs)
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
    
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); 
    projectorWindow = glfwCreateWindow(displayWidth, displayHeight, windowName.c_str(), NULL, uiWindow);
    if (!projectorWindow) {
        glfwTerminate();
        std::cerr << "[ERROR] Impossibile creare la finestra del proiettore GLFW!" << std::endl;
        exit(1);
    }
    glfwSetWindowPos(projectorWindow, display.x, display.y);
    
    glfwMakeContextCurrent(uiWindow);
    glfwSwapInterval(1);
    
    setupProjectorQuad();
    
    glfwMakeContextCurrent(uiWindow); 
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
    
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(shaderProgram);
    
    glfwDestroyWindow(projectorWindow);
    glfwDestroyWindow(uiWindow);
    glfwTerminate();
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

void GraphicsManager::endDashboard(const cv::Mat& liveFrame, const cv::Mat& cueFrame, RuntimeState& state, double bpm, AudioAnalyzer& audioAnalyzer, KNNPrediction pred) {
    glfwMakeContextCurrent(uiWindow);
    
    UpdateGLTexture(liveFrame, liveTextureID);
    UpdateGLTexture(cueFrame, cueTextureID);
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    ImGui::Text("=== SISTEMA DI REGIA VJ ===");
    ImGui::Separator();
    
    float previewWidth = (ImGui::GetContentRegionAvail().x / 2.0f) - 10.0f;
    float previewHeight = previewWidth * ((float)displayHeight / (float)displayWidth);
    
    ImGui::BeginChild("Previews", ImVec2(0, previewHeight + 40), true);
    ImGui::Columns(2, "PreviewColumns", false);
    
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "CUE (PREVIEW)");
    if (cueTextureID != 0) ImGui::Image((void*)(intptr_t)cueTextureID, ImVec2(previewWidth, previewHeight));
    
    ImGui::NextColumn();
    
    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "LIVE (ON AIR)");
    if (liveTextureID != 0) ImGui::Image((void*)(intptr_t)liveTextureID, ImVec2(previewWidth, previewHeight));
    
    ImGui::Columns(1);
    ImGui::EndChild();
    
    AudioMetrics audioData = audioAnalyzer.getMetrics();
    float currentBpm = (audioData.bpm > 0.0f) ? audioData.bpm : static_cast<float>(bpm);
    bool audioActive = (audioData.peak > 0.001f);
    
    ImGui::BeginChild("BottomSection", ImVec2(0, 0), false);
    ImGui::Columns(2, "BottomCols", false);
    
    ImGui::BeginChild("RuntimeState", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "STATO RUNTIME");
    ImGui::Separator();
    
    ImGui::Text("Intensità Attuale: ");
    ImGui::SameLine();
    if (state.currentSongDynamicTarget == VERSE) ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "VERSE");
    else if (state.currentSongDynamicTarget == BUILD) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "BUILD");
    else ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "CHORUS");
    
    ImGui::Text("Background: [ %s ]", state.activeBackground.get_key().c_str());
    if (!state.activeBackground.get_force_no_foreground()) {
        std::string fgPath = state.activeForeground.get_foreground_path();
        std::string fgFilename = fs::path(fgPath).filename().string();
        if (fgFilename.empty()) fgFilename = fgPath;
        ImGui::Text("Foreground: %s [ %s ]", fgFilename.c_str(), state.activeForeground.get_key().c_str());
    } else {
        ImGui::TextDisabled("Foreground: Disabilitato (Force No FG)");
    }
    
    bool strobeActive = ImGui::IsKeyDown(ImGuiKey_Space);
    ImGui::Text("Strobo (Spazio): ");
    ImGui::SameLine();
    if (strobeActive) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ON (%dms)", strobeDurationMs);
    else ImGui::TextDisabled("OFF");
    
    ImGui::Spacing();
    if (ImGui::Button("Transizione CUE -> LIVE (INVIO)", ImVec2(-1.0f, 24.0f))) {
        state.transitionCueToLive();
    }
    
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "AUDIO INPUT & BEAT");
    ImGui::Separator();
    
    static std::vector<AudioDeviceInfo> audioDevices = audioAnalyzer.getInputDevices();
    static std::string currentDeviceName = "Seleziona Dispositivo...";
    if (currentDeviceName == "Seleziona Dispositivo..." && !audioDevices.empty()) {
        unsigned int activeId = audioAnalyzer.getCurrentDeviceId();
        for (const auto& dev : audioDevices) {
            if (dev.id == activeId) currentDeviceName = dev.name;
        }
    }
    
    ImGui::Text("Sorgente:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
    if (ImGui::BeginCombo("##AudioInterface", currentDeviceName.c_str())) {
        for (const auto& device : audioDevices) {
            bool isSelected = (currentDeviceName == device.name);
            if (ImGui::Selectable(device.name.c_str(), isSelected)) {
                currentDeviceName = device.name;
                audioAnalyzer.stop();
                audioAnalyzer.start(device.id);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(55.0f, 0.0f))) audioDevices = audioAnalyzer.getInputDevices();
    
    static float peakHoldL = 0.0f;
    static float peakHoldR = 0.0f;

    if (audioData.peakL > peakHoldL) peakHoldL = audioData.peakL;
    else peakHoldL *= 0.999f;

    if (audioData.peakR > peakHoldR) peakHoldR = audioData.peakR;
    else peakHoldR *= 0.999f;

    float maxHold = std::max(peakHoldL, peakHoldR);
    float maxHoldDb = (maxHold > 0.0001f) ? 20.0f * std::log10(maxHold) : -60.0f;
    
    char avgBuf[32];
    if (maxHoldDb <= -59.0f) snprintf(avgBuf, sizeof(avgBuf), "-INF");
    else snprintf(avgBuf, sizeof(avgBuf), "%+.1f dB", maxHoldDb);

    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Main In (dBFS):");
    ImGui::SameLine();
    
    ImVec4 avgColor;
    if (maxHoldDb >= -0.1f) avgColor = ImVec4(1.0f, 0.1f, 0.1f, 1.0f); 
    else if (maxHoldDb > -6.0f) avgColor = ImVec4(1.0f, 0.8f, 0.1f, 1.0f);
    else avgColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); 
    
    ImGui::TextColored(avgColor, "[ %s ]", avgBuf);

    auto drawDbMeter = [](const char* label, float linearPeak, float linearHold) {
        float db = (linearPeak > 0.0001f) ? 20.0f * std::log10(linearPeak) : -60.0f;
        float holdDb = (linearHold > 0.0001f) ? 20.0f * std::log10(linearHold) : -60.0f;
        
        float minDb = -48.0f; 
        float maxDb = 0.0f;
        float fraction = std::clamp((db - minDb) / (maxDb - minDb), 0.0f, 1.0f);
        float holdFraction = std::clamp((holdDb - minDb) / (maxDb - minDb), 0.0f, 1.0f);

        ImVec4 color;
        if (db >= -0.1f) color = ImVec4(1.0f, 0.1f, 0.1f, 1.0f); 
        else if (db > -6.0f) color = ImVec4(1.0f, 0.8f, 0.1f, 1.0f);
        else color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); 

        ImGui::Text("%s", label);
        ImGui::SameLine(25.0f);
        
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 8.0f), ""); 
        ImGui::PopStyleColor();

        if (holdFraction > 0.0f) {
            ImVec2 p_min = ImGui::GetItemRectMin();
            ImVec2 p_max = ImGui::GetItemRectMax();
            float tick_x = p_min.x + (p_max.x - p_min.x) * holdFraction;
            if (tick_x > p_max.x - 1.0f) tick_x = p_max.x - 1.0f;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(tick_x, p_min.y), 
                ImVec2(tick_x, p_max.y), 
                IM_COL32(255, 255, 255, 255), 
                2.0f
            );
        }
    };

    drawDbMeter("L", audioData.peakL, peakHoldL);
    drawDbMeter("R", audioData.peakR, peakHoldR);
    
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    
    ImGui::Text("BPM Attuali: %.1f", currentBpm);
    ImGui::Text("Bounce (B): ");
    ImGui::SameLine();
    if (state.isBounceActive && currentBpm > 0.0f) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ON");
    else if (state.isBounceActive && currentBpm <= 0.0f) ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "OFF (No BPM)");
    else ImGui::TextDisabled("OFF");

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "REGISTRAZIONE AUDIO (RAW .WAV)");
    ImGui::Separator();
    
    if (audioAnalyzer.isRecordingActive()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Stop Registrazione", ImVec2(-1, 30))) {
            audioAnalyzer.stopRecording();
        }
        ImGui::PopStyleColor();
        
        ImGui::Text("File: %s", audioAnalyzer.getCurrentRecordFilename().c_str());
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Tempo: %s (in corso...)", audioAnalyzer.getRecordingTimeFormatted().c_str());
    } else {
        if (ImGui::Button("Avvia Registrazione", ImVec2(-1, 30))) {
            auto t = std::time(nullptr);
            auto tm = *std::localtime(&t);
            char timeBuf[64];
            std::strftime(timeBuf, sizeof(timeBuf), "rec_%Y%m%d_%H%M%S", &tm);
            audioAnalyzer.startRecording(timeBuf);
        }
    }

    ImGui::EndChild();
    ImGui::NextColumn();

    ImGui::BeginChild("AutoModeML", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "AUTO MODE & MACHINE LEARNING");
    ImGui::Separator();
    
    if (!audioActive) ImGui::BeginDisabled();
    ImGui::Checkbox("Auto Mode (M)", &state.isAutoMode);
    if (!audioActive) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), " (Disabilitato: Muto)");
    }

    ImGui::SameLine();
    static std::vector<std::string> csvPresets = {"knn_dataset.csv"};
    static std::string currentPreset = "knn_dataset.csv";
    static bool presetsScanned = false;
    auto scanCsvPresets = [&]() {
        csvPresets.clear();
        for (const auto& entry : fs::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".csv") {
                csvPresets.push_back(entry.path().filename().string());
            }
        }
        if (csvPresets.empty()) csvPresets.push_back("knn_dataset.csv");
    };
    if (!presetsScanned) { scanCsvPresets(); presetsScanned = true; }
    
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##PresetCSV", currentPreset.c_str())) {
        for (const auto& preset : csvPresets) {
            bool isSelected = (currentPreset == preset);
            if (ImGui::Selectable(preset.c_str(), isSelected)) {
                currentPreset = preset;
                knnClassifier.loadDataset(currentPreset);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Nuovo Dataset")) ImGui::OpenPopup("Training Dataset KNN");
    
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    
    std::string predWinStr = (pred.winner == VERSE) ? "VERSE" : (pred.winner == BUILD) ? "BUILD" : (pred.winner == CHORUS) ? "CHORUS" : "MANUAL";
    ImVec4 winColor = (pred.winner == VERSE) ? ImVec4(0.2f, 0.6f, 1.0f, 1.0f) :
                     (pred.winner == BUILD) ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    if (pred.winner == MANUAL) winColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                     
    ImGui::Text("CLASSE VINCENTE KNN:");
    ImGui::SameLine();
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextColored(winColor, "[ %s ]", predWinStr.c_str());
    ImGui::SetWindowFontScale(1.0f);
    
    char bufVerse[32], bufBuild[32], bufChorus[32];
    snprintf(bufVerse, sizeof(bufVerse), "VERSE (%.0f%%)", pred.confVerse * 100.0f);
    snprintf(bufBuild, sizeof(bufBuild), "BUILD (%.0f%%)", pred.confBuild * 100.0f);
    snprintf(bufChorus, sizeof(bufChorus), "CHORUS (%.0f%%)", pred.confChorus * 100.0f);
    
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
    ImGui::ProgressBar(pred.confVerse, ImVec2(-1.0f, 12.0f), bufVerse);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
    ImGui::ProgressBar(pred.confBuild, ImVec2(-1.0f, 12.0f), bufBuild);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    ImGui::ProgressBar(pred.confChorus, ImVec2(-1.0f, 12.0f), bufChorus);
    ImGui::PopStyleColor();
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.9f, 1.0f), "KNN ACTIVE FEATURES (DATASET)");
    ImGui::Separator();

    auto drawCompactMeter = [](const char* label, float val, const ImVec4& col) {
        ImGui::Text("%-12s", label); 
        ImGui::SameLine(90.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(std::clamp(val, 0.0f, 1.0f), ImVec2(-1.0f, 6.0f), "");
        ImGui::PopStyleColor();
    };

    ImGui::Columns(2, "KNNFeaturesGrid", false);
    
    drawCompactMeter("RMS (Volume)", audioData.rms, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); 
    drawCompactMeter("SubBass",      audioData.bandSubBass, ImVec4(0.6f, 0.2f, 0.8f, 1.0f)); 
    drawCompactMeter("Bass",         audioData.bandBass, ImVec4(0.8f, 0.3f, 0.3f, 1.0f)); 
    
    ImGui::NextColumn();
    
    drawCompactMeter("HighMid",      audioData.bandHighMid, ImVec4(0.2f, 0.8f, 1.0f, 1.0f)); 
    drawCompactMeter("Spec Flux",    audioData.spectralFlux, ImVec4(1.0f, 0.2f, 0.8f, 1.0f)); 

    ImGui::Columns(1);

    if (ImGui::BeginPopupModal("Training Dataset KNN", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::map<SongDynamic, int> counts = knnClassifier.getClassCounts();
        int totalSamples = counts[VERSE] + counts[BUILD] + counts[CHORUS];
        float estimatedKB = (totalSamples * 64.0f) / 1024.0f;
        ImGui::Text("Statistiche Dataset Temporaneo:");
        ImGui::BulletText("VERSE:  %d samples", counts[VERSE]);
        ImGui::BulletText("BUILD:  %d samples", counts[BUILD]);
        ImGui::BulletText("CHORUS: %d samples", counts[CHORUS]);
        ImGui::TextDisabled("Totale: %d samples (Dimensione stimata: %.2f KB)", totalSamples, estimatedKB);
        ImGui::Separator();
        ImGui::Text("Clicca o tieni premuto per catturare (~4 sample/sec):");
        static float captureTimer = 0.0f;
        captureTimer += ImGui::GetIO().DeltaTime;
        auto handleCaptureButton = [&](const char* btnLabel, SongDynamic level, const ImVec4& btnCol) {
            ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
            bool clicked = ImGui::Button(btnLabel, ImVec2(125, 45));
            bool held = ImGui::IsItemActive(); 
            ImGui::PopStyleColor();
            if (clicked || (held && captureTimer >= 0.25f)) {
                knnClassifier.addSample(audioData, level);
                captureTimer = 0.0f;
            }
        };
        handleCaptureButton("Registra VERSE",  VERSE,  ImVec4(0.15f, 0.45f, 0.80f, 1.0f));
        ImGui::SameLine();
        handleCaptureButton("Registra BUILD",  BUILD,  ImVec4(0.75f, 0.60f, 0.05f, 1.0f));
        ImGui::SameLine();
        handleCaptureButton("Registra CHORUS", CHORUS, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
        ImGui::Separator();
        if (ImGui::Button("Salva Dataset", ImVec2(190, 30))) {
            knnClassifier.commitTemporaryDataset();
            knnClassifier.saveDataset(currentPreset);
            scanCsvPresets();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Annulla", ImVec2(190, 30))) {
            knnClassifier.clearTemporaryDataset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild(); 
    ImGui::Columns(1);
    ImGui::EndChild(); 

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

void GraphicsManager::showLive(const cv::Mat& frame) {
    if (frame.empty()) return;
    glfwMakeContextCurrent(projectorWindow);
    UpdateGLTexture(frame, projectorTexID);
    int display_w, display_h;
    glfwGetFramebufferSize(projectorWindow, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, projectorTexID);
    glUniform1i(glGetUniformLocation(shaderProgram, "texMap"), 0);
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glfwSwapBuffers(projectorWindow);
    glfwMakeContextCurrent(uiWindow);
}

bool GraphicsManager::shouldCloseUI() const {
    return glfwWindowShouldClose(uiWindow) || glfwWindowShouldClose(projectorWindow);
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
    Background& bgToUse = state.cueBackground.has_value() ? state.cueBackground.value() : state.activeBackground;
    cv::Mat bgFrame = bgToUse.get_next_frame();
    if (bgFrame.empty()) {
        bgFrame = cv::Mat(displayHeight, displayWidth, CV_8UC3, cv::Scalar(50, 50, 50));
    }
    cv::Mat outputFrame = scaleToFit(bgFrame, displayWidth, displayHeight);
    if (!bgToUse.get_force_no_foreground()) {
        Foreground& fgToUse = state.cueForeground.has_value() ? state.cueForeground.value() : state.activeForeground;
        cv::Mat fgFrame = fgToUse.get_next_frame();
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