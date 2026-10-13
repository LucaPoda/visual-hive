#include "AudioAnalyzer.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>

const double PI = 3.14159265358979323846;
typedef std::complex<float> Complex;

// FFT Iterativa In-Place (Cooley-Tukey Radix-2) - 0 Allocazioni
void computeFFT(std::vector<Complex>& x) {
    size_t N = x.size();
    if (N <= 1) return;

    for (size_t i = 1, j = 0; i < N; i++) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    for (size_t len = 2; len <= N; len <<= 1) {
        float angle = -2 * PI / len;
        Complex wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < N; i += len) {
            Complex w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; j++) {
                Complex u = x[i + j];
                Complex v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

AudioAnalyzer::AudioAnalyzer() {}

AudioAnalyzer::~AudioAnalyzer() {
    stop();
}

std::vector<AudioDeviceInfo> AudioAnalyzer::getInputDevices() {
    std::vector<AudioDeviceInfo> devices;
    std::vector<unsigned int> ids = adc.getDeviceIds();
    for (unsigned int id : ids) {
        RtAudio::DeviceInfo info = adc.getDeviceInfo(id);
        if (info.inputChannels > 0) {
            devices.push_back({id, info.name});
        }
    }
    return devices;
}

bool AudioAnalyzer::start(unsigned int deviceId) {
    if (adc.getDeviceCount() < 1) {
        std::cerr << "[AUDIO] Nessun dispositivo audio trovato.\n";
        return false;
    }

    unsigned int selectedId = (deviceId == 0) ? adc.getDefaultInputDevice() : deviceId;
    RtAudio::DeviceInfo info = adc.getDeviceInfo(selectedId);

    if (info.inputChannels < 1) {
        auto devices = getInputDevices();
        if (devices.empty()) return false;
        selectedId = devices[0].id;
    }

    currentDeviceId = selectedId;

    RtAudio::StreamParameters parameters;
    parameters.deviceId = currentDeviceId;
    parameters.nChannels = 1;
    parameters.firstChannel = 0;

    unsigned int bufferFrames = 512;
    
    // Inizializza BTrack (HopSize = bufferFrames, FrameSize = bufferFrames * 2)
    btrack = std::make_unique<BTrack>(bufferFrames, bufferFrames * 2);
    btrackBuffer.resize(bufferFrames, 0.0);
    
    try {
        if (adc.isStreamRunning()) adc.stopStream();
        if (adc.isStreamOpen()) adc.closeStream();

        if (adc.openStream(nullptr, &parameters, RTAUDIO_FLOAT32, sampleRate, &bufferFrames, &AudioAnalyzer::audioCallback, this) != RTAUDIO_NO_ERROR) {
            std::cerr << "[AUDIO] Errore openStream: " << adc.getErrorText() << "\n";
            return false;
        }
        
        if (adc.startStream() != RTAUDIO_NO_ERROR) {
            std::cerr << "[AUDIO] Errore startStream: " << adc.getErrorText() << "\n";
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[AUDIO] Eccezione critica RtAudio: " << e.what() << "\n";
        return false;
    }

    std::cout << "[AUDIO] Stream avviato. BTrack Inizializzato (Buffer: " << bufferFrames << ").\n";
    return true;
}

void AudioAnalyzer::stop() {
    try {
        if (adc.isStreamRunning()) adc.stopStream(); 
        if (adc.isStreamOpen()) adc.closeStream(); 
    } catch (const std::exception& e) {
        std::cerr << "[AUDIO] Eccezione durante lo stop: " << e.what() << "\n";
    }
}

AudioMetrics AudioAnalyzer::getMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    // Auto-reset del flag isBeat dopo la lettura per evitare falsi positivi nel main loop
    AudioMetrics copy = currentMetrics;
    currentMetrics.isBeat = false; 
    return copy;
}

int AudioAnalyzer::audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                                 double streamTime, RtAudioStreamStatus status, void* userData) {
    if (inputBuffer == nullptr) return 0; 
    
    AudioAnalyzer* analyzer = static_cast<AudioAnalyzer*>(userData);
    analyzer->processBuffer(static_cast<float*>(inputBuffer), nBufferFrames, streamTime);
    return 0;
}

void AudioAnalyzer::processBuffer(const float* buffer, unsigned int numFrames, double streamTime) {
    // 1. Calcolo RMS e Peak
    float sumSquares = 0.0f;
    float maxPeak = 0.0f;
    
    for (unsigned int i = 0; i < numFrames; i++) {
        float sample = buffer[i];
        sumSquares += sample * sample;
        float absSample = std::abs(sample);
        if (absSample > maxPeak) maxPeak = absSample;
    }
    float rms = std::sqrt(sumSquares / numFrames);

    // 2. BTrack - BEAT TRACKING E BPM
    bool isBeatNow = false;
    float detectedBpm = 0.0f;
    
    if (btrack) {
        if (btrackBuffer.size() != numFrames) btrackBuffer.resize(numFrames);
        for (unsigned int i = 0; i < numFrames; i++) {
            btrackBuffer[i] = static_cast<double>(buffer[i]); // Cast da float a double
        }
        
        btrack->processAudioFrame(btrackBuffer.data());
        isBeatNow = btrack->beatDueInCurrentFrame();
        detectedBpm = static_cast<float>(btrack->getCurrentTempoEstimate());
    }

    // 3. Calcolo Bande Spettrali tramite FFT
    unsigned int fftSize = 2048; 
    while (fftSize < numFrames) fftSize <<= 1;
    
    std::vector<Complex> fftData(fftSize, 0.0f);
    for (unsigned int i = 0; i < numFrames; i++) {
        float window = 0.5f * (1.0f - std::cos(2.0f * PI * i / (numFrames > 1 ? numFrames - 1 : 1)));
        fftData[i] = buffer[i] * window;
    }
    computeFFT(fftData);

    float flux = 0.0f;
    float binResolution = static_cast<float>(sampleRate) / fftSize;
    float energies[6] = {0}; 
    int binCounts[6] = {0};

    if (previousMagnitudes.size() != fftSize) previousMagnitudes.assign(fftSize, 0.0f);

    for (unsigned int i = 1; i < fftSize / 2; i++) {
        float magnitude = (std::abs(fftData[i]) / (fftSize / 2.0f)) * 2.0f; 
        float freq = i * binResolution;
        
        if      (freq < 60.0f)   { energies[0] += magnitude; binCounts[0]++; }
        else if (freq < 250.0f)  { energies[1] += magnitude; binCounts[1]++; }
        else if (freq < 500.0f)  { energies[2] += magnitude; binCounts[2]++; }
        else if (freq < 2000.0f) { energies[3] += magnitude; binCounts[3]++; }
        else if (freq < 4000.0f) { energies[4] += magnitude; binCounts[4]++; }
        else                     { energies[5] += magnitude; binCounts[5]++; }

        float diff = magnitude - previousMagnitudes[i];
        if (diff > 0.0f) flux += diff;
        previousMagnitudes[i] = magnitude;
    }

    if (flux > maxFlux) {
        maxFlux = flux;
    } else {
        maxFlux *= 0.9999f; 
        if (maxFlux < 0.0001f) maxFlux = 0.0001f; 
    }
    float normalizedFlux = std::min(flux / maxFlux, 1.0f);

    // 4. Finestra Temporale ed Aggregazione Generale
    InstantBands instant = {0, 0, 0, 0, 0, 0, 0, 0};
    float avgEnergies[6];
    
    for (int i = 0; i < 6; i++) {
        avgEnergies[i] = binCounts[i] > 0 ? (energies[i] / binCounts[i]) : 0.0f;
        if (avgEnergies[i] > maxBandEnergies[i]) {
            maxBandEnergies[i] = avgEnergies[i];
        } else {
            maxBandEnergies[i] *= 0.9999f; 
            if (maxBandEnergies[i] < 0.0001f) maxBandEnergies[i] = 0.0001f; 
        }
    }

    instant.subBass = avgEnergies[0] / maxBandEnergies[0];
    instant.bass    = avgEnergies[1] / maxBandEnergies[1];
    instant.lowMid  = avgEnergies[2] / maxBandEnergies[2];
    instant.mid     = avgEnergies[3] / maxBandEnergies[3];
    instant.highMid = avgEnergies[4] / maxBandEnergies[4];
    instant.high    = avgEnergies[5] / maxBandEnergies[5];
    instant.rms     = rms;
    instant.flux    = normalizedFlux;

    bandHistory.push_back(instant);
    if (bandHistory.size() > 1000) bandHistory.pop_front();

    float activeBpm = (detectedBpm > 30.0f) ? detectedBpm : 120.0f; 
    float timeWindowSec = (60.0f / activeBpm) * 4.0f; 
    float bufferDurationSec = static_cast<float>(numFrames) / sampleRate;
    
    int framesInWindow = static_cast<int>(timeWindowSec / bufferDurationSec);
    framesInWindow = std::min(framesInWindow, static_cast<int>(bandHistory.size()));
    framesInWindow = std::max(framesInWindow, 2); 

    InstantBands aggregated = {0, 0, 0, 0, 0, 0, 0, 0};
    float sumWeights = 0.0f;
    float recentRms = 0.0f, olderRms = 0.0f;
    float recentW = 0.0f, olderW = 0.0f;
    int halfWindow = framesInWindow / 2;

    auto it = bandHistory.rbegin();
    for (int i = 0; i < framesInWindow; ++i, ++it) {
        float weight = 1.0f - (static_cast<float>(i) / framesInWindow);
        sumWeights += weight;
        
        aggregated.subBass += it->subBass * weight;
        aggregated.bass    += it->bass * weight;
        aggregated.lowMid  += it->lowMid * weight;
        aggregated.mid     += it->mid * weight;
        aggregated.highMid += it->highMid * weight;
        aggregated.high    += it->high * weight;
        aggregated.rms     += it->rms * weight;
        aggregated.flux    += it->flux * weight;

        if (i < halfWindow) {
            recentRms += it->rms * weight;
            recentW += weight;
        } else {
            olderRms += it->rms * weight;
            olderW += weight;
        }
    }

    if (sumWeights > 0.0f) {
        aggregated.subBass /= sumWeights;
        aggregated.bass    /= sumWeights;
        aggregated.lowMid  /= sumWeights;
        aggregated.mid     /= sumWeights;
        aggregated.highMid /= sumWeights;
        aggregated.high    /= sumWeights;
        aggregated.rms     /= sumWeights;
        aggregated.flux    /= sumWeights;
    }

    float avgRecent = (recentW > 0.0f) ? (recentRms / recentW) : aggregated.rms;
    float avgOlder  = (olderW > 0.0f) ? (olderRms / olderW) : aggregated.rms;
    
    float rawDelta = (avgRecent - avgOlder) * 10.0f;
    smoothedDelta = (smoothedDelta * 0.90f) + (std::max(-1.0f, std::min(rawDelta, 1.0f)) * 0.10f);

    // 5. UPDATE METRICHE THREAD-SAFE
    if (metricsMutex.try_lock()) {
        currentMetrics.peak = maxPeak; 
        currentMetrics.bpm = detectedBpm;          // <--- BPM forniti da BTrack
        if (isBeatNow) currentMetrics.isBeat = true; // <--- Rilevamento preciso del colpo
        currentMetrics.deltaIntensity = smoothedDelta;
        
        currentMetrics.rms = aggregated.rms;
        currentMetrics.spectralFlux = aggregated.flux;
        currentMetrics.bandSubBass = aggregated.subBass;
        currentMetrics.bandBass    = aggregated.bass;
        currentMetrics.bandLowMid  = aggregated.lowMid;
        currentMetrics.bandMid     = aggregated.mid;
        currentMetrics.bandHighMid = aggregated.highMid;
        currentMetrics.bandHigh    = aggregated.high;
        metricsMutex.unlock();
    }
}