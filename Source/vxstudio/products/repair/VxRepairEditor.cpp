#include "VxRepairEditor.h"
#include "../../framework/VxStudioParameters.h"

namespace {

constexpr int kEditorW = 760;
constexpr int kEditorH = 580;

constexpr int kHeaderH  = 60;
constexpr int kStatusH  = 30;
constexpr int kFooterH  = 70;    // taller footer to fit makeup knob
constexpr int kRowH     = 110;   // per tool row
constexpr int kRowPad   = 8;

juce::Colour fromRgb(const std::array<float, 3>& c) {
    return juce::Colour::fromFloatRGBA(c[0], c[1], c[2], 1.0f);
}

// Thresholds match RepairAnalyser::kActiveThreshold (0.10)
juce::Colour scoreColour(float score) {
    if (score < 0.10f) return juce::Colour(0xff3a4a3a);   // inactive grey-green
    if (score < 0.40f) return juce::Colour(0xffbbaa00);   // amber
    if (score < 0.70f) return juce::Colour(0xffdd6622);   // orange
    return juce::Colour(0xffdd2222);                       // red
}

juce::String scoreLabel(float score) {
    if (score < 0.10f) return "Clean";
    if (score < 0.40f) return "Mild";
    if (score < 0.70f) return "Moderate";
    return "Heavy";
}

} // namespace

