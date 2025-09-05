#include "BpmDetector.h"

// --- Global Constant Definitions ---
const uint_t SAMPLE_RATE = 44100;
const uint_t FRAMES_PER_BUFFER = 512;
const uint_t WIN_SIZE = 1024;
const uint_t HOP_SIZE = 512;
const std::chrono::milliseconds READ_INTERVAL = std::chrono::milliseconds(100);
const std::chrono::milliseconds CALCULATION_INTERVAL = std::chrono::milliseconds(500);
const size_t BUFFER_SIZE = 15;
const double PERCENTAGE_TOLERANCE = 0.1;
const double INITIAL_ROUNDING_TOLERANCE = 0.2;
const double MIN_ROUNDING_TOLERANCE = 0.01;
const double MAX_ROUNDING_TOLERANCE = 0.5;
const double TOLERANCE_SHRINK_RATE = 0.05;
const double TOLERANCE_GROWTH_RATE = 0.1;

// --- Shared BPM Data (Thread-Safe) ---
std::shared_ptr<double> g_BPM = std::make_shared<double>(0.0);

// --- Static and Global Variables for state management ---
static aubio_tempo_t* tempo_detector = nullptr;
static PaStream* stream = nullptr;
static AudioData audio_data;
static std::deque<double> timeWindow;
static std::vector<double> sortedWindow;
static std::vector<double> lastReadings;

static auto lastCalculationTime = std::chrono::steady_clock::now();
static auto lastReadingTime = std::chrono::steady_clock::now();

// --- Static variables for rounding logic ---
static double roundingTolerance = INITIAL_ROUNDING_TOLERANCE;
static double candidateBPM = -1.0;
static int confirmationCount = 0;

void addValue(std::deque<double>& timeWindow, std::vector<double>& sortedWindow, double value, size_t windowSize) {
    if (value < 100) {
        value *= 2;
    }

    if (timeWindow.size() >= windowSize) {
        double currentMedian = *g_BPM;
        double min_val = sortedWindow.front();
        double max_val = sortedWindow.back();
        
        if (value < (min_val - currentMedian * PERCENTAGE_TOLERANCE) || value > (max_val + currentMedian * PERCENTAGE_TOLERANCE)) {
            return;
        }
    }

    timeWindow.push_back(value);
    auto it = std::lower_bound(sortedWindow.begin(), sortedWindow.end(), value);
    sortedWindow.insert(it, value);

    if (timeWindow.size() >= windowSize) {
        double oldestValue = timeWindow.front();
        auto remove_it = std::lower_bound(sortedWindow.begin(), sortedWindow.end(), oldestValue);
        
        while (remove_it != sortedWindow.end() && *remove_it == oldestValue) {
            sortedWindow.erase(remove_it);
            break;
        }
        
        timeWindow.pop_front();
    }
}

