#include "GraphicsManager.h"
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

void GraphicsManager::endDashboard(const cv::Mat& liveFrame, const cv::Mat& cueFrame, RuntimeState& state, double bpm, AudioAnalyzer& audioAnalyzer) {
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

    // --- SEZIONE INFERIORE A DUE COLONNE ---
    ImGui::BeginChild("BottomSection", ImVec2(0, 0), false);
    ImGui::Columns(2, "BottomCols", false);

    // COLONNA 1: Telemetria Originale
    ImGui::BeginChild("Telemetry", ImVec2(0, 0), true);
    ImGui::Text("Telemetria e Stato Sistema:");
    ImGui::Separator();
    ImGui::Text("Ableton Link: %s (BPM: %.1f)", state.linkEnabled ? "ON" : "OFF", bpm);
    ImGui::Text("Auto Mode: %s", state.isAutoMode ? "ON" : "OFF");
    ImGui::Text("Bounce Effect: %s", state.isBounceActive ? "ON" : "OFF");
    
    if (ImGui::Button("Transizione CUE -> LIVE (INVIO)")) {
        state.transitionCueToLive();
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    // COLONNA 2: Modulo Audio DSP (COMPLETO E AGGIORNATO)
    ImGui::BeginChild("AudioDSP", ImVec2(0, 0), true);
    ImGui::Text("Modulo Audio DSP:");
    ImGui::Separator();
    
    AudioMetrics audioData = audioAnalyzer.getMetrics();
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
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    if (ImGui::BeginCombo("##AudioInput", currentDeviceName.c_str())) {
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
    if (ImGui::Button("Refresh")) {
        audioDevices = audioAnalyzer.getInputDevices();
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // 1. LIVE INPUT METER
    ImGui::Text("Live Input Level (Peak):");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); 
    if (audioData.peak > 0.85f) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.1f, 0.1f, 1.0f)); 
    else if (audioData.peak > 0.6f) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.8f, 0.1f, 1.0f)); 
    ImGui::ProgressBar(audioData.peak, ImVec2(-1.0f, 12.0f), "");
    ImGui::PopStyleColor(audioData.peak > 0.6f ? 2 : 1);

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    
    // 2. BPM E DELTA INTENSITA'
    ImGui::Text("BPM Stimati (Sub): %.1f", audioData.bpm);
    ImGui::Text("Delta Intensità (Trend): ");
    ImGui::SameLine();
    if (audioData.deltaIntensity > 0.05f) { // Soglia aumentata
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "IN CRESCENDO (+%.2f)", audioData.deltaIntensity);
    } else if (audioData.deltaIntensity < -0.05f) { // Soglia aumentata
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "IN DISCESA (%.2f)", audioData.deltaIntensity);
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "STABILE (%.2f)", audioData.deltaIntensity);
    }
    
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // 3. CORE METRICS
    ImGui::Text("Energia RMS");
    ImGui::ProgressBar(std::min(audioData.rms * 3.0f, 1.0f), ImVec2(-1.0f, 8.0f), ""); 
    
    ImGui::Text("Spectral Flux (FFT)");
    ImGui::ProgressBar(audioData.spectralFlux, ImVec2(-1.0f, 8.0f), "");

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // 4. ANALISI MULTIBANDA (Feature normalizzate per il KNN)
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "--- KNN Audio Features (Normalized) ---");

    ImGui::Text("Sub-Bass (20-60 Hz)");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.6f, 0.2f, 0.8f, 1.0f)); 
    ImGui::ProgressBar(audioData.bandSubBass, ImVec2(-1.0f, 6.0f), "");
    ImGui::PopStyleColor();

    ImGui::Text("Bass (60-250 Hz)");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.3f, 0.3f, 1.0f)); 
    ImGui::ProgressBar(audioData.bandBass, ImVec2(-1.0f, 6.0f), "");
    ImGui::PopStyleColor();

    ImGui::Text("Low-Mid (250-500 Hz)");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.6f, 0.2f, 1.0f)); 
    ImGui::ProgressBar(audioData.bandLowMid, ImVec2(-1.0f, 6.0f), "");
    ImGui::PopStyleColor();

    ImGui::Text("Mid (500-2000 Hz)");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.8f, 0.3f, 1.0f)); 
    ImGui::ProgressBar(audioData.bandMid, ImVec2(-1.0f, 6.0f), "");
    ImGui::PopStyleColor();
    
    ImGui::Text("High-Mid (2k-4k Hz)");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.7f, 0.8f, 1.0f)); 
    ImGui::ProgressBar(audioData.bandHighMid, ImVec2(-1.0f, 6.0f), "");
    ImGui::PopStyleColor();

    ImGui::Text("High (4k+ Hz)");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.5f, 1.0f, 1.0f)); 
    ImGui::ProgressBar(audioData.bandHigh, ImVec2(-1.0f, 6.0f), "");
    ImGui::PopStyleColor();

    ImGui::EndChild(); // Fine AudioDSP

    ImGui::Columns(1);
    ImGui::EndChild(); // Fine BottomSection
    
    ImGui::End();

    // Rendering finale
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
    double scaleHeight = static_cast<double>(targetHeight) / srcHeight;
    double scaleFactor = scaleWidth;

    int newWidth = static_cast<int>(std::ceil(srcWidth * scaleFactor));
    int newHeight = static_cast<int>(std::ceil(srcHeight * scaleFactor));
    
    cv::Mat resizedFrame;
    cv::resize(src, resizedFrame, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_LINEAR);

    int xOffset = (newWidth - targetWidth) / 2;
    int yOffset = (newHeight - targetHeight) / 2;
    
    if (xOffset < 0) xOffset = 0;
    if (yOffset < 0) yOffset = 0;

    cv::Rect cropRegion(xOffset, yOffset, targetWidth, targetHeight);    
    return resizedFrame(cropRegion).clone();
}

double GraphicsManager::calculateBounceScale(RuntimeState& state, double currentBeat, double tempo, std::chrono::microseconds now) {
    double scale = 1.0;

    if (std::floor(currentBeat) > state.lastBeat) {
        state.lastBeat = std::floor(currentBeat);
        if (state.isBounceActive) {
            state.isAnimating = true;
            state.animationStartTime = now;
        }
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
            state.activeForeground.get_force_original_colors() // <-- NUOVO
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
    }

    if (now >= state.nextStrobeTime.value()) {
        state.strobeFrameToggle = !state.strobeFrameToggle;
        double duration = 2000.0 / bpm; 
        state.nextStrobeTime = now + std::chrono::milliseconds(static_cast<long long>(duration));
    }

    if (state.strobeFrameToggle) {
        return cv::Mat(displayHeight, displayWidth, CV_8UC3, cv::Scalar(255, 255, 255));
    }

    return composedFrame;
}