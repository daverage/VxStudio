#include "VxCleanupDsp.h"

namespace vxsuite::cleanup {

void Dsp::prepare(double sampleRate, int, int numChannels) {
    corrective.prepare(sampleRate, numChannels);
}

void Dsp::setParams(const Params& params) {
    corrective.setParams(params);
}

void Dsp::reset() {
    corrective.reset();
}

void Dsp::processCorrective(juce::AudioBuffer<float>& buffer) {
    corrective.process(buffer);
}

} // namespace vxsuite::cleanup
