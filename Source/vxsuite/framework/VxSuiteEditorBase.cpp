#include "VxSuiteEditorBase.h"

namespace vxsuite {
#ifndef VXSUITE_VERSION_STRING
#define VXSUITE_VERSION_STRING "0.1.0"
#endif

EditorBase::EditorBase(ProcessorBase& owner)
    : juce::AudioProcessorEditor(&owner),
      processor(owner),
      lookAndFeel(owner.getProductIdentity().theme) {
    setLookAndFeel(&lookAndFeel);
    setResizable(true, false);
    const bool hasTertiary = owner.getProductIdentity().supportsTertiaryControl();
    setResizeLimits(hasTertiary ? 680 : 520, hasTertiary ? 430 : 420, 980, 680);
    setSize(hasTertiary ? 840 : 620, 440);

    const auto& identity = processor.getProductIdentity();

    suiteLabel.setText(toJuceString(identity.suiteName), juce::dontSendNotification);
    suiteLabel.setFont(juce::FontOptions().withHeight(16.0f).withKerningFactor(0.16f));
    suiteLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(suiteLabel);

    productLabel.setText(toJuceString(identity.productName), juce::dontSendNotification);
    productLabel.setFont(juce::FontOptions().withHeight(34.0f).withStyle("Bold"));
    productLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(productLabel);

    modeLabel.setText("Mode", juce::dontSendNotification);
    modeLabel.setFont(juce::FontOptions().withHeight(13.0f).withKerningFactor(0.08f));
    if (identity.supportsModeSwitch())
        addAndMakeVisible(modeLabel);

    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setFont(juce::FontOptions().withHeight(12.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.62f));
    addAndMakeVisible(statusLabel);

    modeBox.addItem("Vocal", 1);
    modeBox.addItem("General", 2);
    modeBox.setWantsKeyboardFocus(true);
    if (identity.supportsModeSwitch())
        addAndMakeVisible(modeBox);

    listenButton.setButtonText("Listen");
    listenButton.setClickingTogglesState(true);
    listenButton.setWantsKeyboardFocus(true);
    if (identity.supportsListenMode())
        addAndMakeVisible(listenButton);

    configureKnob(primarySlider, primaryLabel, identity.primaryLabel, identity.primaryHint);
    configureKnob(secondarySlider, secondaryLabel, identity.secondaryLabel, identity.secondaryHint);
    if (identity.supportsTertiaryControl())
        configureKnob(tertiarySlider, tertiaryLabel, identity.tertiaryLabel, identity.tertiaryHint);
    addAndMakeVisible(primaryHint);
    addAndMakeVisible(secondaryHint);
    if (identity.supportsTertiaryControl())
        addAndMakeVisible(tertiaryHint);

    auto& state = processor.getValueTreeState();
    if (identity.supportsModeSwitch())
        modeAttachment = std::make_unique<ComboAttachment>(state, identity.modeParamId.data(), modeBox);
    if (identity.supportsListenMode())
        listenAttachment = std::make_unique<ButtonAttachment>(state, identity.listenParamId.data(), listenButton);
    primaryAttachment = std::make_unique<SliderAttachment>(state, identity.primaryParamId.data(), primarySlider);
    secondaryAttachment = std::make_unique<SliderAttachment>(state, identity.secondaryParamId.data(), secondarySlider);
    if (identity.supportsTertiaryControl())
        tertiaryAttachment = std::make_unique<SliderAttachment>(state, identity.tertiaryParamId.data(), tertiarySlider);

    addMouseListener(this, true);
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

    // ── Shelf filter icons ────────────────────────────────────────────────────
    const auto& id = processor.getProductIdentity();
    if (id.showLowShelfIcon || id.showHighShelfIcon) {
        const auto accent = lookAndFeel.findColour(juce::Label::textColourId);
        auto& state = processor.getValueTreeState();
        const bool lowOn  = !id.supportsLowShelfToggle()  || readBool(state, id.lowShelfParamId,  true);
        const bool highOn = !id.supportsHighShelfToggle() || readBool(state, id.highShelfParamId, true);

        // Helper: draws a small rounded box with a filter-response curve inside.
        // isLow=true  → high-pass / low-shelf cut shape (curve rises left-to-right)
        // isLow=false → low-pass / high-shelf cut shape (curve drops left-to-right)
        // active=false → icon is dimmed to indicate the processing is bypassed.
        auto drawShelfIcon = [&](juce::Rectangle<int> bounds, bool isLow, bool active) {
            const auto r = bounds.toFloat();
            const float x = r.getX(), y = r.getY();
            const float w = r.getWidth(), h = r.getHeight();
            const float curveAlpha = active ? 0.72f : 0.20f;

            g.setColour(accent.withAlpha(active ? 0.10f : 0.05f));
            g.fillRoundedRectangle(r, static_cast<float>(scaled(4)));
            g.setColour(accent.withAlpha(active ? 0.35f : 0.12f));
            g.drawRoundedRectangle(r.reduced(0.5f), static_cast<float>(scaled(4)), 0.75f);

            juce::Path p;
            if (isLow) {
                // Rises from bottom-left, sweeps up, then flat to the right
                p.startNewSubPath(x + w * 0.04f, y + h * 0.82f);
                p.cubicTo(x + w * 0.28f, y + h * 0.82f,
                          x + w * 0.46f, y + h * 0.18f,
                          x + w * 0.62f, y + h * 0.18f);
                p.lineTo(x + w * 0.96f, y + h * 0.18f);
            } else {
                // Flat from left, then sweeps down to bottom-right
                p.startNewSubPath(x + w * 0.04f, y + h * 0.18f);
                p.lineTo(x + w * 0.38f, y + h * 0.18f);
                p.cubicTo(x + w * 0.54f, y + h * 0.18f,
                          x + w * 0.72f, y + h * 0.82f,
                          x + w * 0.96f, y + h * 0.82f);
            }
            g.setColour(accent.withAlpha(curveAlpha));
            g.strokePath(p, juce::PathStrokeType(1.5f,
                            juce::PathStrokeType::curved,
                            juce::PathStrokeType::rounded));
        };

        if (id.showLowShelfIcon  && !lowShelfIconBounds .isEmpty())
            drawShelfIcon(lowShelfIconBounds,  true,  lowOn);
        if (id.showHighShelfIcon && !highShelfIconBounds.isEmpty())
            drawShelfIcon(highShelfIconBounds, false, highOn);
    }

    g.setColour(lookAndFeel.findColour(juce::Label::textColourId).withAlpha(0.45f));
    g.setFont(juce::FontOptions().withHeight(static_cast<float>(scaled(11))));
    g.drawFittedText(footerText(),
                     getLocalBounds().reduced(scaled(24), scaled(18)),
                     juce::Justification::bottomRight,
                     1);
}

void EditorBase::resized() {
    auto bounds = getLocalBounds().reduced(scaled(20), scaled(16));
    auto header = bounds.removeFromTop(scaled(104));
    auto body = bounds.reduced(0, scaled(10));

    suiteLabel.setBounds(header.removeFromTop(scaled(20)));
    productLabel.setBounds(header.removeFromTop(scaled(42)));

    header.removeFromTop(scaled(4));
    auto modeRow = header.removeFromTop(scaled(36));
    if (processor.getProductIdentity().supportsModeSwitch()) {
        modeLabel.setBounds(modeRow.removeFromLeft(scaled(72)));
        modeRow.removeFromLeft(scaled(8));
        modeBox.setBounds(modeRow.removeFromLeft(scaled(176)).reduced(0, scaled(2)));
        modeRow.removeFromLeft(scaled(16));
    }
    if (processor.getProductIdentity().supportsListenMode()) {
        listenButton.setBounds(modeRow.removeFromLeft(scaled(96)).reduced(0, scaled(2)));
        modeRow.removeFromLeft(scaled(12));
    }
    statusLabel.setBounds(modeRow);

    const auto& layoutId = processor.getProductIdentity();
    const bool hasTertiary = layoutId.supportsTertiaryControl();
    const bool stacked = body.getWidth() < scaled(hasTertiary ? 760 : 560);

    // Compute shelf icon bounds — both icons side-by-side and centred horizontally
    // on the upper separator line (top of the knob area).  The separator is drawn
    // in paint() at paint_body.getY() + 72; in resized() coordinates that maps to
    // body.getY() + 64, so both sets of coordinates land on the same screen pixel.
    {
        const int iconW  = scaled(28);
        const int iconH  = scaled(16);
        const int gap    = scaled(8);
        const int iconY  = body.getY() + scaled(64) - iconH / 2;

        const bool bothVisible = layoutId.showLowShelfIcon && layoutId.showHighShelfIcon;
        const int  rowW  = bothVisible ? iconW * 2 + gap : iconW;
        const int  iconX0 = body.getCentreX() - rowW / 2;
        const int  iconX1 = iconX0 + iconW + gap;

        lowShelfIconBounds  = layoutId.showLowShelfIcon
            ? juce::Rectangle<int>(iconX0, iconY, iconW, iconH)
            : juce::Rectangle<int>();
        highShelfIconBounds = layoutId.showHighShelfIcon
            ? juce::Rectangle<int>(iconX1, iconY, iconW, iconH)
            : juce::Rectangle<int>();
    }
    if (stacked) {
        body.removeFromBottom(scaled(34));
        const int rows = hasTertiary ? 3 : 2;
        const int rowHeight = body.getHeight() / rows;
        auto layoutKnob = [&](juce::Rectangle<int> area, juce::Slider& slider, juce::Label& label, juce::Label& hint) {
            auto section = area.reduced(scaled(26), scaled(20));
            const int dialSize = std::min(section.getWidth(), scaled(134));
            label.setBounds(section.removeFromTop(scaled(28)));
            section.removeFromTop(scaled(6));
            auto dialRow = section.removeFromTop(dialSize);
            slider.setBounds(dialRow.withSizeKeepingCentre(dialSize, dialSize));
            section.removeFromTop(scaled(10));
            hint.setBounds(section.removeFromTop(scaled(44)));
        };

        layoutKnob(body.removeFromTop(rowHeight), primarySlider, primaryLabel, primaryHint);
        if (hasTertiary) {
            layoutKnob(body.removeFromTop(rowHeight), secondarySlider, secondaryLabel, secondaryHint);
            layoutKnob(body, tertiarySlider, tertiaryLabel, tertiaryHint);
        } else {
            layoutKnob(body, secondarySlider, secondaryLabel, secondaryHint);
        }
        return;
    }

    body.removeFromBottom(scaled(34));
    if (hasTertiary) {
        auto left = body.removeFromLeft(body.getWidth() / 3).reduced(scaled(18), scaled(24));
        auto center = body.removeFromLeft(body.getWidth() / 2).reduced(scaled(18), scaled(24));
        auto right = body.reduced(scaled(18), scaled(24));
        const int dialSize = std::min({ left.getWidth(), center.getWidth(), right.getWidth(), scaled(128) });

        auto layoutKnob = [&](juce::Rectangle<int> area, juce::Slider& slider, juce::Label& label, juce::Label& hint) {
            label.setBounds(area.removeFromTop(scaled(28)));
            area.removeFromTop(scaled(8));
            auto dialRow = area.removeFromTop(dialSize);
            slider.setBounds(dialRow.withSizeKeepingCentre(dialSize, dialSize));
            area.removeFromTop(scaled(12));
            hint.setBounds(area.removeFromTop(scaled(44)));
        };

        layoutKnob(left, primarySlider, primaryLabel, primaryHint);
        layoutKnob(center, secondarySlider, secondaryLabel, secondaryHint);
        layoutKnob(right, tertiarySlider, tertiaryLabel, tertiaryHint);
        return;
    }

    auto left = body.removeFromLeft(body.getWidth() / 2).reduced(scaled(26), scaled(24));
    auto right = body.reduced(scaled(26), scaled(24));
    const int dialSize = std::min({ left.getWidth(), right.getWidth(), scaled(138) });

    primaryLabel.setBounds(left.removeFromTop(scaled(28)));
    left.removeFromTop(scaled(8));
    auto leftDialRow = left.removeFromTop(dialSize);
    primarySlider.setBounds(leftDialRow.withSizeKeepingCentre(dialSize, dialSize));
    left.removeFromTop(scaled(12));
    primaryHint.setBounds(left.removeFromTop(scaled(44)));

    secondaryLabel.setBounds(right.removeFromTop(scaled(28)));
    right.removeFromTop(scaled(8));
    auto rightDialRow = right.removeFromTop(dialSize);
    secondarySlider.setBounds(rightDialRow.withSizeKeepingCentre(dialSize, dialSize));
    right.removeFromTop(scaled(12));
    secondaryHint.setBounds(right.removeFromTop(scaled(44)));
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
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, scaled(76), scaled(20));
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f,
                               true);
    slider.setDoubleClickReturnValue(true, 0.5);
    slider.setScrollWheelEnabled(false);
    slider.setWantsKeyboardFocus(true);
    addAndMakeVisible(slider);

    label.setText(toJuceString(text), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setFont(juce::FontOptions().withHeight(static_cast<float>(scaled(18))).withStyle("Bold"));
    addAndMakeVisible(label);

    juce::Label* hintLabel = &secondaryHint;
    if (&label == &primaryLabel)
        hintLabel = &primaryHint;
    else if (&label == &tertiaryLabel)
        hintLabel = &tertiaryHint;
    hintLabel->setText(toJuceString(hint), juce::dontSendNotification);
    hintLabel->setJustificationType(juce::Justification::centredLeft);
    hintLabel->setFont(juce::FontOptions().withHeight(static_cast<float>(scaled(12))));
    hintLabel->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
}

