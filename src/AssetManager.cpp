#include "AssetManager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include "PlatformSpecificCode.h"

namespace fs = std::filesystem;

cv::Scalar toScalar(const std::string& hexColor);

fs::path Background::backgroundsPath;
fs::path Foreground::foregroundsPath;

NLOHMANN_JSON_SERIALIZE_ENUM(EventPhase, {
    {AMBIENT, "ambient"}, {LOW, "low"}, {MID, "mid"}, {HIGH, "high"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(SongDynamic, {
    {VERSE, "verse"}, {BUILD, "build"}, {CHORUS, "chorus"}, {MANUAL, "manual"}
})

void from_json(const nlohmann::json& j, Background& b) {
    if(j.contains("foreground_color")) j.at("foreground_color").get_to(b.foregroundColor);
    if(j.contains("key")) j.at("key").get_to(b.key);
    b.eventPhase = j.value("event_phase", AMBIENT); 
    b.songDynamic = j.value("song_dynamic", VERSE);
    b.primaryColor = j.value("primary_color", "");
    b.secondaryColor = j.value("secondary_color", "");
    b.thumbnailFrameIndex = j.value("thumbnail_frame_index", 0);
    b.isEnabledGlobal = j.value("is_enabled_global", true);
    b.forceNoForeground = j.value("force_no_foreground", false);
    
    if (j.contains("app_name")) b.set_target_app_name(j.at("app_name").get<std::string>());
    if (j.contains("window_name")) b.set_target_window_name(j.at("window_name").get<std::string>());
}

void to_json(nlohmann::json& j, const Background& b) {
    j = nlohmann::json{
        {"foreground_color", b.foregroundColor}, 
        {"key", b.key},
        {"event_phase", b.eventPhase},
        {"song_dynamic", b.songDynamic},
        {"primary_color", b.primaryColor},
        {"secondary_color", b.secondaryColor},
        {"thumbnail_frame_index", b.thumbnailFrameIndex},
        {"is_enabled_global", b.isEnabledGlobal},
        {"force_no_foreground", b.forceNoForeground}
    };
    if (b.get_type() == WINDOW_CAPTURE) {
        j["app_name"] = b.get_target_app_name();
        j["window_name"] = b.get_target_window_name();
    }
}

void to_json(nlohmann::json& j, const Foreground& f) {
    j = nlohmann::json{
        {"scale", f.scale},
        {"key", f.key},
        {"force_original_colors", f.forceOriginalColors},
        {"primary_color", f.primaryColor},
        {"secondary_color", f.secondaryColor},
        {"is_enabled_global", f.isEnabledGlobal},
        {"custom_text", f.customText},
        {"font_size", f.fontSize}
    };
}

void from_json(const nlohmann::json& j, Foreground& f) {
    if(j.contains("scale")) j.at("scale").get_to(f.scale);
    if(j.contains("key")) j.at("key").get_to(f.key);
    if(j.contains("force_original_colors")) j.at("force_original_colors").get_to(f.forceOriginalColors);
    f.primaryColor = j.value("primary_color", "");
    f.secondaryColor = j.value("secondary_color", "");
    f.isEnabledGlobal = j.value("is_enabled_global", true);
    f.customText = j.value("custom_text", "");
    f.fontSize = j.value("font_size", 32);
}

void to_json(nlohmann::json& j, const Default& d) {
    j = nlohmann::json{{"background", d.background}, {"foreground", d.foreground}};
}

void from_json(const nlohmann::json& j, Default& d) {
    if(j.contains("background")) j.at("background").get_to(d.background);
    if(j.contains("foreground")) j.at("foreground").get_to(d.foreground);
}

void from_json(const nlohmann::json& j, AssetsConfig& ac) {
    if (j.contains("backgrounds")) j.at("backgrounds").get_to(ac.get_mutable_backgrounds());
    if (j.contains("window_captures")) {
        for (auto& [key, value] : j["window_captures"].items()) {
            Background bg = value.get<Background>();
            bg.set_source(WINDOW_CAPTURE, key); 
            ac.get_mutable_backgrounds()[key] = bg;
        }
    }
    if(j.contains("default")) j.at("default").get_to(ac.get_mutable_default_config());
    if(j.contains("foregrounds")) j.at("foregrounds").get_to(ac.get_mutable_foregrounds());
}

void to_json(nlohmann::json& j, const AssetsConfig& ac) {
    nlohmann::json standardBackgrounds = nlohmann::json::object();
    nlohmann::json windowCaptures = nlohmann::json::object();
    for (const auto& [key, bg] : ac.get_backgrounds()) {
        if (bg.get_type() == WINDOW_CAPTURE) windowCaptures[key] = bg;
        else standardBackgrounds[key] = bg;
    }
    j = nlohmann::json{
        {"backgrounds", standardBackgrounds},
        {"window_captures", windowCaptures},
        {"default", ac.get_default_config()},
        {"foregrounds", ac.get_foregrounds()}
    };
}

const std::optional<cv::Scalar> Background::get_background_color() const {
    return (type == SOLID_COLOR) ? std::optional<cv::Scalar>(toScalar(asset_source)) : std::nullopt;
}

const std::optional<std::string> Background::get_background_path() const {
    return (type == VIDEO_LOOP) ? std::optional<std::string>(backgroundsPath / asset_source) : std::nullopt;
}

const cv::Mat Background::get_first_frame() {
    if (this->type == VIDEO_LOOP) {
        cv::VideoCapture cap(this->get_background_path().value());
        if (!cap.isOpened()) return cv::Mat::zeros(1080, 1920, CV_8UC3);
        
        cap.set(cv::CAP_PROP_POS_FRAMES, thumbnailFrameIndex); 
        cv::Mat frame;
        cap >> frame;
        cap.release();
        return frame.empty() ? cv::Mat::zeros(1080, 1920, CV_8UC3) : frame;
    }
    else if (this->type == WINDOW_CAPTURE) return cv::Mat(); 
    else return this->get_solid_color_frame(1920, 1080, this->get_background_color().value()); 
}

bool Background::open() {
    if (this->type == VIDEO_LOOP) {
        this->video_loop_cap.open(this->get_background_path().value());
        this->lastVideoFrameTime = 0; 
        this->currentVideoFrame.release();
        return this->video_loop_cap.isOpened();
    }
    else if (this->type == WINDOW_CAPTURE) {
        if (this->captureWindowId != -1) {
            startWindowCapture(this->captureWindowId);
            return true;
        }
        return false;
    }
    else {
        this->solid_color_img = this->get_solid_color_frame(1920, 1080, this->get_background_color().value());
        return true;
    }
}

void Background::close() {
    if (this->type == VIDEO_LOOP) this->video_loop_cap.release();
    else if (this->type == WINDOW_CAPTURE) stopWindowCapture();
}

cv::Mat Background::get_next_frame() {
    if (this->type == VIDEO_LOOP) {
        long long currentTick = cv::getTickCount();
        if (lastVideoFrameTime == 0 || currentVideoFrame.empty()) {
            this->video_loop_cap >> currentVideoFrame;
            if (currentVideoFrame.empty()) {
                this->video_loop_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                this->video_loop_cap >> currentVideoFrame;
            }
            lastVideoFrameTime = currentTick;
            return currentVideoFrame.clone();
        }
        double elapsedTime_ms = (currentTick - lastVideoFrameTime) * 1000.0 / cv::getTickFrequency();
        double target_fps = this->get_fps();
        if (target_fps <= 0) target_fps = 30.0;
        double frameDuration_ms = 1000.0 / target_fps;

        if (elapsedTime_ms >= frameDuration_ms) {
            this->video_loop_cap >> currentVideoFrame;
            if (currentVideoFrame.empty()) {
                this->video_loop_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                this->video_loop_cap >> currentVideoFrame;
            }
            lastVideoFrameTime = currentTick;
        }
        return currentVideoFrame.clone();
    }
    else if (this->type == WINDOW_CAPTURE) {
        cv::Mat frame = getLatestWindowFrame();
        if (frame.empty()) {
            currentVideoFrame = this->get_solid_color_frame(1920, 1080, cv::Scalar(0,0,0));
            return currentVideoFrame;
        }
        cv::Mat gray, mask;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY);
        cv::Rect contentBox = cv::boundingRect(mask);
        if(contentBox.width > 0 && contentBox.height > 0)
            currentVideoFrame = frame(contentBox).clone();
        else
            currentVideoFrame = frame.clone();
        return currentVideoFrame;
    }
    else return this->solid_color_img;
}

cv::Mat Background::get_current_frame() const {
    if (this->type == VIDEO_LOOP || this->type == WINDOW_CAPTURE) {
        if (!currentVideoFrame.empty()) return currentVideoFrame.clone();
        return this->get_solid_color_frame(1920, 1080, cv::Scalar(0, 0, 0));
    }
    return this->solid_color_img;
}

const cv::Mat Background::get_solid_color_frame(int width, int height, const cv::Scalar& color) const {
    return cv::Mat(height, width, CV_8UC3, color);
}

double Background::get_fps() {
    return (type == VIDEO_LOOP) ? this->video_loop_cap.get(cv::CAP_PROP_FPS) : 30.0;
}

const std::string Foreground::get_foreground_path() const {
    return foregroundsPath / this->asset_source;
}

const cv::Mat Foreground::get_first_frame() const {
    if (this->isRamData || this->asset_source == "RAW_RAM_DATA") return cv::Mat(); 
    return cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
}

bool Foreground::open() {
    if (this->isRamData) return !this->data.empty();
    this->data = cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
    return !this->data.empty();
}

void Foreground::close() {}

cv::Mat Foreground::get_next_frame() {
    if (!this->isRamData && this->data.empty()) this->data = cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
    return this->data;
}   

cv::Mat Foreground::get_current_frame() const { return this->data; }

AssetManager::AssetManager(const AppConfig& config) : appConfig(config) {
    std::ifstream jsonFile(config.assetsConfigFile);
    if (!jsonFile.is_open()) {
        std::cout << "[INFO] File di configurazione " << config.assetsConfigFile 
                  << " non trovato. Inizializzazione di un AssetManager vuoto e creazione del file..." << std::endl;
        assets = AssetsConfig(); // Crea la struttura vuota
        saveAssetsConfig();      // Salva fisicamente il JSON sul disco
    } else {
        nlohmann::json data;
        jsonFile >> data; 
        assets = data.get<AssetsConfig>();
    }
}

AssetManager::~AssetManager() {
    for (auto& t : analysisThreads) {
        if (t.joinable()) t.join();
    }
}

std::shared_ptr<VideoAnalysisTask> AssetManager::analyzeVideoAsync(const std::string& filepath) {
    auto task = std::make_shared<VideoAnalysisTask>();
    task->filepath = filepath;
    analysisThreads.emplace_back([task]() {
        cv::VideoCapture cap(task->filepath);
        if (!cap.isOpened()) { task->isDone = true; return; }

        int totalFrames = cap.get(cv::CAP_PROP_FRAME_COUNT);
        int frameStep = 10;
        double maxStdDev = -1.0;
        int bestFrame = 0;

        std::map<std::string, int> globalColorCounts;

        struct NamedColor { std::string name; cv::Vec3b bgr; };
        std::vector<NamedColor> baseColors = {
            {"Bianco", cv::Vec3b(255, 255, 255)}, {"Nero", cv::Vec3b(0, 0, 0)},
            {"Rosso", cv::Vec3b(0, 0, 255)}, {"Verde", cv::Vec3b(0, 255, 0)},
            {"Blu", cv::Vec3b(255, 0, 0)}, {"Viola", cv::Vec3b(128, 0, 128)},
            {"Giallo", cv::Vec3b(0, 255, 255)}, {"Arancione", cv::Vec3b(0, 165, 255)}
        };
        for(const auto& bc : baseColors) globalColorCounts[bc.name] = 0;

        cv::Mat frame;
        int frameIdx = 0;
        while (cap.read(frame)) {
            if (frameIdx % frameStep == 0 && !frame.empty()) {
                cv::Mat mean, stddev;
                cv::meanStdDev(frame, mean, stddev);
                double currentStdDev = stddev.at<double>(0) + stddev.at<double>(1) + stddev.at<double>(2);
                if (currentStdDev > maxStdDev) {
                    maxStdDev = currentStdDev;
                    bestFrame = frameIdx;
                }
                
                cv::Mat resized;
                cv::resize(frame, resized, cv::Size(32, 32), 0, 0, cv::INTER_NEAREST);
                for (int y = 0; y < resized.rows; ++y) {
                    for (int x = 0; x < resized.cols; ++x) {
                        cv::Vec3b pixel = resized.at<cv::Vec3b>(y, x);
                        int minDist = std::numeric_limits<int>::max();
                        std::string closestName = "";
                        for (const auto& bc : baseColors) {
                            int db = pixel[0] - bc.bgr[0], dg = pixel[1] - bc.bgr[1], dr = pixel[2] - bc.bgr[2];
                            int distSq = db*db + dg*dg + dr*dr;
                            if (distSq < minDist) { minDist = distSq; closestName = bc.name; }
                        }
                        globalColorCounts[closestName]++;
                    }
                }
                
                if(totalFrames > 0)
                    task->progress = static_cast<float>(frameIdx) / totalFrames;
            }
            frameIdx++;
        }
        
        std::vector<std::pair<std::string, int>> sortedCounts(globalColorCounts.begin(), globalColorCounts.end());
        std::sort(sortedCounts.begin(), sortedCounts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        if(sortedCounts.size() > 0) task->primaryColor = sortedCounts[0].first;
        if(sortedCounts.size() > 1) task->secondaryColor = sortedCounts[1].first;

        task->bestFrameIndex = bestFrame;
        task->isDone = true;
        cap.release();
    });
    return task;
}

void AssetManager::initializeAssets() {
    loadAssetsIntoMemory();
}

void AssetManager::saveAssetsConfig() {
    std::ofstream jsonFile(appConfig.assetsConfigFile);
    if (jsonFile.is_open()) {
        nlohmann::json data = assets;
        jsonFile << data.dump(4);
    }
}

cv::Scalar toScalar(const std::string& hexColor) {
    if (hexColor.length() != 7 || hexColor[0] != '#') return cv::Scalar(0,0,0);
    int r = std::stoi(hexColor.substr(1, 2), nullptr, 16);
    int g = std::stoi(hexColor.substr(3, 2), nullptr, 16);
    int b = std::stoi(hexColor.substr(5, 2), nullptr, 16);
    return cv::Scalar(b, g, r);
}

void AssetManager::loadAssetsIntoMemory() {
    Background::backgroundsPath = fs::path(appConfig.assetsDir) / "backgrounds";
    Foreground::foregroundsPath = fs::path(appConfig.assetsDir) / "foregrounds";
    std::vector<WindowInfo> openWindows;
    bool fetchedWindows = false;

    for (auto& [key, value] : this->assets.get_mutable_backgrounds()) {
        if (value.get_type() == WINDOW_CAPTURE) {
            if (!fetchedWindows) { openWindows = getOpenWindows(); fetchedWindows = true; }
            for (const auto& win : openWindows) {
                if (win.ownerName == value.get_target_app_name()) {
                    value.set_capture_window_id(win.id); break;
                }
            }
        }
        else if (fs::exists(Background::backgroundsPath / key)) value.set_source(VIDEO_LOOP, key);
        else if (key.rfind("#", 0) == 0) value.set_source(SOLID_COLOR, key);
    }

    for (auto& [key, value] : this->assets.get_mutable_foregrounds()) {
        value.set_source(key);
    }
}

cv::Mat AssetManager::blend(const cv::Mat& background, const cv::Mat& foregroundAsset, int screenWidth, int screenHeight, double foregroundScalePercent, cv::Scalar foregroundColor, bool forceOriginalColor) {
    if (background.empty() || foregroundAsset.empty()) return background.clone();

    cv::Mat result = background.clone();
    int newWidth = static_cast<int>(screenWidth * (foregroundScalePercent / 100.0));
    int newHeight = static_cast<int>(foregroundAsset.rows * ((double)newWidth / foregroundAsset.cols));
    if (newWidth <= 0 || newHeight <= 0) return result;

    cv::Mat resizedFg;
    cv::resize(foregroundAsset, resizedFg, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_AREA);
    if (resizedFg.channels() != 4) cv::cvtColor(resizedFg, resizedFg, cv::COLOR_BGR2BGRA);

    int xOffset = (screenWidth - newWidth) / 2;
    int yOffset = (screenHeight - newHeight) / 2;
    int xStart = std::max(0, xOffset);
    int yStart = std::max(0, yOffset);
    int xEnd = std::min(screenWidth, xOffset + newWidth);
    int yEnd = std::min(screenHeight, yOffset + newHeight);

    int fgXStart = xStart - xOffset;
    int fgYStart = yStart - yOffset;
    int roiWidth = xEnd - xStart;
    int roiHeight = yEnd - yStart;

    if (roiWidth <= 0 || roiHeight <= 0) return result;

    cv::Mat bgRegion = result(cv::Rect(xStart, yStart, roiWidth, roiHeight));
    cv::Mat fgRegion = resizedFg(cv::Rect(fgXStart, fgYStart, roiWidth, roiHeight));

    int targetB = foregroundColor[0];
    int targetG = foregroundColor[1];
    int targetR = foregroundColor[2];

    for (int y = 0; y < roiHeight; ++y) {
        uchar* bgPtr = bgRegion.ptr<uchar>(y);
        const uchar* fgPtr = fgRegion.ptr<uchar>(y);
        for (int x = 0; x < roiWidth; ++x) {
            int bgIdx = x * 3;
            int fgIdx = x * 4;
            uchar alpha = fgPtr[fgIdx + 3];
            if (alpha == 0) continue;
            
            if (alpha == 255) {
                bgPtr[bgIdx]     = forceOriginalColor ? fgPtr[fgIdx] : targetB;
                bgPtr[bgIdx + 1] = forceOriginalColor ? fgPtr[fgIdx + 1] : targetG;
                bgPtr[bgIdx + 2] = forceOriginalColor ? fgPtr[fgIdx + 2] : targetR;
            } else {
                int invAlpha = 255 - alpha;
                bgPtr[bgIdx]     = ((forceOriginalColor ? fgPtr[fgIdx] : targetB) * alpha + bgPtr[bgIdx] * invAlpha) / 255;
                bgPtr[bgIdx + 1] = ((forceOriginalColor ? fgPtr[fgIdx + 1] : targetG) * alpha + bgPtr[bgIdx + 1] * invAlpha) / 255;
                bgPtr[bgIdx + 2] = ((forceOriginalColor ? fgPtr[fgIdx + 2] : targetR) * alpha + bgPtr[bgIdx + 2] * invAlpha) / 255;
            }
        }
    }
    return result;
}

std::optional<Background> AssetManager::getDefaultBackground() {
    std::string defBgKey = this->assets.get_default_config().get_background();
    const auto& bgs = this->assets.get_backgrounds();
    
    if (!defBgKey.empty() && bgs.find(defBgKey) != bgs.end()) {
        return bgs.at(defBgKey);
    }

    Background blackBg;
    blackBg.set_key("#000000");
    blackBg.set_source(SOLID_COLOR, "#000000");
    blackBg.set_primary_color("Nero");
    blackBg.set_secondary_color("Nero");
    blackBg.set_foreground_color(cv::Scalar(255, 255, 255));
    return blackBg;
}

std::optional<Foreground> AssetManager::getDefaultForeground() {
    std::string defFgKey = this->assets.get_default_config().get_foreground();
    const auto& fgs = this->assets.get_foregrounds();

    if (!defFgKey.empty() && fgs.find(defFgKey) != fgs.end()) {
        return fgs.at(defFgKey);
    }

    return Foreground();
}

std::optional<Background> AssetManager::getBackroundByPressedKey(char pressed_key) {
    std::string key(1, pressed_key);
    for (auto b : this->assets.get_backgrounds()) if (b.second.get_key() == key) return b.second;
    return std::nullopt;
}

std::optional<Foreground> AssetManager::getForegroundByPressedKey(char pressed_key) {
    std::string key(1, pressed_key);
    for (auto b : this->assets.get_foregrounds()) if (b.second.get_key() == key) return b.second;
    return std::nullopt;
}

char AssetManager::displayAndGetKey(const std::string& windowName, const cv::Mat asset) {
    cv::imshow(windowName, asset);
    return cv::waitKey(0);
}

std::pair<std::string, std::string> AssetManager::analyzeFramePalette(const cv::Mat& frame) {
    if (frame.empty()) return {"", ""};

    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(64, 64), 0, 0, cv::INTER_NEAREST);
    if (resized.channels() == 4) cv::cvtColor(resized, resized, cv::COLOR_BGRA2BGR);
    else if (resized.channels() == 1) cv::cvtColor(resized, resized, cv::COLOR_GRAY2BGR);

    struct NamedColor { std::string name; cv::Vec3b bgr; };
    std::vector<NamedColor> baseColors = {
        {"Bianco", cv::Vec3b(255, 255, 255)}, {"Nero", cv::Vec3b(0, 0, 0)},
        {"Rosso", cv::Vec3b(0, 0, 255)}, {"Verde", cv::Vec3b(0, 255, 0)},
        {"Blu", cv::Vec3b(255, 0, 0)}, {"Viola", cv::Vec3b(128, 0, 128)},
        {"Giallo", cv::Vec3b(0, 255, 255)}, {"Arancione", cv::Vec3b(0, 165, 255)}
    };

    std::map<std::string, int> counts;
    for (const auto& c : baseColors) counts[c.name] = 0;

    for (int y = 0; y < resized.rows; ++y) {
        for (int x = 0; x < resized.cols; ++x) {
            cv::Vec3b pixel = resized.at<cv::Vec3b>(y, x);
            int minDist = std::numeric_limits<int>::max();
            std::string closestName = "";
            for (const auto& bc : baseColors) {
                int db = pixel[0] - bc.bgr[0], dg = pixel[1] - bc.bgr[1], dr = pixel[2] - bc.bgr[2];
                int distSq = db*db + dg*dg + dr*dr;
                if (distSq < minDist) { minDist = distSq; closestName = bc.name; }
            }
            counts[closestName]++;
        }
    }

    std::vector<std::pair<std::string, int>> sortedCounts(counts.begin(), counts.end());
    std::sort(sortedCounts.begin(), sortedCounts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    int threshold = (64 * 64) / 10;
    if (sortedCounts.size() > 1 && sortedCounts[1].second >= threshold) {
        return {sortedCounts[0].first, sortedCounts[1].first};
    } else if (sortedCounts.size() > 0) {
        return {sortedCounts[0].first, ""};
    }
    return {"", ""};
}

Foreground AssetManager::createTextForeground(const std::string& text, int fontSize) {
    cv::Mat frame = cv::Mat::zeros(1080, 1920, CV_8UC3);
    int baseline = 0;
    double fontScale = static_cast<double>(fontSize);
    int thickness = fontSize > 4 ? 4 : 2;
    cv::Size textSize = cv::getTextSize(text, cv::FONT_HERSHEY_COMPLEX, fontScale, thickness, &baseline);
    cv::Point textOrg((frame.cols - textSize.width) / 2, (frame.rows + textSize.height) / 2);
    cv::putText(frame, text, textOrg, cv::FONT_HERSHEY_COMPLEX, fontScale, cv::Scalar(255, 255, 255), thickness);
    
    Foreground fg;
    fg.set_key("TEXT_" + text);
    fg.set_scale(1.0);
    fg.set_raw_data(frame);
    fg.set_force_original_colors(true);
    fg.set_is_ram_data(true);
    return fg;
}