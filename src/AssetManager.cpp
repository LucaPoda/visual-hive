#include "AssetManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>
#include <random>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace fs = std::filesystem;

cv::Scalar toScalar(const std::string& hexColor);

fs::path Background::backgroundsPath;
fs::path Foreground::foregroundsPath;

// Background conversion
void to_json(nlohmann::json& j, const Background& b) {
    j = nlohmann::json{
        {"foreground_color", b.foregroundColor}, 
        {"key", b.key}
    };
}

void from_json(const nlohmann::json& j, Background& b) {
    j.at("foreground_color").get_to(b.foregroundColor);
    j.at("key").get_to(b.key);
}

// Foreground conversion
void to_json(nlohmann::json& j, const Foreground& f) {
    j = nlohmann::json{
        {"scale", f.scale},
        {"key", f.key}
    };
}

void from_json(const nlohmann::json& j, Foreground& f) {
    j.at("scale").get_to(f.scale);
    j.at("key").get_to(f.key);
}

// Default conversion
void to_json(nlohmann::json& j, const Default& d) {
    j = nlohmann::json{{"background", d.background}, {"foreground", d.foreground}};
}

void from_json(const nlohmann::json& j, Default& d) {
    j.at("background").get_to(d.background);
    j.at("foreground").get_to(d.foreground);
}

// AssetsConfig conversion
void to_json(nlohmann::json& j, const AssetsConfig& ac) {
    j = nlohmann::json{
        {"backgrounds", ac.backgrounds},
        {"default", ac.default_config},
        {"foregrounds", ac.foregrounds}
    };
}

void from_json(const nlohmann::json& j, AssetsConfig& ac) {
    j.at("backgrounds").get_to(ac.backgrounds);
    j.at("default").get_to(ac.default_config);
    j.at("foregrounds").get_to(ac.foregrounds);
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
        return this->get_solid_color_frame(1920, 1080, this->get_background_color().value()); // todo: define default width and height or assign width and height to the background itself
    }
}

