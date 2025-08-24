#include "AbletonLinkManager.h"

int PHRASE_LENGTH = 0;
int BPM = 0;

std::shared_ptr<ableton::Link> loadAbletonLink(const AppConfig& config) {
    std::cout << "Initializing Ableton Link..." << std::endl;

    PHRASE_LENGTH = config.phraseLength;
    BPM = config.default_bpm;
    std::shared_ptr<ableton::Link> link = std::make_shared<ableton::Link>(PHRASE_LENGTH);

    std::cout << "Connecting to Ableton Link session..." << std::endl;

    // Enable the Link session to join the local network.
    link->enable(true);
    link->setTempoCallback([](double newTempo) {
        // This code will be executed whenever the tempo changes
        std::cout << "Tempo changed to: " << newTempo << " BPM\n";
        BPM = newTempo;
    });

    return link;
}
