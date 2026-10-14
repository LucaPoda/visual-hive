#include "KNNClassifier.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>

KNNClassifier::KNNClassifier(int k) : k_neighbors(k) {}

std::vector<float> KNNClassifier::extractFeatures(const AudioMetrics& metrics) const {
    // Vettore forte a 5 dimensioni (già in scala 0.0 - 1.0 dal DSP)
    return {
        metrics.rms,
        metrics.bandSubBass,
        metrics.bandBass,
        metrics.bandHighMid,
        metrics.spectralFlux
    };
}

void KNNClassifier::addSample(const AudioMetrics& metrics, EnergyLevel label) {
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
    if (dataset.empty()) return {MID, 0.0f, 1.0f, 0.0f};

    std::vector<float> currentFeatures = extractFeatures(metrics);
    std::vector<std::pair<float, EnergyLevel>> distances;

    for (const auto& sample : dataset) {
        distances.push_back({calculateDistance(currentFeatures, sample.features), sample.label});
    }

    std::sort(distances.begin(), distances.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    int countLow = 0, countMid = 0, countHigh = 0;
    int k = std::min(k_neighbors, (int)distances.size());

    for (int i = 0; i < k; ++i) {
        if (distances[i].second == LOW) countLow++;
        else if (distances[i].second == MID) countMid++;
        else if (distances[i].second == HIGH) countHigh++;
    }

    KNNPrediction result;
    result.confLow = (float)countLow / k;
    result.confMid = (float)countMid / k;
    result.confHigh = (float)countHigh / k;

    if (countHigh >= countMid && countHigh >= countLow) result.winner = HIGH;
    else if (countLow >= countMid && countLow >= countHigh) result.winner = LOW;
    else result.winner = MID;

    return result;
}

std::map<EnergyLevel, int> KNNClassifier::getClassCounts() const {
    std::map<EnergyLevel, int> counts = {{LOW, 0}, {MID, 0}, {HIGH, 0}};
    for (const auto& sample : temp_dataset) counts[sample.label]++;
    for (const auto& sample : dataset) counts[sample.label]++;
    return counts;
}

bool KNNClassifier::saveDataset(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    for (const auto& sample : dataset) {
        for (float f : sample.features) file << f << ",";
        file << sample.label << "\n";
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
                sample.label = static_cast<EnergyLevel>(std::stoi(val));
            } else {
                sample.features.push_back(std::stof(val));
            }
        }
        // Assicurati di caricare solo vettori con 5 feature
        if (sample.features.size() == 5) {
             dataset.push_back(sample);
        }
    }
    return true;
}