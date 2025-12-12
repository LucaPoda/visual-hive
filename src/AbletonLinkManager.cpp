#include "AbletonLinkManager.h"

int PHRASE_LENGTH = 0;
int BPM = 0;

ableton::Link * loadAbletonLink(const AppConfig& config) {
    std::cout << "[INFO] Initializing Ableton Link..." << std::endl;

    PHRASE_LENGTH = config.phraseLength;
    BPM = config.default_bpm;
    ableton::Link * link = new ableton::Link(PHRASE_LENGTH);

    std::cout << "[INFO] Connecting to Ableton Link session..." << std::endl;

    // Enable the Link session to join the local network.
    link->enable(true);
    link->setTempoCallback([](double newTempo) {
        // This code will be executed whenever the tempo changes
        // std::cout << "[INFO] Tempo changed to: " << newTempo << " BPM\n";
        BPM = newTempo;
    });

    return link;
}

void manualSync(ableton::Link * link) {
    auto now = std::chrono::microseconds(link->clock().micros());
    auto timeline = link->captureAppSessionState();
    double currentBeat = timeline.beatAtTime(now, 4);
    double phaseOffset = std::fmod(currentBeat, 1.0);
    double correctedBeat = currentBeat - phaseOffset;
    timeline.forceBeatAtTime(correctedBeat, now, 4);

    std::cout << std::endl << "[INFO] Manual sync triggered. Corrected beat: " << correctedBeat << std::endl;
}
