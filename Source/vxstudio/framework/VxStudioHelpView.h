#pragma once

#include "VxStudioParameters.h"
#include "VxStudioProduct.h"
#include "VxStudioUiHelpers.h"
#include "VxStudioVersions.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vxsuite {

void showHelpDialog(juce::Component& parent, const ProductIdentity& identity);

class HelpButton final : public juce::TextButton {
public:
    HelpButton();
};

} // namespace vxsuite
