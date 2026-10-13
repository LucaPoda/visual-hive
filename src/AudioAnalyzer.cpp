#include "AudioAnalyzer.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>

const double PI = 3.14159265358979323846;
typedef std::complex<float> Complex;

// Semplice FFT in-place (Cooley-Tukey)
void computeFFT(std::vector<Complex>& x) {
    size_t N = x.size();
    if (N <= 1) return;
    std::vector<Complex> even(N / 2), odd(N / 2);
    for (size_t i = 0; i < N / 2; i++) {
        even[i] = x[i * 2];
        odd[i] = x[i * 2 + 1];
    }
    computeFFT(even);
    computeFFT(odd);
    for (size_t k = 0; k < N / 2; k++) {
        Complex t = std::polar(1.0f, static_cast<float>(-2 * PI * k / N)) * odd[k];
        x[k] = even[k] + t;
        x[k + N / 2] = even[k] - t;
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

    currentDeviceId = (deviceId == 0) ? adc.getDefaultInputDevice() : deviceId;

    RtAudio::StreamParameters parameters;
    parameters.deviceId = currentDeviceId;
    parameters.nChannels = 1;
    parameters.firstChannel = 0;

    unsigned int bufferFrames = 512;
    
    if (adc.openStream(nullptr, &parameters, RTAUDIO_FLOAT32, sampleRate, &bufferFrames, &AudioAnalyzer::audioCallback, this) != RTAUDIO_NO_ERROR) {
        std::cerr << "[AUDIO] Errore openStream: " << adc.getErrorText() << "\n";
        return false;
    }
    
    if (adc.startStream() != RTAUDIO_NO_ERROR) {
        std::cerr << "[AUDIO] Errore startStream: " << adc.getErrorText() << "\n";
        return false;
    }

    std::cout << "[AUDIO] Stream avviato sul dispositivo " << parameters.deviceId << ".\n";
    return true;
}

void AudioAnalyzer::stop() {
    // Chiusura sicura dei thread per evitare segfaults
    if (adc.isStreamRunning()) {
        adc.stopStream(); 
    }
    if (adc.isStreamOpen()) {
        adc.closeStream(); 
    }
}

AudioMetrics AudioAnalyzer::getMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    return currentMetrics;
}

int AudioAnalyzer::audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                                 double streamTime, RtAudioStreamStatus status, void* userData) {
    if (status) std::cerr << "[AUDIO] Stream overflow/underflow!\n";
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

    // 2. Calcolo Spectral Flux (FFT) e 6 Bande (Feature KNN)
    unsigned int fftSize = 2048; 
    
    // SICUREZZA ANTI-SEGFAULT: Se il driver impone un buffer enorme, allarghiamo la FFT
    while (fftSize < numFrames) fftSize <<= 1;
    
    std::vector<Complex> fftData(fftSize, 0.0f);
    for (unsigned int i = 0; i < numFrames; i++) {
        // Evita divisioni per zero se numFrames dovesse impazzire
        float window = 0.5f * (1.0f - std::cos(2.0f * PI * i / (numFrames > 1 ? numFrames - 1 : 1)));
        fftData[i] = buffer[i] * window;
    }
    computeFFT(fftData);

    float flux = 0.0f;
    float lowFreqEnergy = 0.0f;
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
        
        if (freq >= 20.0f && freq <= 150.0f) lowFreqEnergy += magnitude; 
    }

    // 3. Normalizzazione del Flusso Spettrale
    if (flux > maxFlux) {
        maxFlux = flux;
    } else {
        maxFlux *= 0.9999f; 
        if (maxFlux < 0.0001f) maxFlux = 0.0001f; // Previene divisioni per zero/denormalizzazione
    }
    float normalizedFlux = std::min(flux / maxFlux, 1.0f);

    // 4. Rilevamento Transienti / BPM
    float currentBpm = currentMetrics.bpm;
    if (lowFreqEnergy > lastOnsetEnergy * 1.5f && lowFreqEnergy > 5.0f) { 
        peakTimes.push_back(streamTime);
        if (peakTimes.size() > 8) peakTimes.erase(peakTimes.begin()); 

        if (peakTimes.size() >= 2) {
            std::vector<double> intervals;
            for (size_t i = 1; i < peakTimes.size(); i++) {
                intervals.push_back(peakTimes[i] - peakTimes[i-1]);
            }
            std::sort(intervals.begin(), intervals.end());
            double medianInterval = intervals[intervals.size() / 2];
            if (medianInterval > 0.3 && medianInterval < 2.0) { 
                float estimatedBpm = static_cast<float>(60.0 / medianInterval);
                currentBpm = (currentBpm == 0.0f) ? estimatedBpm : (currentBpm * 0.8f + estimatedBpm * 0.2f);
            }
        }
    }
    lastOnsetEnergy = lowFreqEnergy;

    // 5. Finestra Temporale (4 Battiti) e Aggregazione Generale
    {
        // A. Calcolo e Normalizzazione Istantanea delle Bande
        InstantBands instant = {0, 0, 0, 0, 0, 0, 0, 0};
        float avgEnergies[6];
        
        for (int i = 0; i < 6; i++) {
            avgEnergies[i] = binCounts[i] > 0 ? (energies[i] / binCounts[i]) : 0.0f;
            if (avgEnergies[i] > maxBandEnergies[i]) {
                maxBandEnergies[i] = avgEnergies[i];
            } else {
                maxBandEnergies[i] *= 0.9999f; 
                // Sicurezza: non far scendere mai il massimo sotto lo zero assoluto
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

        // B. Dimensionamento Finestra Dinamica
        float activeBpm = (currentBpm > 30.0f) ? currentBpm : 120.0f; 
        float timeWindowSec = (60.0f / activeBpm) * 4.0f; 
        float bufferDurationSec = static_cast<float>(numFrames) / sampleRate;
        
        int framesInWindow = static_cast<int>(timeWindowSec / bufferDurationSec);
        framesInWindow = std::min(framesInWindow, static_cast<int>(bandHistory.size()));
        framesInWindow = std::max(framesInWindow, 2); 

        // C. Media Pesata Globale e Calcolo Trend (Delta)
        InstantBands aggregated = {0, 0, 0, 0, 0, 0, 0, 0};
        float sumWeights = 0.0f;
        
        // Variabili per il Delta (Metà recente vs Metà vecchia della finestra)
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
        rawDelta = std::max(-1.0f, std::min(rawDelta, 1.0f)); 
        
        smoothedDelta = (smoothedDelta * 0.90f) + (rawDelta * 0.10f);

        // D. Update
        std::lock_guard<std::mutex> lock(metricsMutex);
        currentMetrics.peak = maxPeak; 
        currentMetrics.bpm = currentBpm;
        currentMetrics.deltaIntensity = smoothedDelta;
        
        currentMetrics.rms = aggregated.rms;
        currentMetrics.spectralFlux = aggregated.flux;
        currentMetrics.bandSubBass = aggregated.subBass;
        currentMetrics.bandBass    = aggregated.bass;
        currentMetrics.bandLowMid  = aggregated.lowMid;
        currentMetrics.bandMid     = aggregated.mid;
        currentMetrics.bandHighMid = aggregated.highMid;
        currentMetrics.bandHigh    = aggregated.high;
    }
}