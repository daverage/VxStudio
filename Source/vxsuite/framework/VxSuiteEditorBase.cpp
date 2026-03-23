#include "VxSuiteEditorBase.h"

namespace vxsuite {
#ifndef VXSUITE_VERSION_STRING
#define VXSUITE_VERSION_STRING "0.1.0"
#endif

EditorBase::EditorBase(ProcessorBase& owner)
    : juce::AudioProcessorEditor(&owner),
      processor(owner),
      lookAndFeel(owner.getProductIdentity().theme),
      tooltipWindow(this, 700),
      levelTraceView(owner.getProductIdentity().theme) {
    auto makeMouseTransparent = [](auto& component) {
        component.setInterceptsMouseClicks(false, false);
    };

    setLookAndFeel(&lookAndFeel);
    setResizable(true, false);
    const bool hasTertiary = owner.getProductIdentity().supportsTertiaryControl();
    const bool hasQuaternary = owner.getProductIdentity().supportsQuaternaryControl();
    setResizeLimits(hasQuaternary ? 920 : (hasTertiary ? 820 : 680),
                    hasQuaternary ? 700 : (hasTertiary ? 640 : 600),
                    1280,
                    940);
    setSize(hasQuaternary ? 1100 : (hasTertiary ? 950 : 760), hasQuaternary ? 740 : 660);

    const auto& identity = processor.getProductIdentity();

    suiteLabel.setText(toJuceString(identity.suiteName), juce::dontSendNotification);
    suiteLabel.setFont(juce::FontOptions().withHeight(17.0f).withKerningFactor(0.16f));
    suiteLabel.setJustificationType(juce::Justification::centredLeft);
    suiteLabel.setMinimumHorizontalScale(0.70f);
    makeMouseTransparent(suiteLabel);
    addAndMakeVisible(suiteLabel);

    productLabel.setText(toJuceString(identity.productName), juce::dontSendNotification);
    productLabel.setFont(juce::FontOptions().withHeight(38.0f).withStyle("Bold"));
    productLabel.setJustificationType(juce::Justification::centredLeft);
    productLabel.setMinimumHorizontalScale(0.58f);
    makeMouseTransparent(productLabel);
    addAndMakeVisible(productLabel);

    modeLabel.setText(toJuceString(identity.selectorLabel.empty() ? "Mode" : identity.selectorLabel),
                      juce::dontSendNotification);
    modeLabel.setFont(juce::FontOptions().withHeight(14.0f).withKerningFactor(0.08f));
    modeLabel.setMinimumHorizontalScale(0.72f);
    makeMouseTransparent(modeLabel);
    if (identity.supportsModeSwitch())
        addAndMakeVisible(modeLabel);

    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setFont(juce::FontOptions().withHeight(13.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.62f));
    statusLabel.setMinimumHorizontalScale(0.55f);
    makeMouseTransparent(statusLabel);
    addAndMakeVisible(statusLabel);

    learnMeterLabel.setJustificationType(juce::Justification::centredLeft);
    learnMeterLabel.setFont(juce::FontOptions().withHeight(12.0f).withKerningFactor(0.05f));
    learnMeterLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.74f));
    learnMeterLabel.setMinimumHorizontalScale(0.58f);
    makeMouseTransparent(learnMeterLabel);
    learnMeterBar.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff14141c));
    learnMeterBar.setColour(juce::ProgressBar::foregroundColourId,
                            lookAndFeel.findColour(juce::Slider::rotarySliderFillColourId).withAlpha(0.92f));
    makeMouseTransparent(learnMeterBar);
    if (identity.supportsLearnButton()) {
        addAndMakeVisible(learnMeterLabel);
        addAndMakeVisible(learnMeterBar);
    }

    const auto selectorChoices = makeSelectorChoiceLabels(identity);
    for (int i = 0; i < selectorChoices.size(); ++i)
        modeBox.addItem(selectorChoices[i], i + 1);
    modeBox.setWantsKeyboardFocus(true);
    if (identity.supportsModeSwitch())
        addAndMakeVisible(modeBox);

    listenButton.setButtonText("Listen");
    listenButton.setClickingTogglesState(true);
    listenButton.setWantsKeyboardFocus(true);
    if (identity.supportsListenMode())
        addAndMakeVisible(listenButton);

    learnButton.setButtonText(identity.learnButtonLabel.empty() ? "Learn" : toJuceString(identity.learnButtonLabel));
    learnButton.setClickingTogglesState(true);
    learnButton.setWantsKeyboardFocus(true);
    if (identity.supportsLearnButton())
        addAndMakeVisible(learnButton);

    traceZoomBox.addItem("1.5 s", 1);
    traceZoomBox.addItem("3 s", 2);
    traceZoomBox.addItem("6 s", 3);
    traceZoomBox.addItem("12 s", 4);
    traceZoomBox.setSelectedId(3, juce::dontSendNotification);
    traceZoomBox.onChange = [this] {
        switch (traceZoomBox.getSelectedId()) {
            case 1: levelTraceView.setZoomSeconds(1.5f); break;
            case 2: levelTraceView.setZoomSeconds(3.0f); break;
            case 4: levelTraceView.setZoomSeconds(12.0f); break;
            case 3:
            default: levelTraceView.setZoomSeconds(6.0f); break;
        }
    };
    if (identity.showLevelTrace) {
        addAndMakeVisible(traceZoomBox);
        addAndMakeVisible(levelTraceView);
    }
    learnButton.setTooltip("Capture pure background noise only for about 1 to 2 seconds, then press again to stop and lock the profile.");
    learnMeterBar.setTooltip("Confidence reflects how clean and representative the captured noise print is.");
    learnMeterLabel.setTooltip("Confidence reflects how clean and representative the captured noise print is.");

    configureKnob(primarySlider, primaryLabel, identity.primaryLabel, identity.primaryHint);
    configureKnob(secondarySlider, secondaryLabel, identity.secondaryLabel, identity.secondaryHint);
    if (identity.supportsTertiaryControl())
        configureKnob(tertiarySlider, tertiaryLabel, identity.tertiaryLabel, identity.tertiaryHint);
    if (identity.supportsQuaternaryControl())
        configureKnob(quaternarySlider, quaternaryLabel, identity.quaternaryLabel, identity.quaternaryHint);
    addAndMakeVisible(primaryHint);
    addAndMakeVisible(secondaryHint);
    if (identity.supportsTertiaryControl())
        addAndMakeVisible(tertiaryHint);
    if (identity.supportsQuaternaryControl())
        addAndMakeVisible(quaternaryHint);

    auto& state = processor.getValueTreeState();
    if (identity.supportsModeSwitch())
        modeAttachment = std::make_unique<ComboAttachment>(state, identity.modeParamId.data(), modeBox);
    if (identity.supportsListenMode())
        listenAttachment = std::make_unique<ButtonAttachment>(state, identity.listenParamId.data(), listenButton);
    if (identity.supportsLearnButton())
        learnAttachment = std::make_unique<ButtonAttachment>(state, identity.learnParamId.data(), learnButton);
    primaryAttachment = std::make_unique<SliderAttachment>(state, identity.primaryParamId.data(), primarySlider);
    secondaryAttachment = std::make_unique<SliderAttachment>(state, identity.secondaryParamId.data(), secondarySlider);
    if (identity.supportsTertiaryControl())
        tertiaryAttachment = std::make_unique<SliderAttachment>(state, identity.tertiaryParamId.data(), tertiarySlider);
    if (identity.supportsQuaternaryControl())
        quaternaryAttachment = std::make_unique<SliderAttachment>(state, identity.quaternaryParamId.data(), quaternarySlider);

    timerCallback();
    startTimerHz(12);
}

