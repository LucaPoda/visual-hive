// AssetManager.cpp
#include "AssetManager.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>
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
    {AMBIENT, "ambient"},
    {LOW, "low"},
    {MID, "mid"},
    {HIGH, "high"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(SongDynamic, {
    {VERSE, "verse"},
    {BUILD, "build"},
    {CHORUS, "chorus"},
    {MANUAL, "manual"}
})

void from_json(const nlohmann::json& j, Background& b) {
    j.at("foreground_color").get_to(b.foregroundColor);
    j.at("key").get_to(b.key);
    b.eventPhase = j.value("event_phase", AMBIENT); 
    b.songDynamic = j.value("song_dynamic", VERSE);
    b.paletteColor = j.value("palette_color", "");
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
        {"palette_color", b.paletteColor},
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
        {"palette_color", f.paletteColor},
        {"is_enabled_global", f.isEnabledGlobal},
        {"custom_text", f.customText},
        {"font_size", f.fontSize}
    };
}

void from_json(const nlohmann::json& j, Foreground& f) {
    j.at("scale").get_to(f.scale);
    j.at("key").get_to(f.key);
    j.at("force_original_colors").get_to(f.forceOriginalColors);
    f.paletteColor = j.value("palette_color", "");
    f.isEnabledGlobal = j.value("is_enabled_global", true);
    f.customText = j.value("custom_text", "");
    f.fontSize = j.value("font_size", 32);
}

void to_json(nlohmann::json& j, const Default& d) {
    j = nlohmann::json{{"background", d.background}, {"foreground", d.foreground}};
}

void from_json(const nlohmann::json& j, Default& d) {
    j.at("background").get_to(d.background);
    j.at("foreground").get_to(d.foreground);
}

void from_json(const nlohmann::json& j, AssetsConfig& ac) {
    if (j.contains("backgrounds")) {
        j.at("backgrounds").get_to(ac.get_mutable_backgrounds());
    }
    
    if (j.contains("window_captures")) {
        for (auto& [key, value] : j["window_captures"].items()) {
            Background bg = value.get<Background>();
            bg.set_source(WINDOW_CAPTURE, key); 
            ac.get_mutable_backgrounds()[key] = bg;
        }
    }
    
    j.at("default").get_to(ac.get_mutable_default_config());
    j.at("foregrounds").get_to(ac.get_mutable_foregrounds());
}

void to_json(nlohmann::json& j, const AssetsConfig& ac) {
    nlohmann::json standardBackgrounds;
    nlohmann::json windowCaptures;
    
    for (const auto& [key, bg] : ac.get_backgrounds()) {
        if (bg.get_type() == WINDOW_CAPTURE) {
            windowCaptures[key] = bg;
        } else {
            standardBackgrounds[key] = bg;
        }
    }

    j = nlohmann::json{
        {"backgrounds", standardBackgrounds},
        {"window_captures", windowCaptures},
        {"default", ac.get_default_config()},
        {"foregrounds", ac.get_foregrounds()}
    };
}

const std::optional<cv::Scalar> Background::get_background_color() const {
    if (type == SOLID_COLOR) {
        return toScalar(asset_source);
    }
    else {
        return std::nullopt;
    }
}

const std::optional<std::string> Background::get_background_path() const {
    if (type == VIDEO_LOOP) {
        return backgroundsPath / asset_source;
    }
    else {
        return std::nullopt;
    }
}

const cv::Mat Background::get_first_frame() const {
    if (this->type == VIDEO_LOOP) {
        cv::VideoCapture cap(this->get_background_path().value());

        if (!cap.isOpened()) {
            std::cerr << "Error: Could not open video file\n";
            exit(-1);
        }

        cv::Mat frame;
        cap >> frame;
        cap.release();
        if (!frame.empty()) {
            return frame;
        }
        else {
            std::cerr << "Error: First frame is empty\n";
            exit(-1);
        }
    }
    else {
        return this->get_solid_color_frame(1920, 1080, this->get_background_color().value()); 
    }
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
        } else {
            std::cerr << "[ERROR] Window Capture requested but no window ID set.\n";
            return false;
        }
    }
    else {
        this->solid_color_img = this->get_solid_color_frame(1920, 1080, this->get_background_color().value());
        return true;
    }
}