double calculateMedianBPM(const std::vector<double>& window) {
    if (window.empty()) {
        return 0;
    }
     double currentBPM = *g_BPM;

    std::vector<double> sortedBPMs(window.begin(), window.end());
    std::sort(sortedBPMs.begin(), sortedBPMs.end());
    size_t size = sortedBPMs.size();
    double median_double = (size % 2 == 0) ? (sortedBPMs[size / 2 - 1] + sortedBPMs[size / 2]) / 2.0 : sortedBPMs[size / 2];
    // double corrected_bpm = median_double - (median_double * 0.005);
    double corrected_bpm = median_double;
    
    double floor_bpm = std::floor(corrected_bpm);
    double ceil_bpm = std::ceil(corrected_bpm);
    double rounded_bpm;
    // Condition for "strong evidence": a reading on the other side of the 0.5 threshold
    bool strongEvidence = (currentBPM > 0 &&
                           ((corrected_bpm > currentBPM && corrected_bpm > ceil_bpm + 0.1) ||
                            (corrected_bpm < currentBPM && corrected_bpm < floor_bpm + 0.1)));
    if (strongEvidence) {
        roundingTolerance = 0.1; // Reset to default for a quick change
        rounded_bpm = std::round(corrected_bpm);
    } else if (std::abs(corrected_bpm - floor_bpm) < roundingTolerance || std::abs(corrected_bpm - ceil_bpm) < roundingTolerance) {
        // Classic rounding with tolerance
        rounded_bpm = std::round(corrected_bpm);
        // If the rounded BPM is the same as the current BPM, shrink the window
        if (rounded_bpm == currentBPM) {
            roundingTolerance = std::max(0.01, roundingTolerance - 0.05); // Shrink, but don't go below a minimum value
        } else {
            // It was rounded to a new value, so reset the tolerance
            roundingTolerance = 0.1;
        }
    } else {
        // Drifting away from the current value
        if (corrected_bpm < currentBPM) {
            rounded_bpm = std::ceil(corrected_bpm);
        } else {
            rounded_bpm = std::floor(corrected_bpm);
        }
        // Widen the tolerance to make it easier to transition next time
        roundingTolerance = std::min(roundingTolerance + 0.1, 0.5); // Cap the tolerance at a maximum value
    }
    
    // std::cout << "[ ";
    // for (auto n : window) {
    //     std::cout << n << " ";
    // }
    // std::cout << " ] -> " << corrected_bpm << " - " << rounded_bpm << " - " << roundingTolerance << " ---" << '\r' << std::flush;
    return rounded_bpm;
}

static int paCallbackMethod(const void* inputBuffer, void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData) {
    auto data = reinterpret_cast<AudioData*>(userData);
    const float *input_data = reinterpret_cast<const float*>(inputBuffer);
    
    std::lock_guard<std::mutex> lock(data->mtx);

    if (inputBuffer == NULL) {
        data->buffer.assign(framesPerBuffer, 0.0f);
    } else {
        data->buffer.assign(input_data, input_data + framesPerBuffer);
    }
    return paContinue;
}

PaDeviceIndex listAndSelectInputDevice() {
    PaDeviceIndex inputDeviceIndex = Pa_GetDefaultInputDevice();
    const PaDeviceInfo* deviceInfo;
    int numDevices = Pa_GetDeviceCount();

    if (numDevices < 0) {
        std::cerr << "PortAudio error: Failed to get device count." << std::endl;
        return paNoDevice;
    }

    std::cout << "Available Audio Input Devices:" << std::endl;
    std::vector<PaDeviceIndex> inputDeviceList;

    for (int i = 0; i < numDevices; ++i) {
        deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo->maxInputChannels > 0) {
            std::cout << "  [" << inputDeviceList.size() << "] " << deviceInfo->name;
            if (i == Pa_GetDefaultInputDevice()) {
                std::cout << " (Default)";
            }
            std::cout << std::endl;
            inputDeviceList.push_back(i);
        }
    }

    if (inputDeviceList.empty()) {
        std::cerr << "No audio input devices found. Exiting." << std::endl;
        return paNoDevice;
    }

    int userChoice = 0;
    while (true) {
        std::cout << "Select a device by number: ";
        std::cin >> userChoice;
        if (std::cin.fail() || userChoice < 0 || userChoice >= inputDeviceList.size()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Invalid selection. Please enter a valid number." << std::endl;
        } else {
            return inputDeviceList[userChoice];
        }
    }
}

PaError bpmDetectionInit() {
    PaError err;
    
    tempo_detector = new_aubio_tempo("specflux", WIN_SIZE, HOP_SIZE, SAMPLE_RATE);
    if (!tempo_detector) {
        std::cerr << "Error creating aubio tempo detector." << std::endl;
        return 1;
    }

    err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return err;
    }

    PaDeviceIndex selectedDevice = listAndSelectInputDevice();
    if (selectedDevice == paNoDevice) {
        Pa_Terminate();
        del_aubio_tempo(tempo_detector);
        return 1;
    }

    PaStreamParameters inputParameters;
    inputParameters.device = selectedDevice;
    inputParameters.channelCount = 1;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(selectedDevice)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(&stream,
        &inputParameters,
        NULL,
        SAMPLE_RATE,
        HOP_SIZE,
        paClipOff,
        paCallbackMethod,
        &audio_data);
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        del_aubio_tempo(tempo_detector);
        return err;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        del_aubio_tempo(tempo_detector);
        return err;
    }
    
    std::cout << "\nListening on device: " << Pa_GetDeviceInfo(selectedDevice)->name << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    return paNoError;
}