bool Background::open() {
    if (this->type == VIDEO_LOOP) {
        this->video_loop_cap.open(this->get_background_path().value());

        return this->video_loop_cap.isOpened();
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
}

cv::Mat Background::get_next_frame() {
    if (this->type == VIDEO_LOOP) {
        cv::Mat frame;
        this->video_loop_cap >> frame;

        if (frame.empty()) { // loop the video when the end is reached
            this->video_loop_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            this->video_loop_cap >> frame;
            if (frame.empty()) {
                std::cerr << "Error: Could not loop video. Exiting.\n";
                exit(1);
            }
        }
        return frame;
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
        return 30.0; // default to 30 FPS --- todo: this should a config param
    }
}

const std::string Foreground::get_foreground_path() const {
    return foregroundsPath / this->asset_source;
}

const cv::Mat Foreground::get_first_frame() const {
    return cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
}

void Foreground::open() {
    this->data = cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
}

void Foreground::close() {
    // todo: understand how to close an image
}

cv::Mat Foreground::get_next_frame() {
    if (this->data.empty()) {
        this->data = cv::imread(this->get_foreground_path(), cv::IMREAD_UNCHANGED);
    }
    return this->data;
}   

// Constructor now takes the AppConfig object
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

// Main initialization function
void AssetManager::initializeAssets() {
    loadAssetsIntoMemory();
}

cv::Scalar toScalar(const std::string& hexColor) {
    if (hexColor.length() != 7 || hexColor[0] != '#') {
        throw std::invalid_argument("Invalid hex color string format.");
    }
    
    try {
        // Extract R, G, and B substrings
        std::string red_hex = hexColor.substr(1, 2);
        std::string green_hex = hexColor.substr(3, 2);
        std::string blue_hex = hexColor.substr(5, 2);

        // Convert hex strings to integer values
        int r = std::stoi(red_hex, nullptr, 16);
        int g = std::stoi(green_hex, nullptr, 16);
        int b = std::stoi(blue_hex, nullptr, 16);
        
        // OpenCV uses BGR format, so we return the values in the correct order
        return cv::Scalar(b, g, r);

    } catch (const std::exception& e) {
        throw std::invalid_argument("Error converting hex to integer: " + std::string(e.what()));
    }
}

void AssetManager::loadAssetsIntoMemory() {
    Background::backgroundsPath = fs::path(appConfig.assetsDir) / "backgrounds";
    Foreground::foregroundsPath = fs::path(appConfig.assetsDir) / "foregrounds";
    
    for (auto& [key, value] : this->assets.get_mutable_backgrounds()) {
        if (fs::exists(Background::backgroundsPath / key)) {
            value.set_source(VIDEO_LOOP, key);
        }
        else if (key.rfind("#", 0) == 0) {
            value.set_source(SOLID_COLOR, key);
        }
        else {
            std::cout << "ERROR: Current asset is neither a color or an existing file: " << key << "\n";
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
        std::cout << bg.first << ": " << bg.second.get_background_path().value_or("solid color") << " - " << bg.second.get_type() << "\n";
    }

    nlohmann::json j;
    to_json(j, this->assets);
    std::ofstream o(this->appConfig.assetsConfigFile);

    if (!o.is_open()) {
        std::cerr << "Error: Could not open the file for writing." << std::endl;
        exit(1);
    }

    o << j.dump(4);

    o.close();

    std::cout << "Successfully saved the updated configuration to updated_config.json" << std::endl;

}

// Public method to blend foreground with alpha channel onto a background
cv::Mat AssetManager::blend(const cv::Mat& background, const cv::Mat& foregroundAsset, int screenWidth, int screenHeight, double foregroundScalePercent, cv::Scalar foregroundColor) {
    if (background.empty() || foregroundAsset.empty()) {
        return background;
    }

    cv::Mat blended = background.clone();

    // Pre-process the foreground image for blending
    cv::Mat resizedForeground;
    cv::Mat solidColorForeground;
    
    int targetWidth = static_cast<int>(screenWidth * (foregroundScalePercent / 100.0));
    double aspectRatio = static_cast<double>(foregroundAsset.rows) / foregroundAsset.cols;
    int newHeight = static_cast<int>(targetWidth * aspectRatio);

    // Adjust dimensions to fit the screen
    if (targetWidth > screenWidth || newHeight > screenHeight) {
        std::cout << "Warning: Foreground scale is too large. Adjusting to fit screen." << std::endl;
        
        if (targetWidth > screenWidth) {
            targetWidth = screenWidth;
            newHeight = static_cast<int>(targetWidth * aspectRatio);
        }
        if (newHeight > screenHeight) {
            newHeight = screenHeight;
            targetWidth = static_cast<int>(newHeight / aspectRatio);
        }
    }
    
    // Resize the foreground image
    cv::resize(foregroundAsset, resizedForeground, cv::Size(targetWidth, newHeight));

    // Calculate position to center the foreground at the bottom of the screen
    int xOffset = (blended.cols - targetWidth) / 2;
    int yOffset = (blended.rows - newHeight) / 2;
    
    // Create the ROI on the background image where the foreground will be placed
    cv::Rect roi(xOffset, yOffset, targetWidth, newHeight);
    
    // Ensure the ROI is completely within the background image bounds
    cv::Rect safeRoi = roi & cv::Rect(0, 0, blended.cols, blended.rows);
    
    if (safeRoi.empty()) {
        // The foreground image is completely outside the background, do nothing.
        return blended;
    }

    // Adjust the resizedForeground to match the clamped ROI dimensions if needed
    cv::Mat adjustedForeground = resizedForeground(cv::Rect(
        safeRoi.x - roi.x,
        safeRoi.y - roi.y,
        safeRoi.width,
        safeRoi.height
    ));

    if (adjustedForeground.empty()) {
        return blended;
    }

    if (adjustedForeground.channels() == 4) {
        // Split the foreground into BGR and Alpha channels
        std::vector<cv::Mat> channels;
        cv::split(adjustedForeground, channels);
        cv::Mat bgr = adjustedForeground.clone();
        cv::Mat alpha = channels[3];

        // Create a new foreground image with a single, custom color
        cv::Mat solidColorForeground(adjustedForeground.size(), CV_8UC3, foregroundColor);

        // Copy the solid color to the ROI using the alpha channel as a mask
        solidColorForeground.copyTo(blended(safeRoi), alpha);
    } else {
        // For images without an alpha channel, just copy them directly
        adjustedForeground.copyTo(blended(safeRoi));
    }

    lastBlendedForeground = solidColorForeground;
    
    // Calculate position to center the foreground
    int xForegroundOffset = (blended.cols - lastBlendedForeground.cols) / 2;
    int yForegroundOffset = (blended.rows - lastBlendedForeground.rows) / 2;
    
    cv::Rect foregroundRoi(xForegroundOffset, yForegroundOffset, lastBlendedForeground.cols, lastBlendedForeground.rows);
    cv::Rect safeForegroundRoi = foregroundRoi & cv::Rect(0, 0, blended.cols, blended.rows);
    
    if (safeForegroundRoi.empty()) {
        return blended;
    }

    adjustedForeground = lastBlendedForeground(cv::Rect(
        safeForegroundRoi.x - foregroundRoi.x,
        safeForegroundRoi.y - foregroundRoi.y,
        safeForegroundRoi.width,
        safeForegroundRoi.height
    ));

    if (adjustedForeground.empty()) {
        return blended;
    }

    adjustedForeground.copyTo(blended(safeRoi));
    
    return blended;
}

std::shared_ptr<Background> AssetManager::getDefaultBackground() {
    for (auto b : this->assets.get_backgrounds()) {
        if (b.first == this->assets.get_default_config().get_background()) {
            return std::make_shared<Background>(b.second);
        }
    }
    return nullptr;
}

std::shared_ptr<Foreground> AssetManager::getDefaultForeground() {
    for (auto b : this->assets.get_foregrounds()) {
        if (b.first == this->assets.get_default_config().get_foreground()) {
            return std::make_shared<Foreground>(b.second);
        }
    }

    return nullptr;
}

std::shared_ptr<Background> AssetManager::getBackroundByPressedKey(char pressed_key) {
    for (auto b : this->assets.get_backgrounds()) {
        std::string key(1, pressed_key);
        if (b.second.get_key() == key) {
            return std::make_shared<Background>(assets.get_backgrounds().at(b.first));
        }
    }
    return nullptr;
}

std::shared_ptr<Foreground> AssetManager::getForegroundByPressedKey(char pressed_key) {
    for (auto b : this->assets.get_foregrounds()) {
        std::string key(1, pressed_key);
        if (b.second.get_key() == key) {
            return std::make_shared<Foreground>(b.second);
        }
    }
    return nullptr;
}

std::shared_ptr<Background> AssetManager::getRandomBackground() {
    const auto& backgrounds = this->assets.get_backgrounds();
    if (backgrounds.empty()) {
        return nullptr;
    }

    // Use a modern random number generator seeded with a high-resolution clock.
    unsigned seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> distrib(0, backgrounds.size() - 1);

    // Get a random index.
    long randomIndex = distrib(gen);

    // Use a std::map iterator to find the element at the random index.
    auto it = backgrounds.begin();
    std::advance(it, randomIndex);
    
    return std::make_shared<Background>(it->second);
}

/**
 * @brief Returns a shared pointer to a random foreground asset.
 * @return A shared pointer to a random Foreground object, or nullptr if no foregrounds are available.
 */
std::shared_ptr<Foreground> AssetManager::getRandomForeground() {
    const auto& foregrounds = this->assets.get_foregrounds();
    if (foregrounds.empty()) {
        return nullptr;
    }

    // Use a modern random number generator seeded with a high-resolution clock.
    unsigned seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> distrib(0, foregrounds.size() - 1);

    // Get a random index.
    long randomIndex = distrib(gen);

    // Use a std::map iterator to find the element at the random index.
    auto it = foregrounds.begin();
    std::advance(it, randomIndex);

    return std::make_shared<Foreground>(it->second);
}

// Helper to display a visual and get a key press
char AssetManager::displayAndGetKey(const std::string& windowName, const cv::Mat asset) {
    cv::imshow(windowName, asset);
    return cv::waitKey(0);
}