void Background::close() {
    if (this->type == VIDEO_LOOP) {
        this->video_loop_cap.release();
    }
    else if (this->type == WINDOW_CAPTURE) {
        stopWindowCapture();
    }
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
                if (currentVideoFrame.empty()) {
                    std::cerr << "Error: Could not loop video. Exiting.\n";
                    exit(1);
                }
            }
            lastVideoFrameTime = currentTick;
        }

        return currentVideoFrame.clone();
    }
    else if (this->type == WINDOW_CAPTURE) {
        cv::Mat frame = getLatestWindowFrame();

        if (frame.empty()) {
            return this->get_solid_color_frame(1920, 1080, cv::Scalar(0,0,0));
        }
        
        cv::Mat gray, mask;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY);
        cv::Rect contentBox = cv::boundingRect(mask);
        
        return frame(contentBox);
    }
    else {
        return this->solid_color_img;
    }
}

const cv::Mat Background::get_solid_color_frame(int width, int height, const cv::Scalar& color) const {
    cv::Mat solidColorFrame(height, width, CV_8UC3, color);
    return solidColorFrame;
}

double Background::get_fps() {
    if (type == VIDEO_LOOP) {
        return this->video_loop_cap.get(cv::CAP_PROP_FPS);
    }
    else {
        return 30.0; 
    }
}

const std::string Foreground::get_foreground_path() const {
    return foregroundsPath / this->asset_source;
}

const cv::Mat Foreground::get_first_frame() const {
    return cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
}

bool Foreground::open() {
    this->data = cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
    
    if (this->data.empty()) {
        return false;
    }
    return true;
}
void Foreground::close() {
}

cv::Mat Foreground::get_next_frame() {
    if (this->data.empty()) {
        this->data = cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
    }
    return this->data;
}   

AssetManager::AssetManager(const AppConfig& config) : appConfig(config) {
   std::ifstream jsonFile(config.assetsConfigFile);

    if (!jsonFile.is_open()) {
        throw std::runtime_error("Failed to open AssetConfig file: " + config.assetsConfigFile);
    }

    try {
        nlohmann::json data;
        jsonFile >> data; 
        assets = data.get<AssetsConfig>();
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        throw;
    }
}

void AssetManager::initializeAssets() {
    loadAssetsIntoMemory();
}

cv::Scalar toScalar(const std::string& hexColor) {
    if (hexColor.length() != 7 || hexColor[0] != '#') {
        throw std::invalid_argument("Invalid hex color string format.");
    }
    
    try {
        std::string red_hex = hexColor.substr(1, 2);
        std::string green_hex = hexColor.substr(3, 2);
        std::string blue_hex = hexColor.substr(5, 2);

        int r = std::stoi(red_hex, nullptr, 16);
        int g = std::stoi(green_hex, nullptr, 16);
        int b = std::stoi(blue_hex, nullptr, 16);
        
        return cv::Scalar(b, g, r);

    } catch (const std::exception& e) {
        throw std::invalid_argument("Error converting hex to integer: " + std::string(e.what()));
    }
}

