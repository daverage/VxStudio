#pragma once

#include "VxSuiteParameters.h"
#include "VxSuiteProduct.h"
#include "VxSuiteUiHelpers.h"
#include "VxSuiteVersions.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vxsuite {

void showHelpDialog(juce::Component& parent, const ProductIdentity& identity);

class HelpButton final : public juce::TextButton {
public:
    HelpButton();
};

} // namespace vxsuite
