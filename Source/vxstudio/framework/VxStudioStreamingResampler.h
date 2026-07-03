#pragma once

#include "../../../ThirdParty/resampler/Resampler.hpp"

namespace vxsuite {

template <int inputChannelCount, int outputChannelCount>
using StreamingResampler = Resampler<inputChannelCount, outputChannelCount>;

} // namespace vxsuite
