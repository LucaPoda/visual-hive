#pragma once
#include <vector>
#include <string>
#include <map>
#include "AssetManager.h" // Per l'enum EnergyLevel
#include "AudioAnalyzer.h" // Per AudioMetrics

struct KNNSample {
    std::vector<float> features;
    EnergyLevel label;
};

struct KNNPrediction {
    EnergyLevel winner;
    float confLow;
    float confMid;
    float confHigh;
};

class KNNClassifier {
public:
    KNNClassifier(int k = 5);
    
    void addSample(const AudioMetrics& metrics, EnergyLevel label);
    KNNPrediction predict(const AudioMetrics& metrics);
    
    bool saveDataset(const std::string& filename) const;
    bool loadDataset(const std::string& filename);
    
    void clearTemporaryDataset();
    void commitTemporaryDataset();
    
    size_t getDatasetSize() const;
    std::map<EnergyLevel, int> getClassCounts() const;

private:
    int k_neighbors;
    std::vector<KNNSample> dataset;
    std::vector<KNNSample> temp_dataset;
    
    std::vector<float> extractFeatures(const AudioMetrics& metrics) const;
    float calculateDistance(const std::vector<float>& a, const std::vector<float>& b) const;
};