EditorBase::~EditorBase() {
    setLookAndFeel(nullptr);
}

void EditorBase::paint(juce::Graphics& g) {
    const auto background = lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId);
    g.fillAll(background);

    auto bounds = getLocalBounds().toFloat();
    auto header = bounds.removeFromTop(static_cast<float>(scaled(108))).reduced(static_cast<float>(scaled(20)),
                                                                               static_cast<float>(scaled(12)));
    auto body = bounds.reduced(static_cast<float>(scaled(20)), static_cast<float>(scaled(14)));

    g.setColour(juce::Colour(0xff16161e));
    g.fillRect(getLocalBounds().removeFromTop(scaled(2)));

    g.setColour(juce::Colours::black.withAlpha(0.16f));
    g.fillRoundedRectangle(body.translated(0.0f, static_cast<float>(scaled(8))), static_cast<float>(scaled(24)));
    g.setColour(juce::Colour(0xff1a1a26));
    g.fillRoundedRectangle(body, static_cast<float>(scaled(24)));

    g.setColour(juce::Colour(0xff2a3142));
    g.drawRoundedRectangle(body, static_cast<float>(scaled(24)), 1.0f);

    if (activityLightCount > 0 && !activityStripBounds.isEmpty()) {
        const auto strip = activityStripBounds.toFloat();
        const int lights = std::min(activityLightCount, static_cast<int>(activityLights.size()));
        const float slotW = strip.getWidth() / static_cast<float>(std::max(1, lights));
        const float ledRadius = juce::jmin(strip.getHeight() * 0.20f, slotW * 0.08f, 5.0f);
        const float ledY = strip.getY() + ledRadius * 2.5f + 2.0f;
        const float labelY = ledY + ledRadius + 3.0f;
        const float labelH = strip.getBottom() - labelY;

        g.setFont(juce::FontOptions().withHeight(juce::jmin(labelH, static_cast<float>(scaled(12)))));

        for (int i = 0; i < lights; ++i) {
            const float cx = strip.getX() + slotW * (static_cast<float>(i) + 0.5f);
            const float activity = juce::jlimit(0.0f, 1.0f, activityLights[static_cast<size_t>(i)]);
            const float glow = std::sqrt(activity);
            const auto offColour = juce::Colour::fromFloatRGBA(0.28f, 0.12f, 0.08f, 0.92f);
            const auto onColour = juce::Colour::fromFloatRGBA(1.0f, 0.42f + 0.35f * glow, 0.12f, 0.75f + 0.25f * glow);

            g.setColour(juce::Colours::black.withAlpha(0.30f));
            g.fillEllipse(cx - ledRadius, ledY - ledRadius, ledRadius * 2.0f, ledRadius * 2.0f);
            if (activity > 0.01f) {
                g.setColour(onColour.withAlpha(0.18f + 0.28f * glow));
                g.fillEllipse(cx - ledRadius * 2.4f, ledY - ledRadius * 2.4f, ledRadius * 4.8f, ledRadius * 4.8f);
            }
            g.setColour(activity > 0.01f ? onColour : offColour);
            g.fillEllipse(cx - ledRadius * 0.72f, ledY - ledRadius * 0.72f, ledRadius * 1.44f, ledRadius * 1.44f);

            const auto labelText = toJuceString(processor.getActivityLightLabel(i));
            if (labelText.isNotEmpty()) {
                g.setColour(lookAndFeel.findColour(juce::Label::textColourId).withAlpha(activity > 0.01f ? 0.82f : 0.38f));
                g.drawText(labelText,
                           juce::Rectangle<float>(cx - slotW * 0.5f, labelY, slotW, labelH),
                           juce::Justification::centredTop, false);
            }
        }
    }

    // Decorative shelf filter curve icons
    const auto& id = processor.getProductIdentity();
    if ((id.showLowShelfIcon && !lowShelfIconBounds.isEmpty())
     || (id.showHighShelfIcon && !highShelfIconBounds.isEmpty())) {
        const auto accent = lookAndFeel.findColour(juce::Label::textColourId);
        auto& state = processor.getValueTreeState();
        const bool lowOn  = id.lowShelfParamId.empty()  ? false : vxsuite::readBool(state, id.lowShelfParamId,  false);
        const bool highOn = id.highShelfParamId.empty() ? false : vxsuite::readBool(state, id.highShelfParamId, false);

        auto drawShelfIcon = [&](juce::Rectangle<int> iconBounds, bool isLow, bool active) {
            const auto r = iconBounds.toFloat();
            const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
            g.setColour(accent.withAlpha(active ? 0.10f : 0.05f));
            g.fillRoundedRectangle(r, static_cast<float>(scaled(4)));
            g.setColour(accent.withAlpha(active ? 0.35f : 0.14f));
            g.drawRoundedRectangle(r.reduced(0.5f), static_cast<float>(scaled(4)), 0.75f);
            juce::Path p;
            if (isLow) {
                p.startNewSubPath(x + w * 0.04f, y + h * 0.82f);
                p.cubicTo(x + w * 0.28f, y + h * 0.82f,
                          x + w * 0.46f, y + h * 0.18f,
                          x + w * 0.62f, y + h * 0.18f);
                p.lineTo(x + w * 0.96f, y + h * 0.18f);
            } else {
                p.startNewSubPath(x + w * 0.04f, y + h * 0.18f);
                p.lineTo(x + w * 0.38f, y + h * 0.18f);
                p.cubicTo(x + w * 0.54f, y + h * 0.18f,
                          x + w * 0.72f, y + h * 0.82f,
                          x + w * 0.96f, y + h * 0.82f);
            }
            g.setColour(accent.withAlpha(active ? 0.72f : 0.22f));
            g.strokePath(p, juce::PathStrokeType(1.5f,
                            juce::PathStrokeType::curved,
                            juce::PathStrokeType::rounded));
        };
        if (id.showLowShelfIcon)  drawShelfIcon(lowShelfIconBounds,  true,  lowOn);
        if (id.showHighShelfIcon) drawShelfIcon(highShelfIconBounds, false, highOn);
    }

    g.setColour(lookAndFeel.findColour(juce::Label::textColourId).withAlpha(0.10f));
    g.drawLine(body.getX() + static_cast<float>(scaled(24)),
               body.getY() + static_cast<float>(scaled(72)),
               body.getRight() - static_cast<float>(scaled(24)),
               body.getY() + static_cast<float>(scaled(72)),
               1.0f);
    g.drawLine(body.getX() + static_cast<float>(scaled(24)),
               body.getBottom() - static_cast<float>(scaled(48)),
               body.getRight() - static_cast<float>(scaled(24)),
               body.getBottom() - static_cast<float>(scaled(48)),
               1.0f);

    g.setColour(lookAndFeel.findColour(juce::Label::textColourId).withAlpha(0.45f));
    g.setFont(juce::FontOptions().withHeight(static_cast<float>(scaled(12))));
    g.drawFittedText(footerText(),
                     getLocalBounds().reduced(scaled(24), scaled(18)),
                     juce::Justification::bottomRight,
                     1);

}

