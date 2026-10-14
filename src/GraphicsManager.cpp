#include "GraphicsManager.h"
#include "KNNClassifier.h"
#include <cmath>
#include <iostream>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;

// Riferimento all'istanza globale definita in main.cpp
extern KNNClassifier knnClassifier;

GraphicsManager::GraphicsManager(const AppConfig& config, const DisplayInfo& display) 
: windowName(config.windowName), displayWidth(display.width), displayHeight(display.height) 
{
    // --- 1. Setup Iniziale GLFW ---
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
    
    // --- 2. Finestra di Regia (UI) ---
    uiWindow = glfwCreateWindow(1280, 720, "Visual-Hive - Regia / Dashboard", NULL, NULL);
    if (!uiWindow) {
        glfwTerminate();
        std::cerr << "[ERROR] Impossibile creare la finestra di regia GLFW!" << std::endl;
        exit(1);
    }
    
    // --- 3. Finestra del Proiettore ---
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); 
    // Il 5° parametro (uiWindow) permette alle due finestre di condividere lo stesso contesto OpenGL
    projectorWindow = glfwCreateWindow(displayWidth, displayHeight, windowName.c_str(), NULL, uiWindow);
    if (!projectorWindow) {
        glfwTerminate();
        std::cerr << "[ERROR] Impossibile creare la finestra del proiettore GLFW!" << std::endl;
        exit(1);
    }
    glfwSetWindowPos(projectorWindow, display.x, display.y);
    
    // --- 4. Configurazione Contesto e V-Sync ---
    glfwMakeContextCurrent(uiWindow);
    glfwSwapInterval(1); // Abilita V-Sync
    
    // --- 5. Inizializza OpenGL Pipeline per il proiettore ---
    setupProjectorQuad();
    
    // --- 6. Setup Dear ImGui ---
    glfwMakeContextCurrent(uiWindow); // Riporta il contesto alla UI
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
    
    // PosX, PosY, TexU, TexV
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
    
    // ==========================================
    // SEZIONE SUPERIORE: PREVIEW CUE & LIVE
    // ==========================================
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
    
    // ==========================================
    // SEZIONE INFERIORE A DUE COLONNE (FASE 4)
    // ==========================================
    AudioMetrics audioData = audioAnalyzer.getMetrics();
    // Preserviamo i BPM passati da main.cpp qualora getMetrics() restituisca 0 o usiamo quelli del DSP
    float currentBpm = (audioData.bpm > 0.0f) ? audioData.bpm : static_cast<float>(bpm);
    
    ImGui::BeginChild("BottomSection", ImVec2(0, 0), false);
    ImGui::Columns(2, "BottomCols", false);
    
    // ------------------------------------------
    // COLONNA SINISTRA: Stato Runtime & Beat Effects
    // ------------------------------------------
    ImGui::BeginChild("RuntimeState", ImVec2(0, 0), true);
    
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "STATO RUNTIME");
    ImGui::Separator();
    
    // 1. Intensità Attuale con colore dedicato
    ImGui::Text("Intensità Attuale: ");
    ImGui::SameLine();
    if (state.currentEnergyTarget == LOW) {
        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "LOW");
    } else if (state.currentEnergyTarget == MID) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "MID");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "HIGH");
    }
    
    // 2. Asset Attivi
    ImGui::Text("Background: [ %s ]", state.activeBackground.get_key().c_str());
    if (!state.activeBackground.get_force_no_foreground()) {
        std::string fgPath = state.activeForeground.get_foreground_path();
        std::string fgFilename = fs::path(fgPath).filename().string();
        if (fgFilename.empty()) fgFilename = fgPath;
        ImGui::Text("Foreground: %s [ %s ]", fgFilename.c_str(), state.activeForeground.get_key().c_str());
    } else {
        ImGui::TextDisabled("Foreground: Disabilitato (Force No FG)");
    }
    
    // 3. Info Effetti (Strobo)
    bool strobeActive = ImGui::IsKeyDown(ImGuiKey_Space);
    ImGui::Text("Strobo (Spazio): ");
    ImGui::SameLine();
    if (strobeActive) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ON (50ms)");
    else ImGui::TextDisabled("OFF");
    
    ImGui::Spacing();
    if (ImGui::Button("Transizione CUE -> LIVE (INVIO)", ImVec2(-1.0f, 24.0f))) {
        state.transitionCueToLive();
    }
    
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "AUDIO INPUT & BEAT");
    ImGui::Separator();
    
    // 4. Selettore Interfaccia Audio + Refresh
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
    if (ImGui::Button("Refresh", ImVec2(55.0f, 0.0f))) {
        audioDevices = audioAnalyzer.getInputDevices();
    }
    
    // 5. Meter Principale Audio in Ingresso
    ImGui::Text("Main In:");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
    if (audioData.peak > 0.85f) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
    else if (audioData.peak > 0.6f) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.8f, 0.1f, 1.0f));
    ImGui::ProgressBar(audioData.peak, ImVec2(-1.0f, 12.0f), "");
    ImGui::PopStyleColor(audioData.peak > 0.6f ? 2 : 1);
    
    // 6. Indicatore BPM e Stato del Bounce
    ImGui::Text("BPM Attuali: %.1f", currentBpm);
    ImGui::Text("Bounce (B): ");
    ImGui::SameLine();
    if (state.isBounceActive && currentBpm > 0.0f) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "ON");
    } else if (state.isBounceActive && currentBpm <= 0.0f) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "OFF (No BPM)");
    } else {
        ImGui::TextDisabled("OFF");
    }

    ImGui::EndChild();
    ImGui::NextColumn();

    // ------------------------------------------
    // COLONNA DESTRA: Auto Mode & Machine Learning
    // ------------------------------------------
    ImGui::BeginChild("AutoModeML", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "AUTO MODE & MACHINE LEARNING");
    ImGui::Separator();
    
    // 1. Top Bar: Toggle Auto Mode + Dropdown Preset CSV + Bottone Nuovo Dataset
    ImGui::Checkbox("Auto Mode (M)", &state.isAutoMode);
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
    if (ImGui::Button("Nuovo Dataset")) {
        ImGui::OpenPopup("Training Dataset KNN");
    }
    
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    
    // 2. Risultati KNN (Confidenze)
    std::string predWinStr = (pred.winner == LOW) ? "LOW" : (pred.winner == MID) ? "MID" : "HIGH";
    ImVec4 winColor = (pred.winner == LOW) ? ImVec4(0.2f, 0.6f, 1.0f, 1.0f) :
                     (pred.winner == MID) ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                     
    ImGui::Text("CLASSE VINCENTE KNN:");
    ImGui::SameLine();
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextColored(winColor, "[ %s ]", predWinStr.c_str());
    ImGui::SetWindowFontScale(1.0f);
    
    char bufLow[32], bufMid[32], bufHigh[32];
    snprintf(bufLow, sizeof(bufLow), "LOW (%.0f%%)", pred.confLow * 100.0f);
    snprintf(bufMid, sizeof(bufMid), "MID (%.0f%%)", pred.confMid * 100.0f);
    snprintf(bufHigh, sizeof(bufHigh), "HIGH (%.0f%%)", pred.confHigh * 100.0f);
    
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
    ImGui::ProgressBar(pred.confLow, ImVec2(-1.0f, 12.0f), bufLow);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
    ImGui::ProgressBar(pred.confMid, ImVec2(-1.0f, 12.0f), bufMid);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    ImGui::ProgressBar(pred.confHigh, ImVec2(-1.0f, 12.0f), bufHigh);
    ImGui::PopStyleColor();
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.9f, 1.0f), "KNN ACTIVE FEATURES (DATASET)");
    ImGui::Separator();

    auto drawCompactMeter = [](const char* label, float val, const ImVec4& col) {
        ImGui::Text("%-12s", label); // Etichetta più larga
        ImGui::SameLine(90.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        ImGui::ProgressBar(std::clamp(val, 0.0f, 1.0f), ImVec2(-1.0f, 6.0f), "");
        ImGui::PopStyleColor();
    };

    auto drawBipolarMeter = [](const char* label, float val, const ImVec4& colPos, const ImVec4& colNeg) {
        ImGui::Text("%-12s", label);
        ImGui::SameLine(90.0f);
        // Val va da -1.0 a 1.0. 
        ImVec4 col = (val > 0) ? colPos : colNeg;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        // Spostiamo lo zero al centro
        float fraction = (val + 1.0f) * 0.5f; 
        ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1.0f, 6.0f), "");
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

    // ==========================================
    // POPUP MODALE: Training Dataset KNN
    // ==========================================
    if (ImGui::BeginPopupModal("Training Dataset KNN", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::map<EnergyLevel, int> counts = knnClassifier.getClassCounts();
        int totalSamples = counts[LOW] + counts[MID] + counts[HIGH];
        // Stima ~64 byte per riga CSV
        float estimatedKB = (totalSamples * 64.0f) / 1024.0f;
        ImGui::Text("Statistiche Dataset Temporaneo:");
        ImGui::BulletText("LOW:  %d samples", counts[LOW]);
        ImGui::BulletText("MID:  %d samples", counts[MID]);
        ImGui::BulletText("HIGH: %d samples", counts[HIGH]);
        ImGui::TextDisabled("Totale: %d samples (Dimensione stimata: %.2f KB)", totalSamples, estimatedKB);
        ImGui::Separator();
        ImGui::Text("Clicca o tieni premuto per catturare (~4 sample/sec):");
        static float captureTimer = 0.0f;
        captureTimer += ImGui::GetIO().DeltaTime;
        auto handleCaptureButton = [&](const char* btnLabel, EnergyLevel level, const ImVec4& btnCol) {
            ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
            bool clicked = ImGui::Button(btnLabel, ImVec2(125, 45));
            bool held = ImGui::IsItemActive(); // Permette di tenere premuto il bottone!
            ImGui::PopStyleColor();
            if (clicked || (held && captureTimer >= 0.25f)) {
                knnClassifier.addSample(audioData, level);
                captureTimer = 0.0f;
            }
        };
        handleCaptureButton("Registra LOW",  LOW,  ImVec4(0.15f, 0.45f, 0.80f, 1.0f));
        ImGui::SameLine();
        handleCaptureButton("Registra MID",  MID,  ImVec4(0.75f, 0.60f, 0.05f, 1.0f));
        ImGui::SameLine();
        handleCaptureButton("Registra HIGH", HIGH, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
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
    ImGui::EndChild(); // Fine AutoModeML
    ImGui::Columns(1);
    ImGui::EndChild(); // Fine BottomSection
    // ==========================================
    // CHIUSURA FRAME IMGUI E RENDER FINESTRA UI
    // ==========================================
    ImGui::End(); // Chiude "Dashboard"
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
    // Sposta il controllo OpenGL sulla finestra del proiettore
    glfwMakeContextCurrent(projectorWindow);
    // Aggiorna la memoria video
    UpdateGLTexture(frame, projectorTexID);
    // Renderizza il quad a tutto schermo usando il nostro shader
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
    // Manda a schermo
    glfwSwapBuffers(projectorWindow);
    // Riporta il controllo alla finestra Regia per il prossimo giro del loop
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
    // Protezione aggiuntiva qualora newHeight o newWidth siano minori del target
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
// 1. STROBE INDIPENDENTE DAI BPM (Frequenza fissa 50ms)
cv::Mat GraphicsManager::applyStrobeEffect(cv::Mat composedFrame, RuntimeState& state, double bpm, bool isSpacePressed) {
    if (!isSpacePressed) {
        state.nextStrobeTime = std::nullopt;
        return composedFrame;
    }
    auto now = std::chrono::steady_clock::now();
    if (!state.nextStrobeTime.has_value()) {
        state.nextStrobeTime = now;
        state.strobeFrameToggle = true; // Inizia acceso
    }
    if (now >= state.nextStrobeTime.value()) {
        state.strobeFrameToggle = !state.strobeFrameToggle;
        state.nextStrobeTime = now + std::chrono::milliseconds(50);
    }
    if (state.strobeFrameToggle) {
        return cv::Mat(displayHeight, displayWidth, CV_8UC3, cv::Scalar(255, 255, 255));
    }
    return composedFrame;
}
// 2. BOUNCE CON BLOCCO BPM E TRIGGER DA BTRACK
double GraphicsManager::calculateBounceScale(RuntimeState& state, bool isBeat, double tempo, std::chrono::microseconds now) {
    // Disattiva automaticamente se non ci sono BPM validi
    if (tempo <= 0.0) {
        state.isAnimating = false;
        return 1.0;
    }
    double scale = 1.0;
    // Trigger dell'animazione sul colpo di cassa/beat
    if (isBeat && state.isBounceActive) {
        state.isAnimating = true;
        state.animationStartTime = now;
    }
    // Esecuzione dell'animazione
    if (state.isAnimating) {
        auto elapsed = now - state.animationStartTime;
        double beatDuration = (60.0 / tempo) * 1000000.0; // Durata del beat in microsecondi
        double progress = static_cast<double>(elapsed.count()) / beatDuration;
        if (progress < 1.0) {
            scale = 1.0 + 0.2 * std::sin((progress + 0.75) * (M_PI));
        } else {
            state.isAnimating = false;
        }
    }
    return scale;
}