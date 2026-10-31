#include "OscManager.h"
#include "Logger.h"
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>

OscManager::OscManager() {
    lastTapSteadyTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
}

OscManager::~OscManager() {
    closeSocket();
}

bool OscManager::init(const std::string& ip, int port,
                      const std::string& bpmAddr, const std::string& tapAddr,
                      bool enable, int pulseDuration) {
    {
        std::lock_guard<std::mutex> lock(configMutex);
        targetIp = ip;
        targetPort = port;
        bpmAddress = bpmAddr.empty() ? "/bpm" : bpmAddr;
        tapAddress = tapAddr.empty() ? "/tap" : tapAddr;
    }
    enabled.store(enable);
    pulseDurationMs.store(pulseDuration > 10 ? pulseDuration : 35);
    
    return initSocket();
}

void OscManager::setEnabled(bool enable) {
    enabled.store(enable);
    LOG_INFO("OscManager", std::string("Trasmissione OSC ") + (enable ? "ABILITATA" : "DISABILITATA"));
}

bool OscManager::updateTarget(const std::string& ip, int port) {
    {
        std::lock_guard<std::mutex> lock(configMutex);
        targetIp = ip;
        targetPort = port;
    }
    LOG_INFO("OscManager", "Aggiornata destinazione OSC: " + ip + ":" + std::to_string(port));
    return initSocket();
}

void OscManager::updateAddresses(const std::string& bpmAddr, const std::string& tapAddr) {
    std::lock_guard<std::mutex> lock(configMutex);
    if (!bpmAddr.empty()) bpmAddress = bpmAddr;
    if (!tapAddr.empty()) tapAddress = tapAddr;
    LOG_INFO("OscManager", "Aggiornati path OSC: BPM=" + bpmAddress + ", TAP=" + tapAddress);
}

bool OscManager::initSocket() {
    std::lock_guard<std::mutex> lock(socketMutex);
    closeSocket();

    sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        LOG_ERROR("OscManager", "Errore creazione socket UDP.");
        return false;
    }

    int broadcast = 1;
    ::setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    std::string currentIp;
    int currentPort;
    {
        std::lock_guard<std::mutex> cfgLock(configMutex);
        currentIp = targetIp;
        currentPort = targetPort;
    }

    std::memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(currentPort);
    if (::inet_pton(AF_INET, currentIp.c_str(), &destAddr.sin_addr) <= 0) {
        LOG_ERROR("OscManager", "IP destinazione non valido: " + currentIp);
        ::close(sockfd);
        sockfd = -1;
        return false;
    }

    LOG_INFO("OscManager", "Socket UDP pronto per invio verso " + currentIp + ":" + std::to_string(currentPort));
    return true;
}

void OscManager::closeSocket() {
    if (sockfd >= 0) {
        ::close(sockfd);
        sockfd = -1;
    }
}

void OscManager::appendPaddedString(std::vector<uint8_t>& buffer, const std::string& str) {
    buffer.insert(buffer.end(), str.begin(), str.end());
    buffer.push_back('\0'); // Almeno un terminatore null
    while (buffer.size() % 4 != 0) {
        buffer.push_back('\0');
    }
}

uint32_t OscManager::floatToNetwork(float f) {
    uint32_t net;
    std::memcpy(&net, &f, sizeof(float));
    return htonl(net);
}

uint32_t OscManager::intToNetwork(int32_t i) {
    return htonl(static_cast<uint32_t>(i));
}

bool OscManager::sendRaw(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(socketMutex);
    if (sockfd < 0) return false;

    ssize_t sent = ::sendto(sockfd, data, size, 0, reinterpret_cast<struct sockaddr*>(&destAddr), sizeof(destAddr));
    if (sent > 0) {
        packetsSent++;
        return true;
    }
    return false;
}

bool OscManager::sendOscFloat(const std::string& address, float value) {
    if (!enabled.load() || sockfd < 0) return false;

    std::vector<uint8_t> buffer;
    buffer.reserve(address.size() + 16);
    appendPaddedString(buffer, address);
    appendPaddedString(buffer, ",f");

    uint32_t netVal = floatToNetwork(value);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&netVal);
    buffer.insert(buffer.end(), p, p + 4);

    return sendRaw(buffer.data(), buffer.size());
}