void AssetManager::loadAssetsIntoMemory() {
    Background::backgroundsPath = fs::path(appConfig.assetsDir) / "backgrounds";
    Foreground::foregroundsPath = fs::path(appConfig.assetsDir) / "foregrounds";
    
    std::vector<WindowInfo> openWindows;
    bool fetchedWindows = false;

    for (auto& [key, value] : this->assets.get_mutable_backgrounds()) {

        if (value.get_type() == WINDOW_CAPTURE) {
            if (!fetchedWindows) {
                openWindows = getOpenWindows(); 
                fetchedWindows = true;
            }

            bool windowFound = false;
            std::string targetApp = value.get_target_app_name();
            std::string targetWindow = value.get_target_window_name();

            for (const auto& win : openWindows) {
                std::cout << "[DEBUG] OS sees App: '" << win.ownerName << "' with Window: '" << win.name << "'\n";
                if (win.ownerName == targetApp) {
                    value.set_capture_window_id(win.id);
                    windowFound = true;
                    std::cout << "[INFO] Successfully bound window capture '" << key 
                            << "' to ID " << win.id << " (" << win.ownerName << " - " << win.name << ")\n";
                    break;
                }
            }

            if (!windowFound) {
                std::cerr << "[WARNING] Could not find open window for capture '" << key 
                        << "' (App: " << targetApp << ", Window: " << targetWindow << "). "
                        << "Ensure the window is open before switching to it. Opened window with similar names:\n";

                for (const auto& win : openWindows) {
                    if (win.ownerName.find(targetApp) != std::string::npos)
                        std::cout << "- '" << win.ownerName << "' with Window: '" << win.name << "'\n";
                }

            }
        }
        else if (fs::exists(Background::backgroundsPath / key)) {
            value.set_source(VIDEO_LOOP, key);
        }
        else if (key.rfind("#", 0) == 0) {
            value.set_source(SOLID_COLOR, key);
        }
        else {
            std::cout << "[ERROR] Current asset is neither a color or an existing file: " << key << "\n";
            exit(1);
        }

        if (value.get_key() == "") {
            std::string pressed_key(1, displayAndGetKey("Key Assignment " + key, value.get_first_frame()));
            value.set_key(pressed_key);
            cv::destroyWindow("Key Assignment " + key);
        }
    }

    for (auto& [key, value] : this->assets.get_mutable_foregrounds()) {
        value.set_source(key);

        if (value.get_key() == "") {
            std::string pressed_key(1, displayAndGetKey("Key Assignment " + key, value.get_first_frame()));
            value.set_key(pressed_key);
            cv::destroyWindow("Key Assignment " + pressed_key);
        }
    }

    for (auto bg : this->assets.get_backgrounds()) {
        std::cout << "[INFO] Loading asset " << bg.first << ": " << bg.second.get_background_path().value_or("solid color") << " - " << bg.second.get_type() << "\n";
    }

    nlohmann::json j;
    to_json(j, this->assets);
    std::ofstream o(this->appConfig.assetsConfigFile);

    if (!o.is_open()) {
        std::cerr << "[ERROR]: Could not open the file for writing." << std::endl;
        exit(1);
    }

    o << j.dump(4);

    o.close();

    std::cout << "[INFO] Successfully saved the updated configuration to updated_config.json" << std::endl;

}

cv::Mat AssetManager::blend(const cv::Mat& background, const cv::Mat& foregroundAsset, int screenWidth, int screenHeight, double foregroundScalePercent, cv::Scalar foregroundColor, bool forceOriginalColor) {
    if (background.empty() || foregroundAsset.empty()) return background.clone();

    cv::Mat result = background.clone();

    int newWidth = static_cast<int>(screenWidth * (foregroundScalePercent / 100.0));
    int newHeight = static_cast<int>(foregroundAsset.rows * ((double)newWidth / foregroundAsset.cols));
    if (newWidth <= 0 || newHeight <= 0) return result;

    cv::Mat resizedFg;
    cv::resize(foregroundAsset, resizedFg, cv::Size(newWidth, newHeight), 0, 0, cv::INTER_AREA);
    if (resizedFg.channels() != 4) {
        cv::cvtColor(resizedFg, resizedFg, cv::COLOR_BGR2BGRA);
    }

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

    if (forceOriginalColor) {
        for (int y = 0; y < roiHeight; ++y) {
            uchar* bgPtr = bgRegion.ptr<uchar>(y);
            const uchar* fgPtr = fgRegion.ptr<uchar>(y);

            for (int x = 0; x < roiWidth; ++x) {
                int bgIdx = x * 3;
                int fgIdx = x * 4;
                uchar alpha = fgPtr[fgIdx + 3];
                
                if (alpha == 0) continue;
                
                if (alpha == 255) {
                    bgPtr[bgIdx]     = fgPtr[fgIdx];
                    bgPtr[bgIdx + 1] = fgPtr[fgIdx + 1];
                    bgPtr[bgIdx + 2] = fgPtr[fgIdx + 2];
                } else {
                    int invAlpha = 255 - alpha;
                    bgPtr[bgIdx]     = (fgPtr[fgIdx]     * alpha + bgPtr[bgIdx]     * invAlpha) / 255;
                    bgPtr[bgIdx + 1] = (fgPtr[fgIdx + 1] * alpha + bgPtr[bgIdx + 1] * invAlpha) / 255;
                    bgPtr[bgIdx + 2] = (fgPtr[fgIdx + 2] * alpha + bgPtr[bgIdx + 2] * invAlpha) / 255;
                }
            }
        }
    } else {
        for (int y = 0; y < roiHeight; ++y) {
            uchar* bgPtr = bgRegion.ptr<uchar>(y);
            const uchar* fgPtr = fgRegion.ptr<uchar>(y);

            for (int x = 0; x < roiWidth; ++x) {
                int bgIdx = x * 3;
                int fgIdx = x * 4;
                uchar alpha = fgPtr[fgIdx + 3];
                
                if (alpha == 0) continue;
                
                if (alpha == 255) {
                    bgPtr[bgIdx]     = targetB;
                    bgPtr[bgIdx + 1] = targetG;
                    bgPtr[bgIdx + 2] = targetR;
                } else {
                    int invAlpha = 255 - alpha;
                    bgPtr[bgIdx]     = (targetB * alpha + bgPtr[bgIdx]     * invAlpha) / 255;
                    bgPtr[bgIdx + 1] = (targetG * alpha + bgPtr[bgIdx + 1] * invAlpha) / 255;
                    bgPtr[bgIdx + 2] = (targetR * alpha + bgPtr[bgIdx + 2] * invAlpha) / 255;
                }
            }
        }
    }

    return result;
}

