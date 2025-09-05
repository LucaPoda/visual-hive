#ifndef BPM_DETECTOR_H
#define BPM_DETECTOR_H

#include <iostream>
#include <vector>
#include <deque>
#include <limits>
#include <numeric>
#include <algorithm>
#include <aubio/aubio.h>
#include <portaudio.h>
#include <mutex>
#include <memory> // For std::shared_ptr
#include <chrono>

// --- Global Constants ---
extern const uint_t SAMPLE_RATE;
extern const uint_t FRAMES_PER_BUFFER;
extern const uint_t WIN_SIZE;
extern const uint_t HOP_SIZE;
extern const std::chrono::milliseconds READ_INTERVAL;
extern const std::chrono::milliseconds CALCULATION_INTERVAL;
extern const size_t BUFFER_SIZE;
extern const double PERCENTAGE_TOLERANCE;
extern const double INITIAL_ROUNDING_TOLERANCE;
extern const double MIN_ROUNDING_TOLERANCE;
extern const double MAX_ROUNDING_TOLERANCE;
extern const double TOLERANCE_SHRINK_RATE;
extern const double TOLERANCE_GROWTH_RATE;

// --- Data Structures ---
struct AudioData {
    std::vector<float> buffer;
    std::mutex mtx;
};

// --- Shared BPM Data (Thread-Safe) ---
extern std::shared_ptr<double> g_BPM;

// --- Function Declarations ---
PaError bpmDetectionInit();
void bpmDetectionLoop();

// Helper functions (could be made private or remain here)
void addValue(std::deque<double>& timeWindow, std::vector<double>& sortedWindow, double value, size_t windowSize);
double calculateMedianBPM(const std::vector<double>& window, double currentBPM);
PaDeviceIndex listAndSelectInputDevice();
static int paCallbackMethod(const void* inputBuffer, void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData);

#endif // BPM_DETECTOR_H