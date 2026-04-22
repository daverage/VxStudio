#pragma once

#include "../../../framework/VxStudioCorrectiveStage.h"

namespace vxsuite::cleanup {

class Dsp {
public:
    using Params = vxsuite::corrective::SharedParams;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void setParams(const Params& params);
    void reset();
    void processCorrective(juce::AudioBuffer<float>& buffer);

    float getDeMudActivity() const noexcept { return corrective.getDeMudActivity(); }
    float getDeEssActivity() const noexcept { return corrective.getDeEssActivity(); }
    float getBreathActivity() const noexcept { return corrective.getBreathActivity(); }
    float getPlosiveActivity() const noexcept { return corrective.getPlosiveActivity(); }
    float getTroubleActivity() const noexcept { return corrective.getTroubleActivity(); }

private:
    vxsuite::corrective::CorrectiveStage corrective;
};

} // namespace vxsuite::cleanup