juce::String EditorBase::footerText() const {
    return juce::String("v") + juce::String(VXSUITE_VERSION_STRING) + "    (c) Andrzej Marczewski 2026";
}

int EditorBase::scaled(const int value) const {
    return juce::roundToInt(static_cast<float>(value) * uiScale);
}

void EditorBase::mouseDown(const juce::MouseEvent& e) {
    const auto& id = processor.getProductIdentity();
    auto& state    = processor.getValueTreeState();

    auto toggleParam = [&](std::string_view paramId) {
        if (auto* param = state.getParameter(paramId.data())) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->getValue() < 0.5f ? 1.0f : 0.0f);
            param->endChangeGesture();
            repaint();
        }
    };

    const auto localPos = e.getEventRelativeTo(this).getPosition();
    if (id.supportsLowShelfToggle()  && lowShelfIconBounds .contains(localPos))
        toggleParam(id.lowShelfParamId);
    else if (id.supportsHighShelfToggle() && highShelfIconBounds.contains(localPos))
        toggleParam(id.highShelfParamId);
    else if (e.eventComponent == this)
        juce::AudioProcessorEditor::mouseDown(e);
}

void EditorBase::timerCallback() {
    statusLabel.setText(processor.getStatusText(), juce::dontSendNotification);

    // Repaint if shelf toggle state changed (e.g. from automation or undo)
    const auto& id = processor.getProductIdentity();
    auto& state    = processor.getValueTreeState();
    const bool lowOn  = !id.supportsLowShelfToggle()  || readBool(state, id.lowShelfParamId,  true);
    const bool highOn = !id.supportsHighShelfToggle() || readBool(state, id.highShelfParamId, true);
    if (lowOn != lastLowShelfOn || highOn != lastHighShelfOn) {
        lastLowShelfOn  = lowOn;
        lastHighShelfOn = highOn;
        repaint();
    }
}

} // namespace vxsuite
