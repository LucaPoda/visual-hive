#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <netinet/in.h>

/**
 * @brief Gestore per la trasmissione Open Sound Control (OSC) su UDP.
 * Permette di sincronizzare luci e controller esterni (come QLC+) inviando:
 * 1. Valore float/int del BPM per il Master Clock / Speed Dials.
 * 2. Impulsi "TAP" (1.0f on-beat, 0.0f release) per sincronizzare gli Speed Dial di QLC+.
 */
class OscManager {
public:
    OscManager();
    ~OscManager();

    // Inizializza il socket e i parametri
    bool init(const std::string& ip = "127.0.0.1", int port = 7700,
              const std::string& bpmAddr = "/bpm", const std::string& tapAddr = "/tap",
              bool enable = true, int pulseDuration = 35);

    void setEnabled(bool enable);
    bool isEnabled() const { return enabled.load(); }

    void setSendTapOnBeat(bool enable) { sendTapOnBeat.store(enable); }
    bool isSendTapOnBeat() const { return sendTapOnBeat.load(); }

    void setPulseDurationMs(int ms) { pulseDurationMs.store(ms); }
    int getPulseDurationMs() const { return pulseDurationMs.load(); }

    // Aggiorna destinazione IP/Port e indirizzi OSC
    bool updateTarget(const std::string& ip, int port);
    void updateAddresses(const std::string& bpmAddr, const std::string& tapAddr);

    // Invio messaggi OSC
    bool sendBpm(float bpm);
    bool sendTap(float value = 1.0f);
    bool sendTapRelease();

    // Gestione evento beat da AudioAnalyzer (tempo reale a bassa latenza)
    void onBeat(float calibratedBpm);

    // Chiamata ad ogni frame audio buffer (~11.6ms) per gestire la durata del rilascio TAP
    void processAudioFrame();

    // Chiamata dal thread UI (fallback rilascio impulso e animazioni)
    void update(double currentTime);

    // Trigger manuali dalla UI (per test o per apprendimento canali in QLC+ "Auto Detect")
    void triggerManualTap();
    void triggerManualBpm(float bpm);

    // Statistiche e stato per ImGui
    std::string getTargetIp() const;
    int getTargetPort() const;
    std::string getBpmAddress() const;
    std::string getTapAddress() const;
    uint64_t getPacketsSent() const { return packetsSent.load(); }
    float getLastSentBpm() const { return lastSentBpm.load(); }
    bool isBeatFlashActive() const;

private:
    bool initSocket();
    void closeSocket();
    bool sendRaw(const uint8_t* data, size_t size);

    static void appendPaddedString(std::vector<uint8_t>& buffer, const std::string& str);
    static uint32_t floatToNetwork(float f);
    static uint32_t intToNetwork(int32_t i);

    bool sendOscFloat(const std::string& address, float value);
    bool sendOscInt(const std::string& address, int32_t value);

private:
    int sockfd = -1;
    sockaddr_in destAddr{};
    mutable std::mutex socketMutex;
    mutable std::mutex configMutex;

    std::atomic<bool> enabled{true};
    std::atomic<bool> sendTapOnBeat{true};
    std::atomic<int> pulseDurationMs{35};

    std::string targetIp = "127.0.0.1";
    int targetPort = 7700;
    std::string bpmAddress = "/bpm";
    std::string tapAddress = "/tap";

    std::atomic<uint64_t> packetsSent{0};
    std::atomic<float> lastSentBpm{0.0f};

    // Gestione impulso TAP (high -> low)
    std::atomic<int> releaseCountdownBuffers{-1};
    std::chrono::steady_clock::time_point lastTapSteadyTime{};
    std::atomic<bool> isTapActive{false};
};
