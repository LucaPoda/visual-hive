// AudioAnalyzer.cpp
#include "AudioAnalyzer.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <cstdio>
#include <cstdint>

const double PI = 3.14159265358979323846;
typedef std::complex<float> Complex;

// Header Standard WAV (32-bit Float)
struct WavHeader {
    char riff[4] = {'R','I','F','F'};
    uint32_t fileSize;
    char wave[4] = {'W','A','V','E'};
    char fmt[4] = {'f','m','t',' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 3; // 3 = IEEE Float
    uint16_t numChannels = 1; // Mono
    uint32_t sampleRate = 44100;
    uint32_t byteRate = 44100 * 1 * 4;
    uint16_t blockAlign = 4;
    uint16_t bitsPerSample = 32;
    char data[4] = {'d','a','t','a'};
    uint32_t dataSize;
};

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
    // empty
}

AudioAnalyzer::~AudioAnalyzer() {
    stopRecording();
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

bool AudioAnalyzer::start(unsigned int deviceId, unsigned int configSampleRate, unsigned int configBufferFrames) {
    if (adc.getDeviceCount() < 1) {
        std::cerr << "[AUDIO] Nessun dispositivo audio trovato." << std::endl;
        return false;
    }

    this->sampleRate = configSampleRate;
    unsigned int bufferFrames = configBufferFrames;

    unsigned int selectedId = (deviceId == 0) ? adc.getDefaultInputDevice() : deviceId;
    RtAudio::DeviceInfo info = adc.getDeviceInfo(selectedId);

    if (info.inputChannels < 1) {
        auto devices = getInputDevices();
        if (devices.empty()) return false;
        selectedId = devices[0].id;
        info = adc.getDeviceInfo(selectedId);
    }

    // --- CONTROLLO HARDCODE STEREO ---
    if (info.inputChannels < 2) {
        std::cerr << "[AUDIO] ERRORE CRITICO: Il dispositivo '" << info.name 
                  << "' ha solo " << info.inputChannels 
                  << " canale/i. Il sistema richiede un ingresso STEREO per la registrazione." << std::endl;
        return false; // Rifiuta di partire
    }

    currentDeviceId = selectedId;

    RtAudio::StreamParameters parameters;
    parameters.deviceId = currentDeviceId;
    parameters.nChannels = 2; // Forzato a 2 canali
    parameters.firstChannel = 0;
    
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
        if (adc.isStreamRunning()) adc.stopStream(); 
        if (adc.isStreamOpen()) adc.closeStream(); 
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
    float* inBuffer = static_cast<float*>(inputBuffer);

    if (analyzer->isRecording) {
        std::vector<float> block(inBuffer, inBuffer + (nBufferFrames * 2));
        {
            std::lock_guard<std::mutex> lock(analyzer->recordMutex);
            analyzer->recordQueue.push(std::move(block));
        }
        analyzer->recordCV.notify_one();
    }

    float peakL = 0.0f;
    float peakR = 0.0f;
    std::vector<float> monoBuffer(nBufferFrames);
    
    for (unsigned int i = 0; i < nBufferFrames; ++i) {
        float l = inBuffer[i * 2];
        float r = inBuffer[i * 2 + 1];
        
        // Estrazione picchi separati
        if (std::abs(l) > peakL) peakL = std::abs(l);
        if (std::abs(r) > peakR) peakR = std::abs(r);
        
        // Downmix mono
        monoBuffer[i] = (l + r) * 0.5f;
    }

    // Passiamo i picchi estratti
    analyzer->processBuffer(monoBuffer.data(), nBufferFrames, streamTime, peakL, peakR);
    return 0;
}

void AudioAnalyzer::processBuffer(const float* buffer, unsigned int numFrames, double streamTime, float peakL, float peakR) {
    if (numFrames == 0) return;

    AudioMetrics metrics;
    metrics.peakL = peakL;
    metrics.peakR = peakR;

    float sumSquares = 0.0f;
    for (unsigned int i = 0; i < numFrames; i++) {
        sumSquares += buffer[i] * buffer[i];
    }
    metrics.rms = std::sqrt(sumSquares / numFrames);
    
    if (btrack) {
        if (btrackBuffer.size() != numFrames) btrackBuffer.resize(numFrames);
        for (unsigned int i = 0; i < numFrames; i++) btrackBuffer[i] = static_cast<double>(buffer[i]);
        btrack->processAudioFrame(btrackBuffer.data());
        metrics.isBeat = btrack->beatDueInCurrentFrame();
        metrics.bpm = static_cast<float>(btrack->getCurrentTempoEstimate());
    }

    unsigned int fftSize = 2048; 
    while (fftSize < numFrames) fftSize <<= 1;
    
    std::vector<Complex> fftData(fftSize, 0.0f);
    for (unsigned int i = 0; i < numFrames; i++) {
        float window = 0.5f * (1.0f - std::cos(2.0f * PI * i / (numFrames > 1 ? numFrames - 1 : 1)));
        fftData[i] = buffer[i] * window;
    }
    computeFFT(fftData);

    if (previousMagnitudes.size() != fftSize) previousMagnitudes.assign(fftSize, 0.0f);
    
    std::vector<float> fftMagnitudes(fftSize / 2, 0.0f);
    float binResolution = static_cast<float>(sampleRate) / fftSize;

    metrics.spectralFlux = 0.0f;
    for (unsigned int i = 1; i < fftSize / 2; i++) {
        float magnitude = (std::abs(fftData[i]) / (fftSize / 2.0f)) * 2.0f; 
        fftMagnitudes[i] = magnitude;
        float diff = magnitude - previousMagnitudes[i];
        if (diff > 0.0f) metrics.spectralFlux += diff;
        previousMagnitudes[i] = magnitude;
    }

    int zeroCrossings = 0;
    float peakAbsolute = 0.0f;
    for (size_t i = 1; i < numFrames; ++i) {
        if ((buffer[i] >= 0 && buffer[i - 1] < 0) || (buffer[i] < 0 && buffer[i - 1] >= 0)) zeroCrossings++;
        if (std::abs(buffer[i]) > peakAbsolute) peakAbsolute = std::abs(buffer[i]);
    }
    metrics.zcr = static_cast<float>(zeroCrossings) / numFrames; 

    // IL CREST FACTOR in una traccia EDM va da 1.0 (Sinusoide Pura) a circa 15.0 (Transiente secco)
    float rawCrest = (metrics.rms > 0.001f) ? (peakAbsolute / metrics.rms) : 1.0f;
    metrics.crestFactor = std::clamp((rawCrest - 1.0f) / 10.0f, 0.0f, 1.0f);

    float totalMagnitude = 0.0f;
    float weightedSum = 0.0f;
    for (size_t i = 0; i < fftSize / 2; ++i) {
        totalMagnitude += fftMagnitudes[i];
        weightedSum += i * fftMagnitudes[i];
    }

    // UNICO BLOCCO PER LE FEATURE SPETTRALI (Soglia abbassata e Valori Raw per lo Z-Score)
    if (totalMagnitude > 1e-6f) {
        // 1. CENTROIDE (Valore raw in Hz)
        float centroidBin = weightedSum / totalMagnitude;
        metrics.spectralCentroid = centroidBin * binResolution;

        // 2. SPREAD (Valore raw in Hz)
        float spreadSum = 0.0f;
        for (size_t i = 0; i < fftSize / 2; ++i) {
            float diff = (float)i - centroidBin;
            spreadSum += (diff * diff) * fftMagnitudes[i];
        }
        metrics.spectralSpread = std::sqrt(spreadSum / totalMagnitude) * binResolution;

        // 3. ROLLOFF (Valore raw in Hz)
        float rolloffThreshold = totalMagnitude * 0.85f;
        float cumulativeMagnitude = 0.0f;
        for (size_t i = 0; i < fftSize / 2; ++i) {
            cumulativeMagnitude += fftMagnitudes[i];
            if (cumulativeMagnitude >= rolloffThreshold) {
                metrics.spectralRolloff = i * binResolution;
                break;
            }
        }
    } else {
        metrics.spectralCentroid = 0.0f;
        metrics.spectralSpread = 0.0f;
        metrics.spectralRolloff = 0.0f;
    }

    // COMPRESSIONE BANDE FFT (Square Root + Gain)
    auto calcBandEnergy = [&](float freqStart, float freqEnd) -> float {
        int startBin = std::max(0, static_cast<int>(freqStart / binResolution));
        int endBin = std::min(static_cast<int>(fftSize / 2), static_cast<int>(freqEnd / binResolution));
        float energy = 0.0f;
        for (int i = startBin; i < endBin; ++i) energy += fftMagnitudes[i];
        
        float avgEnergy = energy / std::max(1, endBin - startBin); 
        return std::clamp(std::sqrt(avgEnergy) * 5.0f, 0.0f, 1.0f); 
    };

    metrics.bandSubBass    = calcBandEnergy(20.0f, 60.0f);
    metrics.bandBass       = calcBandEnergy(60.0f, 250.0f);
    metrics.bandLowMid     = calcBandEnergy(250.0f, 500.0f);
    metrics.bandMid        = calcBandEnergy(500.0f, 2000.0f);
    metrics.bandHighMid    = calcBandEnergy(2000.0f, 4000.0f);
    metrics.bandPresence   = calcBandEnergy(4000.0f, 6000.0f);
    metrics.bandBrilliance = calcBandEnergy(6000.0f, 20000.0f);

    std::lock_guard<std::mutex> lock(metricsMutex);
    currentMetrics = metrics;
}

// =======================================================
// RECORDING ASINCRONO MULTITHREAD
// =======================================================
void AudioAnalyzer::startRecording(const std::string& baseFilename) {
    if (isRecording) return;
    
    std::filesystem::create_directories("recordings");
    recordBasePath = baseFilename;
    recordPartIndex = 1;
    totalRecordedSamples = 0;
    
    stopWriter = false;
    while(!recordQueue.empty()) recordQueue.pop();
    
    openNextWavFile();
    
    isRecording = true;
    writerThread = std::thread(&AudioAnalyzer::writerLoop, this);
}

void AudioAnalyzer::stopRecording() {
    if (!isRecording) return;
    isRecording = false;
    stopWriter = true;
    recordCV.notify_one();
    if (writerThread.joinable()) {
        writerThread.join();
    }
    closeCurrentWavFile();
}

void AudioAnalyzer::openNextWavFile() {
    if (currentWavFile.is_open()) {
        closeCurrentWavFile();
    }
    
    currentRecordFile = "recordings/" + recordBasePath + "_part" + std::to_string(recordPartIndex++) + ".wav";
    currentWavFile.open(currentRecordFile, std::ios::binary);
    
    WavHeader header;
    header.numChannels = 2; // Hardcode Stereo
    header.byteRate = sampleRate * 2 * 4; 
    header.blockAlign = 8; // 2 canali * 4 byte
    
    currentWavFile.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
    currentFileSamples = 0;
}

void AudioAnalyzer::closeCurrentWavFile() {
    if (currentWavFile.is_open()) {
        currentWavFile.seekp(0, std::ios::beg);
        WavHeader header;
        header.sampleRate = sampleRate;
        header.numChannels = 2; // Hardcode Stereo
        header.byteRate = sampleRate * 2 * 4;
        header.blockAlign = 8;
        
        header.dataSize = currentFileSamples * 4;
        header.fileSize = 36 + header.dataSize;
        
        currentWavFile.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
        currentWavFile.close();
    }
}

void AudioAnalyzer::writerLoop() {
    // 60 minuti * 2 canali (per calcolare il limite di float)
    const uint32_t CHUNK_SAMPLE_LIMIT = sampleRate * 60 * 60; 
    
    while (!stopWriter || !recordQueue.empty()) {
        std::vector<float> block;
        {
            std::unique_lock<std::mutex> lock(recordMutex);
            recordCV.wait(lock, [this] { return !recordQueue.empty() || stopWriter; });
            
            if (recordQueue.empty()) continue;
            
            block = std::move(recordQueue.front());
            recordQueue.pop();
        }
        
        if (currentWavFile.is_open()) {
            currentWavFile.write(reinterpret_cast<const char*>(block.data()), block.size() * sizeof(float));
            currentFileSamples += block.size();
            totalRecordedSamples += block.size();
            
            if (currentFileSamples >= CHUNK_SAMPLE_LIMIT) {
                openNextWavFile();
            }
        }
    }
}

std::string AudioAnalyzer::getRecordingTimeFormatted() const {
    // Dividiamo per 2 per tornare ai sample rate reali e calcolare i secondi
    uint32_t seconds = totalRecordedSamples / (sampleRate * 2);
    uint32_t m = seconds / 60;
    uint32_t s = seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return std::string(buf);
}