void EditorBase::resized() {
    auto bounds = getLocalBounds().reduced(scaled(20), scaled(16));
    auto header = bounds.removeFromTop(processor.getProductIdentity().supportsLearnButton() ? scaled(164) : scaled(132));
    auto body = bounds.reduced(0, scaled(10));

    suiteLabel.setBounds(header.removeFromTop(scaled(20)));
    productLabel.setBounds(header.removeFromTop(scaled(42)));

    header.removeFromTop(scaled(4));
    auto modeRow = header.removeFromTop(scaled(40));
    if (processor.getProductIdentity().supportsModeSwitch()) {
        modeLabel.setBounds(modeRow.removeFromLeft(scaled(96)));
        modeRow.removeFromLeft(scaled(8));
        modeBox.setBounds(modeRow.removeFromLeft(scaled(228)).reduced(0, scaled(2)));
        modeRow.removeFromLeft(scaled(16));
    }
    if (processor.getProductIdentity().supportsListenMode()) {
        listenButton.setBounds(modeRow.removeFromLeft(scaled(116)).reduced(0, scaled(2)));
        modeRow.removeFromLeft(scaled(12));
    }
    if (processor.getProductIdentity().supportsLearnButton()) {
        learnButton.setBounds(modeRow.removeFromLeft(scaled(110)).reduced(0, scaled(2)));
        modeRow.removeFromLeft(scaled(12));
    }

    const bool hasTertiary = processor.getProductIdentity().supportsTertiaryControl();
    const bool hasQuaternary = processor.getProductIdentity().supportsQuaternaryControl();
    const bool wrapStatusRow = modeRow.getWidth() < scaled(220);
    if (wrapStatusRow) {
        statusLabel.setBounds(header.removeFromTop(scaled(24)));
    } else {
        statusLabel.setBounds(modeRow);
    }
    if (processor.getProductIdentity().supportsLearnButton()) {
        header.removeFromTop(scaled(6));
        auto learnRow = header.removeFromTop(scaled(24));
        learnMeterLabel.setBounds(learnRow.removeFromLeft(scaled(176)));
        learnRow.removeFromLeft(scaled(8));
        learnMeterBar.setBounds(learnRow.removeFromLeft(juce::jmax(scaled(220), juce::jmin(scaled(320), learnRow.getWidth()))));
    }

    const bool stacked = body.getWidth() < scaled(hasQuaternary ? 940 : (hasTertiary ? 800 : 600));
    activityLightCount = processor.getActivityLightCount();
    activityStripBounds = {};
    traceViewBounds = {};
    if (activityLightCount > 0) {
        activityStripBounds = body.removeFromTop(scaled(38)).reduced(scaled(20), 0);
        body.removeFromTop(scaled(4));
    }
    if (processor.getProductIdentity().showLevelTrace) {
        traceViewBounds = body.removeFromTop(stacked ? scaled(164) : scaled(150)).reduced(scaled(18), scaled(4));
        levelTraceView.setBounds(traceViewBounds);
        auto zoomBounds = traceViewBounds.removeFromTop(scaled(26));
        traceZoomBox.setBounds(zoomBounds.removeFromRight(scaled(104)).reduced(0, scaled(2)));
        body.removeFromTop(scaled(6));
    }

    lowShelfIconBounds  = {};
    highShelfIconBounds = {};
    const auto& layoutId = processor.getProductIdentity();
    const auto placeInlineShelfIcons = [&](const juce::Rectangle<int>& anchorLabelBounds) {
        if (!(layoutId.showLowShelfIcon || layoutId.showHighShelfIcon))
            return;

        const int iconW = scaled(32);
        const int iconH = scaled(18);
        const int gap = scaled(10);
        const bool both = layoutId.showLowShelfIcon && layoutId.showHighShelfIcon;
        const int rowW = both ? iconW * 2 + gap : iconW;
        const int startX = anchorLabelBounds.getCentreX() - rowW / 2 + scaled(54);
        const int iconY = anchorLabelBounds.getCentreY() - iconH / 2;

        if (layoutId.showLowShelfIcon)
            lowShelfIconBounds = { startX, iconY, iconW, iconH };
        if (layoutId.showHighShelfIcon)
            highShelfIconBounds = { both ? startX + iconW + gap : startX, iconY, iconW, iconH };
    };
    if (stacked) {
        body.removeFromBottom(scaled(34));
        const int rows = hasQuaternary ? 4 : (hasTertiary ? 3 : 2);
        const int rowHeight = body.getHeight() / rows;
        auto layoutKnob = [&](juce::Rectangle<int> area, juce::Slider& slider, juce::Label& label, juce::Label& hint) {
            auto section = area.reduced(scaled(26), scaled(20));
            const int dialSize = std::min(section.getWidth(), scaled(142));
            label.setBounds(section.removeFromTop(scaled(36)));
            section.removeFromTop(scaled(6));
            auto dialRow = section.removeFromTop(dialSize);
            slider.setBounds(dialRow.withSizeKeepingCentre(dialSize, dialSize));
            section.removeFromTop(scaled(10));
            hint.setBounds(section.removeFromTop(scaled(62)));
        };

        layoutKnob(body.removeFromTop(rowHeight), primarySlider, primaryLabel, primaryHint);
        if (hasQuaternary) {
            layoutKnob(body.removeFromTop(rowHeight), secondarySlider, secondaryLabel, secondaryHint);
            layoutKnob(body.removeFromTop(rowHeight), tertiarySlider, tertiaryLabel, tertiaryHint);
            layoutKnob(body, quaternarySlider, quaternaryLabel, quaternaryHint);
        } else if (hasTertiary) {
            layoutKnob(body.removeFromTop(rowHeight), secondarySlider, secondaryLabel, secondaryHint);
            layoutKnob(body, tertiarySlider, tertiaryLabel, tertiaryHint);
        } else {
            layoutKnob(body, secondarySlider, secondaryLabel, secondaryHint);
        }
        placeInlineShelfIcons(secondaryLabel.getBounds());
        return;
    }

    body.removeFromBottom(scaled(34));
    if (hasQuaternary) {
        auto c1 = body.removeFromLeft(body.getWidth() / 4).reduced(scaled(14), scaled(24));
        auto c2 = body.removeFromLeft(body.getWidth() / 3).reduced(scaled(14), scaled(24));
        auto c3 = body.removeFromLeft(body.getWidth() / 2).reduced(scaled(14), scaled(24));
        auto c4 = body.reduced(scaled(14), scaled(24));
        const int dialSize = std::min({ c1.getWidth(), c2.getWidth(), c3.getWidth(), c4.getWidth(), scaled(132) });

        auto layoutKnob = [&](juce::Rectangle<int> area, juce::Slider& slider, juce::Label& label, juce::Label& hint) {
            label.setBounds(area.removeFromTop(scaled(36)));
            area.removeFromTop(scaled(8));
            auto dialRow = area.removeFromTop(dialSize);
            slider.setBounds(dialRow.withSizeKeepingCentre(dialSize, dialSize));
            area.removeFromTop(scaled(12));
            hint.setBounds(area.removeFromTop(scaled(72)));
        };

        layoutKnob(c1, primarySlider, primaryLabel, primaryHint);
        layoutKnob(c2, secondarySlider, secondaryLabel, secondaryHint);
        layoutKnob(c3, tertiarySlider, tertiaryLabel, tertiaryHint);
        layoutKnob(c4, quaternarySlider, quaternaryLabel, quaternaryHint);
        placeInlineShelfIcons(secondaryLabel.getBounds());
        return;
    }

    if (hasTertiary) {
        auto left = body.removeFromLeft(body.getWidth() / 3).reduced(scaled(18), scaled(24));
        auto center = body.removeFromLeft(body.getWidth() / 2).reduced(scaled(18), scaled(24));
        auto right = body.reduced(scaled(18), scaled(24));
        const int dialSize = std::min({ left.getWidth(), center.getWidth(), right.getWidth(), scaled(136) });

        auto layoutKnob = [&](juce::Rectangle<int> area, juce::Slider& slider, juce::Label& label, juce::Label& hint) {
            label.setBounds(area.removeFromTop(scaled(36)));
            area.removeFromTop(scaled(8));
            auto dialRow = area.removeFromTop(dialSize);
            slider.setBounds(dialRow.withSizeKeepingCentre(dialSize, dialSize));
            area.removeFromTop(scaled(12));
            hint.setBounds(area.removeFromTop(scaled(62)));
        };

        layoutKnob(left, primarySlider, primaryLabel, primaryHint);
        layoutKnob(center, secondarySlider, secondaryLabel, secondaryHint);
        layoutKnob(right, tertiarySlider, tertiaryLabel, tertiaryHint);
        placeInlineShelfIcons(secondaryLabel.getBounds());
        return;
    }

    auto left = body.removeFromLeft(body.getWidth() / 2).reduced(scaled(26), scaled(24));
    auto right = body.reduced(scaled(26), scaled(24));
    const int dialSize = std::min({ left.getWidth(), right.getWidth(), scaled(146) });

    primaryLabel.setBounds(left.removeFromTop(scaled(36)));
    left.removeFromTop(scaled(8));
    auto leftDialRow = left.removeFromTop(dialSize);
    primarySlider.setBounds(leftDialRow.withSizeKeepingCentre(dialSize, dialSize));
    left.removeFromTop(scaled(12));
    primaryHint.setBounds(left.removeFromTop(scaled(62)));

    secondaryLabel.setBounds(right.removeFromTop(scaled(36)));
    right.removeFromTop(scaled(8));
    auto rightDialRow = right.removeFromTop(dialSize);
    secondarySlider.setBounds(rightDialRow.withSizeKeepingCentre(dialSize, dialSize));
    right.removeFromTop(scaled(12));
    secondaryHint.setBounds(right.removeFromTop(scaled(62)));
    placeInlineShelfIcons(secondaryLabel.getBounds());
}

