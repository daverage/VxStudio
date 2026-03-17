#pragma once

namespace vxsuite::polish {

struct SharedParams {
    float deMud = 0.0f;
    float deEss = 0.0f;
    float breath = 0.0f;
    float plosive = 0.0f;
    float compress = 0.0f;
    float troubleSmooth = 0.0f;
    float limit = 0.0f;
    float recovery = 0.0f;
    float smartGain = 0.5f;
    float voicePreserve = 0.75f;
    float denoiseAmount = 0.0f;
    float artifactRisk = 0.0f;
    float compSidechainBoostDb = 0.0f;
    int contentMode = 0;
    float speechLoudnessDb = -30.0f;
    float proximityContext = 0.0f;
    float speechPresence = 0.5f;
    float noiseFloorDb = -80.0f;
    bool hpfOn = false;
    bool hiShelfOn = false;
};

} // namespace vxsuite::polish