std::optional<Background> AssetManager::getDefaultBackground() {
    for (auto b : this->assets.get_backgrounds()) {
        if (b.first == this->assets.get_default_config().get_background()) {
            return b.second;
        }
    }
    return std::nullopt;
}

std::optional<Foreground> AssetManager::getDefaultForeground() {
    for (auto b : this->assets.get_foregrounds()) {
        if (b.first == this->assets.get_default_config().get_foreground()) {
            return b.second;
        }
    }

    return std::nullopt;
}

std::optional<Background> AssetManager::getBackroundByPressedKey(char pressed_key) {
    for (auto b : this->assets.get_backgrounds()) {
        std::string key(1, pressed_key);
        if (b.second.get_key() == key) {
            return assets.get_backgrounds().at(b.first);
        }
    }
    return std::nullopt;
}

std::optional<Foreground> AssetManager::getForegroundByPressedKey(char pressed_key) {
    for (auto b : this->assets.get_foregrounds()) {
        std::string key(1, pressed_key);
        if (b.second.get_key() == key) {
            return b.second;
        }
    }
    return std::nullopt;
}

char AssetManager::displayAndGetKey(const std::string& windowName, const cv::Mat asset) {
    cv::imshow(windowName, asset);
    return cv::waitKey(0);
}

std::string AssetManager::analyzeFramePalette(const cv::Mat& frame) {
    if (frame.empty()) return "";

    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(64, 64), 0, 0, cv::INTER_NEAREST);
    if (resized.channels() == 4) {
        cv::cvtColor(resized, resized, cv::COLOR_BGRA2BGR);
    } else if (resized.channels() == 1) {
        cv::cvtColor(resized, resized, cv::COLOR_GRAY2BGR);
    }

    struct NamedColor {
        std::string name;
        cv::Vec3b bgr;
    };

    std::vector<NamedColor> baseColors = {
        {"Bianco", cv::Vec3b(255, 255, 255)},
        {"Nero", cv::Vec3b(0, 0, 0)},
        {"Rosso", cv::Vec3b(0, 0, 255)},
        {"Verde", cv::Vec3b(0, 255, 0)},
        {"Blu", cv::Vec3b(255, 0, 0)},
        {"Viola", cv::Vec3b(128, 0, 128)},
        {"Giallo", cv::Vec3b(0, 255, 255)},
        {"Arancione", cv::Vec3b(0, 165, 255)}
    };

    std::map<std::string, int> counts;
    for (const auto& c : baseColors) counts[c.name] = 0;

    for (int y = 0; y < resized.rows; ++y) {
        for (int x = 0; x < resized.cols; ++x) {
            cv::Vec3b pixel = resized.at<cv::Vec3b>(y, x);
            
            int minDist = std::numeric_limits<int>::max();
            std::string closestName = "";
            
            for (const auto& bc : baseColors) {
                int db = pixel[0] - bc.bgr[0];
                int dg = pixel[1] - bc.bgr[1];
                int dr = pixel[2] - bc.bgr[2];
                int distSq = db*db + dg*dg + dr*dr;
                
                if (distSq < minDist) {
                    minDist = distSq;
                    closestName = bc.name;
                }
            }
            counts[closestName]++;
        }
    }

    std::vector<std::pair<std::string, int>> sortedCounts(counts.begin(), counts.end());
    std::sort(sortedCounts.begin(), sortedCounts.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    int threshold = (64 * 64) / 10;

    if (sortedCounts[1].second < threshold) {
        return sortedCounts[0].first;
    } else {
        std::string c1 = sortedCounts[0].first;
        std::string c2 = sortedCounts[1].first;
        if (c1 > c2) std::swap(c1, c2);
        return c1 + "-" + c2;
    }
}