VXRepairEditor::VXRepairEditor(VXRepairAudioProcessor& p)
    : juce::AudioProcessorEditor(p)
    , laf(p.getProductIdentity().theme)
    , repairProcessor(p)
{
    setLookAndFeel(&laf);
    setResizable(true, false);
    setResizeLimits(kEditorW, kEditorH, static_cast<int>(kEditorW * 1.5), static_cast<int>(kEditorH * 1.5));
    setSize(kEditorW, kEditorH);

    const auto& identity = p.getProductIdentity();
    const auto accent = fromRgb(identity.theme.accentRgb);
    const auto text   = fromRgb(identity.theme.textRgb);

    // Header
    suiteLabel.setText("VX Suite", juce::dontSendNotification);
    suiteLabel.setFont(juce::FontOptions().withHeight(14.0f).withKerningFactor(0.18f));
    suiteLabel.setColour(juce::Label::textColourId, accent.withAlpha(0.65f));
    addAndMakeVisible(suiteLabel);

    productLabel.setText("Repair", juce::dontSendNotification);
    productLabel.setFont(juce::FontOptions().withHeight(26.0f).withStyle("Bold"));
    productLabel.setColour(juce::Label::textColourId, text);
    addAndMakeVisible(productLabel);

    statusLabel.setFont(juce::FontOptions().withHeight(13.5f));
    statusLabel.setColour(juce::Label::textColourId, text.withAlpha(0.55f));
    statusLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);

    // Idle state
    analyseButton.setButtonText("Analyse");
    analyseButton.setColour(juce::TextButton::buttonColourId, accent.withAlpha(0.88f));
    analyseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black.withAlpha(0.85f));
    analyseButton.onClick = [this] {
        repairProcessor.triggerAnalysis();
        uiState = UIState::Collecting;
        showIdleState();
    };
    addAndMakeVisible(analyseButton);

    promptLabel.setText("Play a representative 5-second section of your audio", juce::dontSendNotification);
    promptLabel.setFont(juce::FontOptions().withHeight(14.5f));
    promptLabel.setColour(juce::Label::textColourId, text.withAlpha(0.65f));
    promptLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(promptLabel);

    collectingLabel.setFont(juce::FontOptions().withHeight(16.0f));
    collectingLabel.setColour(juce::Label::textColourId, text);
    collectingLabel.setJustificationType(juce::Justification::centred);
    addChildComponent(collectingLabel);

    // Tool rows
    const char* toolNames[3]      = { "Noise", "Speech Clarity", "Reverb" };
    const char* strengthIds[3]    = { "noise_strength",   "clarity_strength", "reverb_strength"  };
    const char* onIds[3]          = { "noise_on",         "clarity_on",       "reverb_on"        };
    const char* listenIds[3]      = { "noise_listen",     "clarity_listen",   "reverb_listen"    };
    auto& apvts = p.getValueTreeState();

    for (int i = 0; i < 3; ++i) {
        auto& row = rows[static_cast<size_t>(i)];

        row.nameLabel.setText(toolNames[i], juce::dontSendNotification);
        row.nameLabel.setFont(juce::FontOptions().withHeight(16.0f).withStyle("Bold"));
        row.nameLabel.setColour(juce::Label::textColourId, text);
        addChildComponent(row.nameLabel);

        row.confidenceLabel.setFont(juce::FontOptions().withHeight(12.5f));
        row.confidenceLabel.setJustificationType(juce::Justification::centredLeft);
        addChildComponent(row.confidenceLabel);

        row.strengthSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        row.strengthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        row.strengthSlider.setRange(0.0, 1.0, 0.001);
        addChildComponent(row.strengthSlider);

        row.strengthLabel.setText("Strength", juce::dontSendNotification);
        row.strengthLabel.setFont(juce::FontOptions().withHeight(11.5f));
        row.strengthLabel.setColour(juce::Label::textColourId, text.withAlpha(0.5f));
        row.strengthLabel.setJustificationType(juce::Justification::centred);
        addChildComponent(row.strengthLabel);

        row.listenButton.setButtonText("Listen");
        row.listenButton.setClickingTogglesState(true);
        row.listenButton.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2e2416));
        row.listenButton.setColour(juce::TextButton::buttonOnColourId,  accent.withAlpha(0.72f));
        row.listenButton.setColour(juce::TextButton::textColourOffId,  text.withAlpha(0.75f));
        row.listenButton.setColour(juce::TextButton::textColourOnId,   juce::Colours::black);
        addChildComponent(row.listenButton);

        row.bypassButton.setButtonText("Off");
        row.bypassButton.setClickingTogglesState(true);
        row.bypassButton.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2e2416));
        row.bypassButton.setColour(juce::TextButton::buttonOnColourId,  accent);
        row.bypassButton.setColour(juce::TextButton::textColourOffId,  text.withAlpha(0.75f));
        row.bypassButton.setColour(juce::TextButton::textColourOnId,   juce::Colours::black);
        addChildComponent(row.bypassButton);

        row.strengthAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, strengthIds[i], row.strengthSlider);
        row.listenAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, listenIds[i], row.listenButton);
        row.bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, onIds[i], row.bypassButton);
    }

    // Makeup gain
    makeupSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    makeupSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, scaled(52), scaled(16));
    makeupSlider.setRange(-12.0, 12.0, 0.1);
    makeupSlider.setDoubleClickReturnValue(true, 0.0);
    makeupSlider.setColour(juce::Slider::textBoxTextColourId, text.withAlpha(0.7f));
    makeupSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    makeupSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addChildComponent(makeupSlider);

    makeupLabel.setText("Makeup", juce::dontSendNotification);
    makeupLabel.setFont(juce::FontOptions().withHeight(12.5f));
    makeupLabel.setColour(juce::Label::textColourId, text.withAlpha(0.55f));
    makeupLabel.setJustificationType(juce::Justification::centred);
    addChildComponent(makeupLabel);

    makeupAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "makeup_gain", makeupSlider);

    // Footer
    resetButton.setButtonText("Reset Analysis");
    resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e1e1e));
    resetButton.setColour(juce::TextButton::textColourOffId, text.withAlpha(0.55f));
    resetButton.onClick = [this] {
        repairProcessor.resetAnalysis();
        uiState = UIState::Idle;
        showIdleState();
    };
    addChildComponent(resetButton);

    deepFilterNote.setText("Tip: for complex background noise, also run VX Deep Filter Net in your chain.",
                           juce::dontSendNotification);
    deepFilterNote.setFont(juce::FontOptions().withHeight(11.5f));
    deepFilterNote.setColour(juce::Label::textColourId, text.withAlpha(0.38f));
    deepFilterNote.setJustificationType(juce::Justification::centred);
    addChildComponent(deepFilterNote);

    // If a previous analysis was saved with the project, restore the repair screen.
    if (repairProcessor.isAnalysisComplete()) {
        lastAssessment = repairProcessor.getAssessment();
        uiState = UIState::Repair;
        buildRepairRows();
        showRepairState();
    }

    startTimerHz(15);
}

VXRepairEditor::~VXRepairEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void VXRepairEditor::timerCallback() {
    statusLabel.setText(repairProcessor.getStatusText(), juce::dontSendNotification);

    // Keep On/Off label in sync with toggle state
    for (auto& row : rows) {
        row.bypassButton.setButtonText(row.bypassButton.getToggleState() ? "On" : "Off");
    }

    if (uiState == UIState::Collecting) {
        const float prog = repairProcessor.getAnalysisProgress();
        collectingLabel.setText("Analysing...  " + juce::String(static_cast<int>(prog * 100.0f)) + "%",
                                juce::dontSendNotification);
        repaint();

        if (repairProcessor.isAnalysisComplete()) {
            repairProcessor.applyAssessmentToParams();
            lastAssessment = repairProcessor.getAssessment();
            uiState = UIState::Repair;
            buildRepairRows();
            showRepairState();
        }
    }
}

