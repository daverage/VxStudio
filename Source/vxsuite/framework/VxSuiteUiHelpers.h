#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vxsuite {

void fitLabelFontToBounds(juce::Label& label,
                          float preferredHeight,
                          float minimumHeight,
                          int maxLines = 1);

} // namespace vxsuite
