#include "VxSuiteEditorBase.h"

namespace vxsuite {
#ifndef VXSUITE_VERSION_STRING
#define VXSUITE_VERSION_STRING "0.1.0"
#endif

class EditorBase::ShelfIconButton final : public juce::Button {
public:
    enum class Kind {
        lowShelf,
        highShelf
    };

    explicit ShelfIconButton(const Kind newKind)
        : juce::Button(newKind == Kind::lowShelf ? "LowShelf" : "HighShelf"),
          kind(newKind) {}

    void setActivity(const float newActivity) {
        const float clamped = juce::jlimit(0.0f, 1.0f, newActivity);
        if (std::abs(activity - clamped) < 0.001f)
            return;
        activity = clamped;
        repaint();
    }

    void paintButton(juce::Graphics& g, const bool isMouseOverButton, const bool isButtonDown) override {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        const float corner = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.28f;
        auto fill = juce::Colour(0xff24202a);
        if (getToggleState())
            fill = fill.brighter(isMouseOverButton ? 0.25f : 0.12f);
        else
            fill = fill.withMultipliedAlpha(isMouseOverButton ? 0.95f : 0.82f);
        if (isButtonDown)
            fill = fill.brighter(0.08f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(juce::Colours::white.withAlpha(getToggleState() ? 0.34f : 0.18f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        auto iconArea = bounds.reduced(bounds.getWidth() * 0.20f, bounds.getHeight() * 0.22f);
        const float left = iconArea.getX();
        const float right = iconArea.getRight();
        const float top = iconArea.getY();
        const float bottom = iconArea.getBottom();
        const float midY = iconArea.getCentreY();
        const float shelfY = kind == Kind::lowShelf ? bottom - iconArea.getHeight() * 0.18f
                                                    : top + iconArea.getHeight() * 0.18f;
        const float bendX = iconArea.getX() + iconArea.getWidth() * 0.42f;

        juce::Path path;
        if (kind == Kind::lowShelf) {
            path.startNewSubPath(left, shelfY);
            path.lineTo(bendX, shelfY);
            path.lineTo(right, top);
        } else {
            path.startNewSubPath(left, bottom);
            path.lineTo(bendX, shelfY);
            path.lineTo(right, shelfY);
        }

        g.setColour(juce::Colours::white.withAlpha(getToggleState() ? 0.95f : 0.42f));
        g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float ledRadius = juce::jmax(4.2f, bounds.getWidth() * 0.16f);
        const juce::Point<float> ledCentre(bounds.getRight() - ledRadius * 1.75f,
                                           bounds.getY() + ledRadius * 1.75f);
        const float ledLevel = std::sqrt(juce::jlimit(0.0f, 1.0f, activity * 2.4f));
        const auto ringColour = juce::Colours::black.withAlpha(0.34f);
        const auto offColour = juce::Colour::fromFloatRGBA(0.42f, 0.14f, 0.12f, 0.96f);
        const auto onColour = juce::Colour::fromFloatRGBA(1.00f, 0.72f, 0.18f, 0.78f + 0.22f * ledLevel);

        g.setColour(ringColour);
        g.fillEllipse(ledCentre.x - ledRadius, ledCentre.y - ledRadius, ledRadius * 2.0f, ledRadius * 2.0f);
        if (activity > 0.01f) {
            g.setColour(onColour.withAlpha(0.24f + 0.30f * ledLevel));
            g.fillEllipse(ledCentre.x - ledRadius * 2.2f, ledCentre.y - ledRadius * 2.2f,
                          ledRadius * 4.4f, ledRadius * 4.4f);
        }
        g.setColour(activity > 0.01f ? onColour : offColour);
        g.fillEllipse(ledCentre.x - ledRadius * 0.72f, ledCentre.y - ledRadius * 0.72f,
                      ledRadius * 1.44f, ledRadius * 1.44f);
        g.setColour(juce::Colours::white.withAlpha(activity > 0.01f ? 0.65f : 0.18f));
        g.fillEllipse(ledCentre.x - ledRadius * 0.26f, ledCentre.y - ledRadius * 0.26f,
                      ledRadius * 0.52f, ledRadius * 0.52f);
    }

private:
    Kind kind;
    float activity = 0.0f;
};

EditorBase::EditorBase(ProcessorBase& owner)
    : juce::AudioProcessorEditor(&owner),
      processor(owner),
      lookAndFeel(owner.getProductIdentity().theme),
      tooltipWindow(this, 700) {
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

    learnMeterLabel.setJustificationType(juce::Justification::centredLeft);
    learnMeterLabel.setFont(juce::FontOptions().withHeight(11.0f).withKerningFactor(0.05f));
    learnMeterLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.74f));
    learnMeterBar.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff14141c));
    learnMeterBar.setColour(juce::ProgressBar::foregroundColourId,
                            lookAndFeel.findColour(juce::Slider::rotarySliderFillColourId).withAlpha(0.92f));
    if (identity.supportsLearnButton()) {
        addAndMakeVisible(learnMeterLabel);
        addAndMakeVisible(learnMeterBar);
    }

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

    learnButton.setButtonText(identity.learnButtonLabel.empty() ? "Learn" : toJuceString(identity.learnButtonLabel));
    learnButton.setClickingTogglesState(true);
    learnButton.setWantsKeyboardFocus(true);
    if (identity.supportsLearnButton())
        addAndMakeVisible(learnButton);

    if (identity.supportsLowShelfToggle()) {
        lowShelfButton = std::make_unique<ShelfIconButton>(ShelfIconButton::Kind::lowShelf);
        configureShelfButton(*lowShelfButton, identity.lowShelfTooltip);
    }