void EditorBase::setScaleFactor(const float newScale) {
    juce::AudioProcessorEditor::setScaleFactor(newScale);
    uiScale = juce::jlimit(0.75f, 2.0f, newScale);
    resized();
    repaint();
}

void EditorBase::configureKnob(juce::Slider& slider,
                               juce::Label& label,
                               std::string_view text,
                               std::string_view hint) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, scaled(88), scaled(24));
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f,
                               true);
    slider.setDoubleClickReturnValue(true, 0.5);
    slider.setScrollWheelEnabled(false);
    slider.setWantsKeyboardFocus(true);
    addAndMakeVisible(slider);

    label.setText(toJuceString(text), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setFont(juce::FontOptions().withHeight(static_cast<float>(scaled(20))).withStyle("Bold"));
    label.setMinimumHorizontalScale(0.58f);
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);

    juce::Label* hintLabel = &secondaryHint;
    if (&label == &primaryLabel)
        hintLabel = &primaryHint;
    else if (&label == &tertiaryLabel)
        hintLabel = &tertiaryHint;
    else if (&label == &quaternaryLabel)
        hintLabel = &quaternaryHint;
    hintLabel->setText(toJuceString(hint), juce::dontSendNotification);
    hintLabel->setJustificationType(juce::Justification::centredLeft);
    hintLabel->setFont(juce::FontOptions().withHeight(static_cast<float>(scaled(13))));
    hintLabel->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    hintLabel->setMinimumHorizontalScale(0.60f);
    hintLabel->setInterceptsMouseClicks(false, false);
}

