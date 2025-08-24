#pragma once

#include <vector>
#include <string>
#include <optional>
#include <map>
#include <opencv2/opencv.hpp>
#include "ConfigManager.h"

namespace fs = std::filesystem;

// Enum to differentiate between background and foreground assets
enum BackgroundType {
    VIDEO_LOOP,
    SOLID_COLOR,
};

// Struct to represent a visual asset
class Background {
    public:
    static fs::path backgroundsPath;

    Background() = default;
    virtual ~Background() = default;

    private:
    std::string key;
    std::string asset_source; // HEX color or file path
    std::vector<int64_t> foregroundColor;
    
    BackgroundType type;
    cv::VideoCapture video_loop_cap;
    cv::Mat solid_color_img;


    // cv::Mat data; // Store the asset's image/video data in memory

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
    std::string get_source() { return asset_source; }

    const cv::Scalar get_foreground_color() const { 
        int64_t r = foregroundColor[0];
        int64_t g = foregroundColor[1];
        int64_t b = foregroundColor[2];
        cv::Scalar obj(b, g, r); 
        return obj;
    }

    cv::Scalar get_mutable_foreground_color() { 
        int64_t r = foregroundColor[0];
        int64_t g = foregroundColor[1];
        int64_t b = foregroundColor[2];
        cv::Scalar obj(b, g, r); 
        return obj;
    }

    void set_foreground_color(const cv::Scalar & value) { 
        int64_t b = value[0];
        int64_t g = value[1];
        int64_t r = value[2];

        this->foregroundColor = { r, g, b };
    }

    const std::optional<cv::Scalar> get_background_color() const;
    const std::optional<std::string> get_background_path() const;

    const cv::Mat get_first_frame() const;
    const cv::Mat get_solid_color_frame(int width, int height, const cv::Scalar& color) const;

    bool open();
    void close();
    cv::Mat get_next_frame();

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
    double scale;
    std::string asset_source;
    std::string key;


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

    void open();
    void close();
    cv::Mat get_next_frame();

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
    std::string lastForegroundPath; // Changed from cv::Mat to std::string
    cv::Mat lastBlendedForeground;
    std::map<char, cv::Mat> foregroundCache; // Cache for pre-processed foreground images
    
public:
    AssetManager(const AppConfig& config);
    void initializeAssets();
    
    cv::Mat blend(const cv::Mat& background, const cv::Mat& foregroundAsset, int screenWidth, int screenHeight, double foregroundScalePercent, cv::Scalar foregroundColor);
    
    

    std::shared_ptr<Background> getDefaultBackground();
    std::shared_ptr<Foreground> getDefaultForeground();

    std::shared_ptr<Background> getBackroundByPressedKey(char key);
    std::shared_ptr<Foreground> getForegroundByPressedKey(char key);

    std::shared_ptr<Background> getRandomBackground();
    std::shared_ptr<Foreground> getRandomForeground();
    
private:
    char displayAndGetKey(const std::string& windowName, const cv::Mat asset);
    void loadAssetsIntoMemory();
};

// Conversion function declarations for all classes
void to_json(nlohmann::json& j, const Background& b);
void from_json(const nlohmann::json& j, Background& b);
void to_json(nlohmann::json& j, const Foreground& f);
void from_json(const nlohmann::json& j, Foreground& f);
void to_json(nlohmann::json& j, const Default& d);
void from_json(const nlohmann::json& j, Default& d);
void to_json(nlohmann::json& j, const AssetsConfig& ac);
void from_json(const nlohmann::json& j, AssetsConfig& ac);