void VXRepairEditor::showIdleState() {
    analyseButton.setVisible(uiState == UIState::Idle);
    promptLabel.setVisible(uiState == UIState::Idle);
    collectingLabel.setVisible(uiState == UIState::Collecting);

    for (auto& row : rows) {
        row.nameLabel.setVisible(false);
        row.confidenceLabel.setVisible(false);
        row.strengthSlider.setVisible(false);
        row.strengthLabel.setVisible(false);
        row.listenButton.setVisible(false);
        row.bypassButton.setVisible(false);
    }
    resetButton.setVisible(false);
    deepFilterNote.setVisible(false);
    makeupSlider.setVisible(false);
    makeupLabel.setVisible(false);
    resized();
    repaint();
}

void VXRepairEditor::buildRepairRows() {
    // Row order: Noise, Speech Clarity, Reverb
    const float scores[3] = { lastAssessment.noiseScore,
                               lastAssessment.humMudScore,
                               lastAssessment.reverbScore };
    const auto& theme = repairProcessor.getProductIdentity().theme;
    const auto text = fromRgb(theme.textRgb);

    for (int i = 0; i < 3; ++i) {
        auto& row = rows[static_cast<size_t>(i)];
        const float score = scores[i];
        const auto colour = scoreColour(score);

        row.confidenceLabel.setText(scoreLabel(score), juce::dontSendNotification);
        row.confidenceLabel.setColour(juce::Label::textColourId, colour.interpolatedWith(text, 0.3f));
        row.nameLabel.setColour(juce::Label::textColourId,
                                score >= 0.10f ? text : text.withAlpha(0.42f));
    }
}

void VXRepairEditor::showRepairState() {
    analyseButton.setVisible(false);
    promptLabel.setVisible(false);
    collectingLabel.setVisible(false);

    for (auto& row : rows) {
        row.nameLabel.setVisible(true);
        row.confidenceLabel.setVisible(true);
        row.strengthSlider.setVisible(true);
        row.strengthLabel.setVisible(true);
        row.listenButton.setVisible(true);
        row.bypassButton.setVisible(true);
    }
    resetButton.setVisible(true);
    deepFilterNote.setVisible(lastAssessment.noiseScore >= 0.10f);
    makeupSlider.setVisible(true);
    makeupLabel.setVisible(true);
    resized();
    repaint();
}

void VXRepairEditor::paint(juce::Graphics& g) {
    const auto& theme = repairProcessor.getProductIdentity().theme;
    g.fillAll(fromRgb(theme.backgroundRgb));

    // Header separator
    const auto panel = fromRgb(theme.panelRgb);
    g.setColour(panel.brighter(0.06f));
    g.fillRect(0, scaled(kHeaderH + kStatusH), getWidth(), 1);

    if (uiState == UIState::Idle)        paintIdle(g);
    else if (uiState == UIState::Collecting) paintCollecting(g);
    else                                  paintRepair(g);

    // Footer separator
    g.setColour(panel.brighter(0.04f));
    g.fillRect(0, getHeight() - scaled(kFooterH), getWidth(), 1);
}

void VXRepairEditor::paintIdle(juce::Graphics& /*g*/) {}

void VXRepairEditor::paintCollecting(juce::Graphics& g) {
    const auto& theme = repairProcessor.getProductIdentity().theme;
    const auto accent = fromRgb(theme.accentRgb);

    const float prog = repairProcessor.getAnalysisProgress();
    const float cx = getWidth() * 0.5f;
    const float cy = static_cast<float>(scaled(kHeaderH + kStatusH)) + static_cast<float>(getHeight() - scaled(kHeaderH + kStatusH + kFooterH)) * 0.38f;
    const float radius = static_cast<float>(scaled(44));

    // Progress arc
    g.setColour(accent.withAlpha(0.12f));
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 2.0f);
    juce::Path arc;
    const float startAngle = -juce::MathConstants<float>::halfPi;
    const float endAngle   = startAngle + juce::MathConstants<float>::twoPi * prog;
    arc.addArc(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, startAngle, endAngle, true);
    g.setColour(accent.withAlpha(0.85f));
    g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void VXRepairEditor::paintRepair(juce::Graphics& g) {
    const auto& theme = repairProcessor.getProductIdentity().theme;
    const auto panel  = fromRgb(theme.panelRgb);

    const int bodyTop = scaled(kHeaderH + kStatusH + 4);
    const int x = scaled(20);
    const int w = getWidth() - scaled(40);

    for (int i = 0; i < 3; ++i) {
        const int y = bodyTop + i * scaled(kRowH + kRowPad);
        const float scores[3] = { lastAssessment.humMudScore,
                                   lastAssessment.noiseScore,
                                   lastAssessment.reverbScore };
        const float alpha = scores[i] >= 0.10f ? 1.0f : 0.55f;
        g.setColour(panel.withAlpha(alpha));
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(w), static_cast<float>(scaled(kRowH)),
                               static_cast<float>(scaled(6)));

        // Score dot
        const auto dotColour = scoreColour(scores[i]);
        g.setColour(dotColour);
        const float dotR = static_cast<float>(scaled(5));
        g.fillEllipse(static_cast<float>(x + scaled(14)),
                      static_cast<float>(y + scaled(kRowH / 2)) - dotR,
                      dotR * 2.0f, dotR * 2.0f);
    }
}