juce::String EditorBase::footerText() const {
    return juce::String("v") + juce::String(VXSUITE_VERSION_STRING) + "    (c) Andrzej Marczewski 2026";
}

int EditorBase::scaled(const int value) const {
    return juce::roundToInt(static_cast<float>(value) * uiScale);
}

void EditorBase::showTransientStatus(const juce::String& text) {
    transientStatusText = text;
    transientStatusTicks = 24;
    statusLabel.setText(transientStatusText, juce::dontSendNotification);
}

void EditorBase::mouseDown(const juce::MouseEvent& e) {
    const auto& id  = processor.getProductIdentity();
    auto& state     = processor.getValueTreeState();
    const auto pos  = e.getEventRelativeTo(this).getPosition();

    auto toggleParam = [&](std::string_view paramId, const juce::String& onText, const juce::String& offText) {
        if (auto* param = state.getParameter(paramId.data())) {
            const bool turnOn = param->getValue() < 0.5f;
            param->beginChangeGesture();
            param->setValueNotifyingHost(turnOn ? 1.0f : 0.0f);
            param->endChangeGesture();
            showTransientStatus(turnOn ? onText : offText);
            repaint();
        }
    };

    if (!id.lowShelfParamId.empty()  && lowShelfIconBounds.contains(pos))
        toggleParam(id.lowShelfParamId,
                    "Low slope on - tighter low-end cleanup before processing",
                    "Low slope off - low-end cleanup bypassed");
    else if (!id.highShelfParamId.empty() && highShelfIconBounds.contains(pos))
        toggleParam(id.highShelfParamId,
                    "High slope on - gentler top-end trim after processing",
                    "High slope off - top-end trim bypassed");
    else
        juce::AudioProcessorEditor::mouseDown(e);
}

