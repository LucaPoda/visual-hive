#pragma once
#include <vector>
#include <string>
#include <map>
#include <array>
#include "AssetManager.h"
#include "AudioAnalyzer.h" 

enum AiTrainingLabel {
    EMPTY_PAD = 0,
    LIGHT_RHYTHM = 1,
    VOCAL_AMBIENT = 2,
    BUILD_SNARE = 3,
    BUILD_ACID = 4,
    BUILD_MELODIC = 5,
    DROP_RAW = 6,
    DROP_MELODIC = 7,
    BRIDGE = 8
};

struct KNNSample {
    std::vector<float> features;
    AiTrainingLabel label;
};

struct KNNPrediction {
    std::array<float, 9> probabilities; 
};

class KNNClassifier {
public:
    KNNClassifier(int k = 5);
    void addSample(const AudioMetrics& metrics, AiTrainingLabel label);
    KNNPrediction predict(const AudioMetrics& metrics);
    bool saveDataset(const std::string& filename) const;
    bool loadDataset(const std::string& filename);
    void clearTemporaryDataset();
    void commitTemporaryDataset();
    size_t getDatasetSize() const { return dataset.size(); }
    std::map<AiTrainingLabel, int> getClassCounts() const;
    void setK(int k) { k_neighbors = k; }
    int getK() const { return k_neighbors; }

private:
    int k_neighbors;
    std::vector<KNNSample> dataset;
    std::vector<KNNSample> temp_dataset;
    std::vector<float> extractFeatures(const AudioMetrics& metrics) const;
    float calculateDistance(const std::vector<float>& a, const std::vector<float>& b) const;
};