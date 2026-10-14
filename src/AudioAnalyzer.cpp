#include "AudioAnalyzer.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>

const double PI = 3.14159265358979323846;
typedef std::complex<float> Complex;

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

AudioAnalyzer::AudioAnalyzer() {
    bandHistory.clear();
}

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
        std::cerr << "[AUDIO] Nessun dispositivo audio trovato." << std::endl;
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
    
    try {
        if (adc.isStreamRunning()) adc.stopStream();
        if (adc.isStreamOpen()) adc.closeStream();

        if (adc.openStream(nullptr, &parameters, RTAUDIO_FLOAT32, sampleRate, &bufferFrames, &AudioAnalyzer::audioCallback, this) != RTAUDIO_NO_ERROR) {
            std::cerr << "[AUDIO] Errore openStream: " << adc.getErrorText() << std::endl;
            return false;
        }
        
        btrack = std::make_unique<BTrack>(bufferFrames, bufferFrames * 2);
        btrackBuffer.resize(bufferFrames, 0.0);
        
        if (adc.startStream() != RTAUDIO_NO_ERROR) {
            std::cerr << "[AUDIO] Errore startStream: " << adc.getErrorText() << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[AUDIO] Eccezione critica RtAudio: " << e.what() << std::endl;
        return false;
    }

    return true;
}

void AudioAnalyzer::stop() {
    try {
        if (adc.isStreamRunning()) {
            adc.stopStream(); 
        }
        if (adc.isStreamOpen()) {
            adc.closeStream(); 
        }
    } catch (const std::exception& e) {
        std::cerr << "[AUDIO] Eccezione durante lo stop: " << e.what() << std::endl;
    }
}

AudioMetrics AudioAnalyzer::getMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    AudioMetrics copy = currentMetrics;
    currentMetrics.isBeat = false; 
    return copy;
}

int AudioAnalyzer::audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                                 double streamTime, RtAudioStreamStatus status, void* userData) {
    if (inputBuffer == nullptr || userData == nullptr) return 0;
    
    AudioAnalyzer* analyzer = static_cast<AudioAnalyzer*>(userData);
    analyzer->processBuffer(static_cast<float*>(inputBuffer), nBufferFrames, streamTime);
    return 0;
}

