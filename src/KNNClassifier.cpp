// KNNClassifier.cpp
#include "KNNClassifier.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>

KNNClassifier::KNNClassifier(int k) : k_neighbors(k) {}

std::vector<float> KNNClassifier::extractFeatures(const AudioMetrics& m) const {
    return {
        m.rms, m.zcr, m.crestFactor, m.spectralFlux,
        m.spectralCentroid, m.spectralSpread, m.spectralRolloff,
        m.bandSubBass, m.bandBass, m.bandLowMid, m.bandMid,
        m.bandHighMid, m.bandPresence, m.bandBrilliance
    };
}

void KNNClassifier::addSample(const AudioMetrics& metrics, AiTrainingLabel label) {
    temp_dataset.push_back({extractFeatures(metrics), label});
}

void KNNClassifier::commitTemporaryDataset() {
    dataset.insert(dataset.end(), temp_dataset.begin(), temp_dataset.end());
    temp_dataset.clear();
}

void KNNClassifier::clearTemporaryDataset() {
    temp_dataset.clear();
}

float KNNClassifier::calculateDistance(const std::vector<float>& a, const std::vector<float>& b) const {
    float sum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

KNNPrediction KNNClassifier::predict(const AudioMetrics& metrics) {
    KNNPrediction result;
    result.probabilities.fill(0.0f);
    if (dataset.empty()) return result;

    std::vector<float> currentFeatures = extractFeatures(metrics);
    std::vector<std::pair<float, AiTrainingLabel>> distances;

    for (const auto& sample : dataset) {
        distances.push_back({calculateDistance(currentFeatures, sample.features), sample.label});
    }

    std::sort(distances.begin(), distances.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    int k = std::min(k_neighbors, (int)distances.size());
    std::array<int, 9> classCounts = {0};

    for (int i = 0; i < k; ++i) classCounts[distances[i].second]++;
    for (int i = 0; i < 9; ++i) result.probabilities[i] = static_cast<float>(classCounts[i]) / k;

    return result;
}

// Assicurati che in loadDataset il controllo sia: if (sample.features.size() == 14) dataset.push_back(sample);

std::map<AiTrainingLabel, int> KNNClassifier::getClassCounts() const {
    std::map<AiTrainingLabel, int> counts;
    for (int i = 0; i < 8; ++i) counts[static_cast<AiTrainingLabel>(i)] = 0;
    
    for (const auto& sample : temp_dataset) counts[sample.label]++;
    for (const auto& sample : dataset) counts[sample.label]++;
    return counts;
}

bool KNNClassifier::saveDataset(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    for (const auto& sample : dataset) {
        for (float f : sample.features) file << f << ",";
        file << static_cast<int>(sample.label) << "\n";
    }
    return true;
}

bool KNNClassifier::loadDataset(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    dataset.clear();
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string val;
        KNNSample sample;
        while (std::getline(ss, val, ',')) {
            if (ss.peek() == EOF || ss.peek() == '\n' || ss.peek() == '\r') {
                sample.label = static_cast<AiTrainingLabel>(std::stoi(val));
            } else {
                sample.features.push_back(std::stof(val));
            }
        }
        if (sample.features.size() == 14) {
             dataset.push_back(sample);
        }
    }
    return true;
}