    if (identity.supportsHighShelfToggle()) {
        highShelfButton = std::make_unique<ShelfIconButton>(ShelfIconButton::Kind::highShelf);
        configureShelfButton(*highShelfButton, identity.highShelfTooltip);
    }

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
    if (identity.supportsLearnButton())
        learnAttachment = std::make_unique<ButtonAttachment>(state, identity.learnParamId.data(), learnButton);
    if (identity.supportsLowShelfToggle() && lowShelfButton != nullptr)
        lowShelfAttachment = std::make_unique<ButtonAttachment>(state, identity.lowShelfParamId.data(), *lowShelfButton);
    if (identity.supportsHighShelfToggle() && highShelfButton != nullptr)
        highShelfAttachment = std::make_unique<ButtonAttachment>(state, identity.highShelfParamId.data(), *highShelfButton);
    primaryAttachment = std::make_unique<SliderAttachment>(state, identity.primaryParamId.data(), primarySlider);
    secondaryAttachment = std::make_unique<SliderAttachment>(state, identity.secondaryParamId.data(), secondarySlider);
    if (identity.supportsTertiaryControl())
        tertiaryAttachment = std::make_unique<SliderAttachment>(state, identity.tertiaryParamId.data(), tertiarySlider);

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

    g.setColour(lookAndFeel.findColour(juce::Label::textColourId).withAlpha(0.45f));
    g.setFont(juce::FontOptions().withHeight(static_cast<float>(scaled(11))));
    g.drawFittedText(footerText(),
                     getLocalBounds().reduced(scaled(24), scaled(18)),
                     juce::Justification::bottomRight,
                     1);

}

void EditorBase::resized() {
    auto bounds = getLocalBounds().reduced(scaled(20), scaled(16));
    auto header = bounds.removeFromTop(processor.getProductIdentity().supportsLearnButton() ? scaled(130) : scaled(104));
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
    if (processor.getProductIdentity().supportsLearnButton()) {
        learnButton.setBounds(modeRow.removeFromLeft(scaled(88)).reduced(0, scaled(2)));
        modeRow.removeFromLeft(scaled(12));
    }

    const int iconSize = scaled(28);
    if (lowShelfButton != nullptr) {
        lowShelfButton->setBounds(modeRow.removeFromLeft(iconSize).reduced(0, scaled(1)));
        modeRow.removeFromLeft(scaled(10));
    }
    if (highShelfButton != nullptr) {
        highShelfButton->setBounds(modeRow.removeFromLeft(iconSize).reduced(0, scaled(1)));
        modeRow.removeFromLeft(scaled(14));
    }

    statusLabel.setBounds(modeRow);
    if (processor.getProductIdentity().supportsLearnButton()) {
        header.removeFromTop(scaled(6));
        auto learnRow = header.removeFromTop(scaled(18));
        learnMeterLabel.setBounds(learnRow.removeFromLeft(scaled(118)));
        learnRow.removeFromLeft(scaled(8));
        learnMeterBar.setBounds(learnRow.removeFromLeft(scaled(180)));
    }

    const bool hasTertiary = processor.getProductIdentity().supportsTertiaryControl();
    const bool stacked = body.getWidth() < scaled(hasTertiary ? 760 : 560);
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

void EditorBase::configureShelfButton(ShelfIconButton& button, const std::string_view tooltip) {
    button.setClickingTogglesState(true);
    button.setWantsKeyboardFocus(true);
    button.setTooltip(toJuceString(tooltip));
    addAndMakeVisible(button);
}

juce::String EditorBase::footerText() const {
    return juce::String("v") + juce::String(VXSUITE_VERSION_STRING) + "    (c) Andrzej Marczewski 2026";
}

int EditorBase::scaled(const int value) const {
    return juce::roundToInt(static_cast<float>(value) * uiScale);
}

void EditorBase::timerCallback() {
    statusLabel.setText(processor.getStatusText(), juce::dontSendNotification);
    updateActivityIndicators();
    updateLearnUi();
}

void EditorBase::updateActivityIndicators() {
    lowShelfActivity = juce::jlimit(0.0f, 1.0f, processor.getLowShelfActivity());
    highShelfActivity = juce::jlimit(0.0f, 1.0f, processor.getHighShelfActivity());
    if (lowShelfButton != nullptr)
        lowShelfButton->setActivity(lowShelfActivity);
    if (highShelfButton != nullptr)
        highShelfButton->setActivity(highShelfActivity);
}

void EditorBase::updateLearnUi() {
    if (!processor.getProductIdentity().supportsLearnButton())
        return;

    const bool active = processor.isLearnActive();
    const bool ready = processor.isLearnReady();
    const float progress = juce::jlimit(0.0f, 1.0f, processor.getLearnProgress());
    const float confidence = juce::jlimit(0.0f, 1.0f, processor.getLearnConfidence());
    const float observedSeconds = juce::jmax(0.0f, processor.getLearnObservedSeconds());

    learnMeterUi = active ? static_cast<double>(progress) : static_cast<double>(confidence);
    learnMeterBar.setVisible(active || ready);
    if (active) {
        learnMeterLabel.setText("Learning " + juce::String(juce::roundToInt(progress * 100.0f)) + "%  "
                                + juce::String(observedSeconds, 1) + "s",
                                juce::dontSendNotification);
        learnButton.setButtonText("Learning");
    } else if (ready) {
        learnMeterLabel.setText("Confidence " + juce::String(juce::roundToInt(confidence * 100.0f)) + "%",
                                juce::dontSendNotification);
        learnButton.setButtonText("Learn");
        if (learnButton.getToggleState())
            learnButton.setToggleState(false, juce::sendNotificationSync);
    } else {
        learnMeterLabel.setText("Learn a representative noise print", juce::dontSendNotification);
        learnButton.setButtonText("Learn");
    }
}

} // namespace vxsuite