// ... inizio di processBuffer ...
void AudioAnalyzer::processBuffer(const float* buffer, unsigned int numFrames, double streamTime) {
    if (numFrames == 0) return;

    float sumSquares = 0.0f;
    float maxPeak = 0.0f;
    
    for (unsigned int i = 0; i < numFrames; i++) {
        float sample = buffer[i];
        sumSquares += sample * sample;
        float absSample = std::abs(sample);
        if (absSample > maxPeak) maxPeak = absSample;
    }
    float rms = std::sqrt(sumSquares / numFrames);

    bool isBeatNow = false;
    float detectedBpm = 0.0f;
    
    if (btrack) {
        if (btrackBuffer.size() != numFrames) {
            btrackBuffer.resize(numFrames);
        }
        for (unsigned int i = 0; i < numFrames; i++) {
            btrackBuffer[i] = static_cast<double>(buffer[i]);
        }
        btrack->processAudioFrame(btrackBuffer.data());
        isBeatNow = btrack->beatDueInCurrentFrame();
        detectedBpm = static_cast<float>(btrack->getCurrentTempoEstimate());
    }

    unsigned int fftSize = 2048; 
    while (fftSize < numFrames) fftSize <<= 1;
    
    std::vector<Complex> fftData(fftSize, 0.0f);
    for (unsigned int i = 0; i < numFrames; i++) {
        float window = 0.5f * (1.0f - std::cos(2.0f * PI * i / (numFrames > 1 ? numFrames - 1 : 1)));
        fftData[i] = buffer[i] * window;
    }
    computeFFT(fftData);

    float rawFlux = 0.0f;
    float binResolution = static_cast<float>(sampleRate) / fftSize;
    float energies[6] = {0}; 
    int binCounts[6] = {0};

    if (previousMagnitudes.size() != fftSize) {
        previousMagnitudes.assign(fftSize, 0.0f);
    }

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
        if (diff > 0.0f) rawFlux += diff;
        previousMagnitudes[i] = magnitude;
    }

    InstantBands instant = {0, 0, 0, 0, 0, 0, 0, 0};
    float avgEnergies[6];
    
    for (int i = 0; i < 6; i++) {
        avgEnergies[i] = binCounts[i] > 0 ? (energies[i] / binCounts[i]) : 0.0f;
        // Aggiorna il picco assoluto senza decadimento
        if (avgEnergies[i] > maxBandEnergies[i]) {
            maxBandEnergies[i] = avgEnergies[i];
        }
    }

    instant.subBass = avgEnergies[0] / maxBandEnergies[0];
    instant.bass    = avgEnergies[1] / maxBandEnergies[1];
    instant.lowMid  = avgEnergies[2] / maxBandEnergies[2];
    instant.mid     = avgEnergies[3] / maxBandEnergies[3];
    instant.highMid = avgEnergies[4] / maxBandEnergies[4];
    instant.high    = avgEnergies[5] / maxBandEnergies[5];
    instant.rms     = rms;
    instant.rawFlux = rawFlux; 

    bandHistory.push_back(instant);
    if (bandHistory.size() > 1000) bandHistory.pop_front();

    // ---------------------------------------------------------
    // AGGREGAZIONE (Finestra veloce) E AUTO-SCALING GLOBALE
    // ---------------------------------------------------------
    float activeBpm = (detectedBpm > 30.0f) ? detectedBpm : 120.0f; 
    float beatDurationSec = 60.0f / activeBpm;
    float bufferDurationSec = static_cast<float>(numFrames) / sampleRate;
    
    int framesInWindow = static_cast<int>((beatDurationSec * 4.0f) / bufferDurationSec);
    framesInWindow = std::clamp(framesInWindow, 2, static_cast<int>(bandHistory.size()));

    InstantBands aggregated = {0, 0, 0, 0, 0, 0, 0, 0};
    float sumWeights = 0.0f;

    auto it = bandHistory.rbegin();
    for (int i = 0; i < framesInWindow; ++i, ++it) {
        float weight = 1.0f - (static_cast<float>(i) / framesInWindow);
        sumWeights += weight;
        aggregated.subBass  += it->subBass * weight;
        aggregated.bass     += it->bass * weight;
        aggregated.lowMid   += it->lowMid * weight;
        aggregated.mid      += it->mid * weight;
        aggregated.highMid  += it->highMid * weight;
        aggregated.high     += it->high * weight;
        aggregated.rms      += it->rms * weight;
        aggregated.rawFlux  += it->rawFlux * weight; 
    }

    if (sumWeights > 0.0f) {
        aggregated.subBass  /= sumWeights;
        aggregated.bass     /= sumWeights;
        aggregated.lowMid   /= sumWeights;
        aggregated.mid      /= sumWeights;
        aggregated.highMid  /= sumWeights;
        aggregated.high     /= sumWeights;
        aggregated.rms      /= sumWeights;
        aggregated.rawFlux  /= sumWeights; 
    }

    // --- AUTO-SCALING GLOBALE SUI PICCHI ASSOLUTI (No decadimento) ---
    
    if (aggregated.rms > globalMaxRms) globalMaxRms = aggregated.rms;
    float normRms = std::clamp(aggregated.rms / globalMaxRms, 0.0f, 1.0f);

    if (aggregated.subBass > globalMaxSubBass) globalMaxSubBass = aggregated.subBass;
    float normSubBass = std::clamp(aggregated.subBass / globalMaxSubBass, 0.0f, 1.0f);

    if (aggregated.bass > globalMaxBass) globalMaxBass = aggregated.bass;
    float normBass = std::clamp(aggregated.bass / globalMaxBass, 0.0f, 1.0f);

    if (aggregated.highMid > globalMaxHighMid) globalMaxHighMid = aggregated.highMid;
    float normHighMid = std::clamp(aggregated.highMid / globalMaxHighMid, 0.0f, 1.0f);

    if (aggregated.rawFlux > globalMaxFlux) globalMaxFlux = aggregated.rawFlux;
    float normFlux = std::clamp(aggregated.rawFlux / globalMaxFlux, 0.0f, 1.0f);

    if (metricsMutex.try_lock()) {
        currentMetrics.peak = maxPeak; 
        currentMetrics.bpm = detectedBpm;          
        if (isBeatNow) currentMetrics.isBeat = true; 
        
        currentMetrics.rms = normRms;
        currentMetrics.bandSubBass = normSubBass;
        currentMetrics.bandBass = normBass;
        currentMetrics.bandHighMid = normHighMid;
        currentMetrics.spectralFlux = normFlux;
        
        currentMetrics.bandLowMid = aggregated.lowMid;
        currentMetrics.bandMid = aggregated.mid;
        currentMetrics.bandHigh = aggregated.high;
        
        metricsMutex.unlock();
    }
}