void EditorBase::timerCallback() {
    if (transientStatusTicks > 0) {
        --transientStatusTicks;
        statusLabel.setText(transientStatusText, juce::dontSendNotification);
    } else {
        transientStatusText.clear();
        statusLabel.setText(processor.getStatusText(), juce::dontSendNotification);
    }
    updateActivityIndicators();
    updateLearnUi();
    if (processor.getProductIdentity().showLevelTrace) {
        vxsuite::spectrum::SnapshotView snapshot;
        if (processor.getSpectrumSnapshotView(snapshot)) {
            traceMissTicks = 0;
            levelTraceView.setSnapshot(snapshot);
        } else if (++traceMissTicks > 12) {
            levelTraceView.setUnavailable();
        }
    }

    const auto& id = processor.getProductIdentity();
    auto& state    = processor.getValueTreeState();
    const bool lowOn  = id.lowShelfParamId.empty()  ? false : vxsuite::readBool(state, id.lowShelfParamId,  false);
    const bool highOn = id.highShelfParamId.empty() ? false : vxsuite::readBool(state, id.highShelfParamId, false);
    if (lowOn != lastLowShelfOn || highOn != lastHighShelfOn) {
        lastLowShelfOn  = lowOn;
        lastHighShelfOn = highOn;
        repaint();
    }
}

