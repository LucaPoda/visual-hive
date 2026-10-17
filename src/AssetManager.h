#pragma once

#include <vector>
#include <string>
#include <optional>
#include <map>
#include <atomic>
#include <thread>
#include <memory>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include "ConfigManager.h"

namespace fs = std::filesystem;

enum EventPhase {
    AMBIENT,
    LOW,
    MID,
    HIGH
};

enum SongDynamic {
    VERSE,
    BUILD,
    CHORUS,
    MANUAL
};

enum BackgroundType {
    VIDEO_LOOP,
    SOLID_COLOR,
    WINDOW_CAPTURE
};

struct VideoAnalysisTask {
    std::string filepath;
    std::atomic<float> progress{0.0f};
    std::atomic<bool> isDone{false};
    std::string primaryColor;
    std::string secondaryColor;
    int bestFrameIndex = 0;
};

class Background {
    public:
    static fs::path backgroundsPath;

    Background() = default;
    virtual ~Background() = default;

    private:
    std::string key;
    std::string asset_source; 
    std::vector<int64_t> foregroundColor = {255, 255, 255};
    
    long long lastVideoFrameTime = 0;
    cv::Mat currentVideoFrame;

    BackgroundType type;
    EventPhase eventPhase = AMBIENT;
    SongDynamic songDynamic = VERSE;
    std::string primaryColor = "";
    std::string secondaryColor = "";
    int thumbnailFrameIndex = 0;
    bool isEnabledGlobal = true;

    bool forceNoForeground = false;

    cv::VideoCapture video_loop_cap;
    cv::Mat solid_color_img;

    int captureWindowId = -1; 
    std::string targetAppName;
    std::string targetWindowName;

    public:
    const std::string & get_key() const { return key; }
    std::string & get_mutable_key() { return key; }
    void set_key(const std::string & value) { this->key = value; }

    const BackgroundType & get_type() const { return type; }
    BackgroundType & get_mutable_type() { return type; }

    void set_source(const BackgroundType & type, const std::string asset_source) { 
        this->type = type; 
        this->asset_source = asset_source; 
    }
    std::string get_source() const { return asset_source; }

    const cv::Scalar get_foreground_color() const { 
        if(foregroundColor.size() < 3) return cv::Scalar(255,255,255);
        int64_t r = foregroundColor[0];
        int64_t g = foregroundColor[1];
        int64_t b = foregroundColor[2];
        return cv::Scalar(b, g, r); 
    }

    cv::Scalar get_mutable_foreground_color() { 
        if(foregroundColor.size() < 3) return cv::Scalar(255,255,255);
        int64_t r = foregroundColor[0];
        int64_t g = foregroundColor[1];
        int64_t b = foregroundColor[2];
        return cv::Scalar(b, g, r); 
    }

    void set_foreground_color(const cv::Scalar & value) { 
        this->foregroundColor = { static_cast<int64_t>(value[2]), static_cast<int64_t>(value[1]), static_cast<int64_t>(value[0]) };
    }

    const std::optional<cv::Scalar> get_background_color() const;
    const std::optional<std::string> get_background_path() const;

    const cv::Mat get_first_frame();
    const cv::Mat get_solid_color_frame(int width, int height, const cv::Scalar& color) const;

    const EventPhase& get_event_phase() const { return eventPhase; }
    void set_event_phase(const EventPhase& phase) { this->eventPhase = phase; }

    const SongDynamic& get_song_dynamic() const { return songDynamic; }
    void set_song_dynamic(const SongDynamic& dynamic) { this->songDynamic = dynamic; }

    const std::string& get_primary_color() const { return primaryColor; }
    void set_primary_color(const std::string& color) { this->primaryColor = color; }

    const std::string& get_secondary_color() const { return secondaryColor; }
    void set_secondary_color(const std::string& color) { this->secondaryColor = color; }

    int get_thumbnail_frame_index() const { return thumbnailFrameIndex; }
    void set_thumbnail_frame_index(int index) { this->thumbnailFrameIndex = index; }

    bool get_is_enabled_global() const { return isEnabledGlobal; }
    void set_is_enabled_global(bool enabled) { this->isEnabledGlobal = enabled; }

    const bool& get_force_no_foreground() const { return forceNoForeground; }
    void set_force_no_foreground(bool value) { this->forceNoForeground = value; }

    int get_capture_window_id() const { return captureWindowId; }
    void set_capture_window_id(int id) { this->captureWindowId = id; }

    const std::string& get_target_app_name() const { return targetAppName; }
    void set_target_app_name(const std::string& name) { targetAppName = name; }

    const std::string& get_target_window_name() const { return targetWindowName; }
    void set_target_window_name(const std::string& name) { targetWindowName = name; }

    bool open();
    void close();
    cv::Mat get_next_frame();
    cv::Mat get_current_frame() const;

    double get_fps();

    void setBackgroundsPath(fs::path path) { backgroundsPath = path; }

    friend void to_json(nlohmann::json& j, const Background& b);
    friend void from_json(const nlohmann::json& j, Background& b);
};

class Foreground {
    public:
    static fs::path foregroundsPath;
    Foreground() = default;
    virtual ~Foreground() = default;

    private:
    double scale = 1.0;
    std::string asset_source;
    std::string key;
    bool forceOriginalColors = false;

    std::string primaryColor = "";
    std::string secondaryColor = "";
    bool isEnabledGlobal = true;
    std::string customText = "";
    int fontSize = 32;

    cv::Mat data;

    public:
    const double & get_scale() const { return scale; }
    double & get_mutable_scale() { return scale; }
    void set_scale(const double & value) { this->scale = value; }