void VXRepairEditor::resized() {
    uiScale = static_cast<float>(getWidth()) / static_cast<float>(kEditorW);

    // Header
    const int headerTop = scaled(12);
    suiteLabel.setBounds(scaled(24), headerTop, scaled(120), scaled(20));
    productLabel.setBounds(scaled(24), headerTop + scaled(18), scaled(200), scaled(32));
    statusLabel.setBounds(getWidth() - scaled(280), headerTop + scaled(22), scaled(256), scaled(20));

    // Status strip
    const int bodyTop = scaled(kHeaderH + kStatusH + 4);
    const int bodyH   = getHeight() - bodyTop - scaled(kFooterH);

    if (uiState == UIState::Idle) {
        const int btnW = scaled(160);
        const int btnH = scaled(44);
        const int btnX = (getWidth() - btnW) / 2;
        const int btnY = bodyTop + (bodyH - btnH) / 2 - scaled(20);
        analyseButton.setBounds(btnX, btnY, btnW, btnH);
        promptLabel.setBounds(scaled(40), btnY + btnH + scaled(14), getWidth() - scaled(80), scaled(24));

    } else if (uiState == UIState::Collecting) {
        const int labelW = scaled(260);
        const int labelH = scaled(28);
        collectingLabel.setBounds((getWidth() - labelW) / 2,
                                  bodyTop + bodyH / 2 + scaled(52),
                                  labelW, labelH);

    } else {  // Repair
        const int rowX = scaled(20);
        const int rowW = getWidth() - scaled(40);

        for (int i = 0; i < 3; ++i) {
            auto& row = rows[static_cast<size_t>(i)];
            const int rowTop = bodyTop + i * scaled(kRowH + kRowPad);
            const int rowH   = scaled(kRowH);
            const int pad    = scaled(28);

            // Name + confidence (left column)
            row.nameLabel.setBounds(rowX + pad, rowTop + scaled(14), scaled(130), scaled(22));
            row.confidenceLabel.setBounds(rowX + pad, rowTop + scaled(38), scaled(130), scaled(18));

            // Strength knob (centre-left)
            const int knobSize = scaled(82);
            const int knobX    = rowX + scaled(170);
            const int knobY    = rowTop + (rowH - knobSize) / 2 - scaled(8);
            row.strengthSlider.setBounds(knobX, knobY, knobSize, knobSize);
            row.strengthLabel.setBounds(knobX, knobY + knobSize, knobSize, scaled(14));

            // Buttons (right side)
            const int btnH    = scaled(32);
            const int btnW    = scaled(80);
            const int btnGap  = scaled(10);
            const int btnY    = rowTop + (rowH - btnH) / 2;
            const int listenX = rowX + rowW - scaled(20) - btnW * 2 - btnGap;
            row.listenButton.setBounds(listenX,               btnY, btnW, btnH);
            row.bypassButton.setBounds(listenX + btnW + btnGap, btnY, btnW, btnH);
        }

        // Footer row
        const int footerY = getHeight() - scaled(kFooterH);
        resetButton.setBounds(scaled(24), footerY + scaled(11), scaled(120), scaled(28));
        deepFilterNote.setBounds(scaled(160), footerY + scaled(16),
                                 getWidth() - scaled(340), scaled(22));

        // Makeup gain knob — right side of footer
        const int mkSize = scaled(46);
        const int mkX    = getWidth() - scaled(22) - mkSize;
        const int mkY    = footerY + scaled(8);
        makeupSlider.setBounds(mkX, mkY, mkSize, mkSize + scaled(16));
        makeupLabel.setBounds(mkX - scaled(6), footerY + scaled(2), mkSize + scaled(12), scaled(14));
    }
}