void EditorBase::updateActivityIndicators() {
    const int lights = std::min(processor.getActivityLightCount(), static_cast<int>(activityLights.size()));
    bool changed = false;
    for (int i = 0; i < lights; ++i) {
        const float next = juce::jlimit(0.0f, 1.0f, processor.getActivityLight(i));
        if (std::abs(activityLights[static_cast<size_t>(i)] - next) > 0.01f) {
            activityLights[static_cast<size_t>(i)] = next;
            changed = true;
        }
    }
    if (changed && !activityStripBounds.isEmpty())
        repaint(activityStripBounds);
}

void EditorBase::updateLearnUi() {
    if (!processor.getProductIdentity().supportsLearnButton())
        return;

    const bool active = processor.isLearnActive();
    const bool ready = processor.isLearnReady();
    const float progress = juce::jlimit(0.0f, 1.0f, processor.getLearnProgress());
    const float confidence = juce::jlimit(0.0f, 1.0f, processor.getLearnConfidence());
    const float observedSeconds = juce::jmax(0.0f, processor.getLearnObservedSeconds());
    const int confidencePct = juce::roundToInt(confidence * 100.0f);

    learnMeterUi = active ? static_cast<double>(progress) : static_cast<double>(confidence);
    learnMeterBar.setVisible(active || ready);
    if (active) {
        juce::String captureHint = "press Learn again to stop";
        if (observedSeconds < 0.8f)
            captureHint = "keep capturing a little longer, then press Learn again to stop";
        else if (observedSeconds >= 1.4f)
            captureHint = "good length, press Learn again to stop";

        learnMeterLabel.setText("Capturing noise " + juce::String(juce::roundToInt(progress * 100.0f)) + "%  "
                                + juce::String(observedSeconds, 1) + "s  " + captureHint,
                                juce::dontSendNotification);
        learnButton.setButtonText("Stop Learn");
    } else if (ready) {
        juce::String qualityText = "usable noise-print quality";
        if (confidencePct < 40)
            qualityText = "low confidence, try a cleaner noise-only capture";
        else if (confidencePct >= 75)
            qualityText = "strong clean capture";

        learnMeterLabel.setText("Profile ready  " + juce::String(confidencePct)
                                + "% confidence  " + qualityText,
                                juce::dontSendNotification);
        learnButton.setButtonText("Learn");
    } else {
        learnMeterLabel.setText("Capture pure room noise for about 1 to 2 seconds, then press Learn again to stop and lock it",
                                juce::dontSendNotification);
        learnButton.setButtonText("Learn");
    }
}

} // namespace vxsuite