bool OscManager::sendOscInt(const std::string& address, int32_t value) {
    if (!enabled.load() || sockfd < 0) return false;

    std::vector<uint8_t> buffer;
    buffer.reserve(address.size() + 16);
    appendPaddedString(buffer, address);
    appendPaddedString(buffer, ",i");

    uint32_t netVal = intToNetwork(value);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&netVal);
    buffer.insert(buffer.end(), p, p + 4);

    return sendRaw(buffer.data(), buffer.size());
}

bool OscManager::sendBpm(float bpm) {
    if (!enabled.load() || bpm <= 0.0f) return false;
    
    std::string addr;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        addr = bpmAddress;
    }
    
    bool ok = sendOscFloat(addr, bpm);
    if (ok) {
        lastSentBpm.store(bpm);
    }
    return ok;
}

bool OscManager::sendTap(float value) {
    if (!enabled.load()) return false;

    std::string addr;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        addr = tapAddress;
    }

    bool ok = sendOscFloat(addr, value);
    if (ok && value > 0.0f) {
        lastTapSteadyTime = std::chrono::steady_clock::now();
        isTapActive.store(true);
    }
    return ok;
}

bool OscManager::sendTapRelease() {
    std::string addr;
    {
        std::lock_guard<std::mutex> lock(configMutex);
        addr = tapAddress;
    }
    return sendOscFloat(addr, 0.0f);
}

void OscManager::onBeat(float calibratedBpm) {
    if (!enabled.load()) return;

    // 1. Invio segnale TAP (pressione 1.0f) per Speed Dials
    if (sendTapOnBeat.load()) {
        sendTap(1.0f);
        // Calcola durata release in frame audio (~11.6ms a 512 campioni / 44.1kHz)
        int frames = std::max(1, pulseDurationMs.load() / 11);
        releaseCountdownBuffers.store(frames);
    }

    // 2. Invio valore BPM per il Clock Master di QLC+
    sendBpm(calibratedBpm);
}

void OscManager::processAudioFrame() {
    int cd = releaseCountdownBuffers.load();
    if (cd > 0) {
        cd--;
        releaseCountdownBuffers.store(cd);
        if (cd == 0) {
            sendTapRelease();
            isTapActive.store(false);
        }
    }
}

void OscManager::update(double /*currentTime*/) {
    // Controllo di fallback nel thread UI se il thread audio è fermo o per trigger manuale
    if (isTapActive.load()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTapSteadyTime).count();
        if (elapsedMs >= pulseDurationMs.load()) {
            sendTapRelease();
            isTapActive.store(false);
            releaseCountdownBuffers.store(-1);
        }
    }
}

void OscManager::triggerManualTap() {
    sendTap(1.0f);
    lastTapSteadyTime = std::chrono::steady_clock::now();
    isTapActive.store(true);
    int frames = std::max(1, pulseDurationMs.load() / 11);
    releaseCountdownBuffers.store(frames);
    LOG_INFO("OscManager", "Trigger TAP manuale inviato a QLC+");
}

void OscManager::triggerManualBpm(float bpm) {
    sendBpm(bpm);
    LOG_INFO("OscManager", "Invio forzato BPM: " + std::to_string(bpm));
}

std::string OscManager::getTargetIp() const {
    std::lock_guard<std::mutex> lock(configMutex);
    return targetIp;
}

int OscManager::getTargetPort() const {
    std::lock_guard<std::mutex> lock(configMutex);
    return targetPort;
}

std::string OscManager::getBpmAddress() const {
    std::lock_guard<std::mutex> lock(configMutex);
    return bpmAddress;
}

std::string OscManager::getTapAddress() const {
    std::lock_guard<std::mutex> lock(configMutex);
    return tapAddress;
}

bool OscManager::isBeatFlashActive() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTapSteadyTime).count();
    return elapsedMs < 120;
}