void bpmDetectionLoop() {
    while (true) {
        std::lock_guard<std::mutex> lock(audio_data.mtx);
        
        if (!audio_data.buffer.empty()) {
            fvec_t* input_buffer = new_fvec(audio_data.buffer.size());
            if (!input_buffer) {
                std::cerr << "ERROR: Failed to create new_fvec!" << std::endl;
                // Proper error handling and cleanup would be needed here
                continue;
            }

            for (size_t i = 0; i < audio_data.buffer.size(); ++i) {
                input_buffer->data[i] = audio_data.buffer[i];
            }

            fvec_t* tempo = new_fvec(1);
            if (!tempo) {
                std::cerr << "ERROR: Failed to create new_fvec for tempo!" << std::endl;
                del_fvec(input_buffer);
                continue;
            }

            aubio_tempo_do(tempo_detector, input_buffer, tempo);
            
            auto now = std::chrono::steady_clock::now();
            
            if (now - lastReadingTime >= READ_INTERVAL) {
                double bpm = aubio_tempo_get_bpm(tempo_detector);
                auto it = std::lower_bound(lastReadings.begin(), lastReadings.end(), bpm);
                lastReadings.insert(it, bpm);
                lastReadingTime = now;
            }
            
            if (now - lastCalculationTime >= CALCULATION_INTERVAL) {
                if (lastReadings.size() > 0) {
                    double last_reading = lastReadings[2]; 
                    addValue(timeWindow, sortedWindow, last_reading, BUFFER_SIZE);
                    
                    // --- Dynamic Rounding Logic (relocated from calculateMedianBPM) ---
                    double median_double = calculateMedianBPM(sortedWindow);
                    double corrected_bpm = median_double - (median_double * 0.015);

                    double floor_bpm = std::floor(corrected_bpm);
                    double ceil_bpm = std::ceil(corrected_bpm);
                    double rounded_bpm;
                    
                    bool strongEvidence = (*g_BPM > 0 && 
                                           ((corrected_bpm > *g_BPM && corrected_bpm > ceil_bpm + 0.1) ||
                                            (corrected_bpm < *g_BPM && corrected_bpm < floor_bpm + 0.1)));

                    if (strongEvidence) {
                        roundingTolerance = INITIAL_ROUNDING_TOLERANCE;
                        rounded_bpm = std::round(corrected_bpm);
                    } else if (std::abs(corrected_bpm - floor_bpm) < roundingTolerance || std::abs(corrected_bpm - ceil_bpm) < roundingTolerance) {
                        rounded_bpm = std::round(corrected_bpm);
                        if (rounded_bpm == *g_BPM) {
                            roundingTolerance = std::max(MIN_ROUNDING_TOLERANCE, roundingTolerance - TOLERANCE_SHRINK_RATE);
                        } else {
                            roundingTolerance = INITIAL_ROUNDING_TOLERANCE;
                        }
                    } else {
                        if (corrected_bpm < *g_BPM) {
                            rounded_bpm = std::ceil(corrected_bpm);
                        } else {
                            rounded_bpm = std::floor(corrected_bpm);
                        }
                        roundingTolerance = std::min(MAX_ROUNDING_TOLERANCE, roundingTolerance + TOLERANCE_GROWTH_RATE);
                    }

                    *g_BPM = rounded_bpm;
                    
                    std::cout << "\t\t\t  [ ";
                    for (auto n : sortedWindow) {
                        std::cout << n << " ";
                    }
                    std::cout << " ] -> " << last_reading << " - " << corrected_bpm << " - " << rounded_bpm << " - " << roundingTolerance << " ---" << '\r' << std::flush;
                }
                lastCalculationTime = now;
                lastReadings.clear();
            }

            del_fvec(input_buffer);
            del_fvec(tempo);
            audio_data.buffer.clear();
        }
    }
}