    const std::string & get_key() const { return key; }
    std::string & get_mutable_key() { return key; }
    void set_key(const std::string & value) { this->key = value; }

    void set_source(const std::string asset_source) { 
        this->asset_source = asset_source; 
    }
    const std::string get_foreground_path() const;
    
    const cv::Mat get_first_frame() const;

    const bool& get_force_original_colors() const { return forceOriginalColors; }
    void set_force_original_colors(bool value) { this->forceOriginalColors = value; }

    const std::string& get_primary_color() const { return primaryColor; }
    void set_primary_color(const std::string& color) { this->primaryColor = color; }

    const std::string& get_secondary_color() const { return secondaryColor; }
    void set_secondary_color(const std::string& color) { this->secondaryColor = color; }

    bool get_is_enabled_global() const { return isEnabledGlobal; }
    void set_is_enabled_global(bool enabled) { this->isEnabledGlobal = enabled; }

    const std::string& get_custom_text() const { return customText; }
    void set_custom_text(const std::string& text) { this->customText = text; }

    int get_font_size() const { return fontSize; }
    void set_font_size(int size) { this->fontSize = size; }

    void set_raw_data(const cv::Mat& rawMat) {
        this->data = rawMat.clone();
        this->asset_source = "RAW_RAM_DATA";
    }

    bool open();
    void close();
    cv::Mat get_next_frame();
    cv::Mat get_current_frame() const;

    friend void to_json(nlohmann::json& j, const Foreground& f);
    friend void from_json(const nlohmann::json& j, Foreground& f);
};

class Default {
    public:
    Default() = default;
    virtual ~Default() = default;

    private:
    std::string background;
    std::string foreground;

    public:
    const std::string & get_background() const { return background; }
    std::string & get_mutable_background() { return background; }
    void set_background(const std::string & value) { this->background = value; }

    const std::string & get_foreground() const { return foreground; }
    std::string & get_mutable_foreground() { return foreground; }
    void set_foreground(const std::string & value) { this->foreground = value; }

    friend void to_json(nlohmann::json& j, const Default& d);
    friend void from_json(const nlohmann::json& j, Default& d);
};

class AssetsConfig {
    public:
    AssetsConfig() = default;
    virtual ~AssetsConfig() = default;

    private:
    std::map<std::string, Background> backgrounds;
    std::map<std::string, Foreground> foregrounds;
    Default default_config;
    
    public:
    const std::map<std::string, Background> & get_backgrounds() const { return backgrounds; }
    std::map<std::string, Background> & get_mutable_backgrounds() { return backgrounds; }
    void set_backgrounds(const std::map<std::string, Background> & value) { this->backgrounds = value; }

    const Default & get_default_config() const { return default_config; }
    Default & get_mutable_default_config() { return default_config; }
    void set_default_config(const Default & value) { this->default_config = value; }

    const std::map<std::string, Foreground> & get_foregrounds() const { return foregrounds; }
    std::map<std::string, Foreground> & get_mutable_foregrounds() { return foregrounds; }
    void set_foregrounds(const std::map<std::string, Foreground> & value) { this->foregrounds = value; }

    friend void to_json(nlohmann::json& j, const AssetsConfig& ac);
    friend void from_json(const nlohmann::json& j, AssetsConfig& ac);
};

class AssetManager {
private:
    const AppConfig& appConfig;
    AssetsConfig assets;
    cv::Scalar activeForegroundColor;
    std::string lastForegroundPath; 
    cv::Mat lastBlendedForeground;
    std::map<char, cv::Mat> foregroundCache; 

    std::vector<std::pair<Background, std::shared_ptr<VideoAnalysisTask>>> stagingBackgrounds;
    std::vector<std::pair<Foreground, std::shared_ptr<VideoAnalysisTask>>> stagingForegrounds;
    std::vector<std::thread> analysisThreads;
    
public:
    AssetManager(const AppConfig& config);
    ~AssetManager();
    void initializeAssets();
    
    cv::Mat blend(const cv::Mat& background, const cv::Mat& foregroundAsset, int screenWidth, int screenHeight, double foregroundScalePercent, cv::Scalar foregroundColor, bool forceOriginalColor = false);

    const AssetsConfig& getAssets() const { return assets; }
    AssetsConfig& getMutableAssets() { return assets; }

    std::optional<Background> getDefaultBackground();
    std::optional<Foreground> getDefaultForeground();
    std::optional<Background> getBackroundByPressedKey(char key);
    std::optional<Foreground> getForegroundByPressedKey(char key);

    static std::pair<std::string, std::string> analyzeFramePalette(const cv::Mat& frame);
    Foreground createTextForeground(const std::string& text, int fontSize);

    std::shared_ptr<VideoAnalysisTask> analyzeVideoAsync(const std::string& filepath);
    std::vector<std::pair<Background, std::shared_ptr<VideoAnalysisTask>>>& getStagingBackgrounds() { return stagingBackgrounds; }
    std::vector<std::pair<Foreground, std::shared_ptr<VideoAnalysisTask>>>& getStagingForegrounds() { return stagingForegrounds; }

    void saveAssetsConfig();

private:
    char displayAndGetKey(const std::string& windowName, const cv::Mat asset);
    void loadAssetsIntoMemory();
};

void to_json(nlohmann::json& j, const Background& b);
void from_json(const nlohmann::json& j, Background& b);
void to_json(nlohmann::json& j, const Foreground& f);
void from_json(const nlohmann::json& j, Foreground& f);
void to_json(nlohmann::json& j, const Default& d);
void from_json(const nlohmann::json& j, Default& d);
void to_json(nlohmann::json& j, const AssetsConfig& ac);
void from_json(const nlohmann::json& j, AssetsConfig& ac);