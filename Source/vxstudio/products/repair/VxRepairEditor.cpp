#include "VxRepairEditor.h"
#include "../../framework/VxStudioParameters.h"

namespace {

constexpr int kEditorW = 760;
constexpr int kEditorH = 660;

constexpr int kHeaderH  = 60;
constexpr int kStatusH  = 30;
constexpr int kFooterH  = 70;
constexpr int kRowH     = 110;
constexpr int kRowPad   = 8;

juce::Colour fromRgb(const std::array<float, 3>& c) {
    return juce::Colour::fromFloatRGBA(c[0], c[1], c[2], 1.0f);
}

juce::Colour scoreColour(float score) {
    if (score < 0.10f) return juce::Colour(0xff3a4a3a);
    if (score < 0.40f) return juce::Colour(0xffbbaa00);
    if (score < 0.70f) return juce::Colour(0xffdd6622);
    return juce::Colour(0xffdd2222);
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

    // Help button
    helpButton.onClick = [this] {
        vxsuite::showHelpDialog(*this, repairProcessor.getProductIdentity());
    };
    addAndMakeVisible(helpButton);

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

    phaseLabel.setFont(juce::FontOptions().withHeight(13.0f));
    phaseLabel.setColour(juce::Label::textColourId, text.withAlpha(0.55f));
    phaseLabel.setJustificationType(juce::Justification::centred);
    addChildComponent(phaseLabel);

    // Tool rows
    const char* toolNames[4]      = { "Noise", "Speech Clarity", "Reverb", "Clicks & Pops" };
    const char* strengthIds[4]    = { "noise_strength",   "clarity_strength", "reverb_strength",  "click_strength"  };
    const char* onIds[4]          = { "noise_on",         "clarity_on",       "reverb_on",        "click_on"        };
    const char* listenIds[4]      = { "noise_listen",     "clarity_listen",   "reverb_listen",    "click_listen"    };
    auto& apvts = p.getValueTreeState();

    for (int i = 0; i < 4; ++i) {
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

        // Auto-enable the tool when the user starts dragging the strength knob
        const juce::String onId { onIds[i] };
        row.strengthSlider.onDragStart = [this, onId] {
            if (auto* p = repairProcessor.getValueTreeState().getParameter(onId))
                if (p->getValue() < 0.5f)
                    p->setValueNotifyingHost(1.0f);
        };

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

    // DeepFilter toggle  -  Noise row only
    deepFilterToggle.setButtonText("DeepFilter");
    deepFilterToggle.setClickingTogglesState(true);
    deepFilterToggle.setColour(juce::ToggleButton::textColourId,         text.withAlpha(0.75f));
    deepFilterToggle.setColour(juce::ToggleButton::tickColourId,         accent);
    deepFilterToggle.setColour(juce::ToggleButton::tickDisabledColourId, text.withAlpha(0.30f));
    addChildComponent(deepFilterToggle);

    deepFilterStatus.setFont(juce::FontOptions().withHeight(11.0f));
    deepFilterStatus.setColour(juce::Label::textColourId, text.withAlpha(0.45f));
    deepFilterStatus.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(deepFilterStatus);

    deepFilterAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "noise_deepfilter", deepFilterToggle);

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

    // Restore repair state from previously saved analysis
    if (repairProcessor.isAnalysisComplete()) {
        lastAssessment = repairProcessor.getAssessment();
        uiState = UIState::Repair;
        buildRepairRows();
        showRepairState();
    }

    startTimerHz(30);
}

VXRepairEditor::~VXRepairEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void VXRepairEditor::timerCallback() {
    statusLabel.setText(repairProcessor.getStatusText(), juce::dontSendNotification);

    for (auto& row : rows)
        row.bypassButton.setButtonText(row.bypassButton.getToggleState() ? "On" : "Off");

    // Update DeepFilter status label while in Repair state
    if (uiState == UIState::Repair) {
        const bool dfOn = deepFilterToggle.getToggleState();
        deepFilterStatus.setVisible(dfOn);
        if (dfOn) {
            if (repairProcessor.isDeepFilterReady())
                deepFilterStatus.setText("Model ready", juce::dontSendNotification);
            else if (repairProcessor.isDeepFilterPrepared())
                deepFilterStatus.setText("Loading model...", juce::dontSendNotification);
            else
                deepFilterStatus.setText("Model not installed  -  use VX Deep Filter Net to install",
                                         juce::dontSendNotification);
        }
    }

    if (uiState == UIState::Collecting) {
        const float prog = repairProcessor.getAnalysisProgress();
        collectingLabel.setText("Analysing...  " + juce::String(static_cast<int>(prog * 100.0f)) + "%",
                                juce::dontSendNotification);

        const auto phase = repairProcessor.getAnalysisPhase();
        using Phase = VXRepairAudioProcessor::AnalysisPhase;
        if (phase == Phase::Phase2)
            phaseLabel.setText("Phase 2  -  reverb & clarity (noise-corrected)", juce::dontSendNotification);
        else
            phaseLabel.setText("Phase 1  -  detecting noise level", juce::dontSendNotification);

        std::array<float, 24> raw {};
        repairProcessor.getDisplayBands(raw);
        for (int i = 0; i < 24; ++i)
            smoothedBands[static_cast<size_t>(i)] = 0.72f * smoothedBands[static_cast<size_t>(i)]
                                                   + 0.28f * raw[static_cast<size_t>(i)];
        repaint();

        if (repairProcessor.isAnalysisComplete()) {
            repairProcessor.applyAssessmentToParams();
            lastAssessment = repairProcessor.getAssessment();
            uiState = UIState::Repair;
            buildRepairRows();
            showRepairState();
        }
    } else if (uiState == UIState::Repair) {
        const auto act = repairProcessor.getToolActivity();
        noiseActivityDisplay   = 0.80f * noiseActivityDisplay   + 0.20f * act.noise;
        clarityActivityDisplay = 0.80f * clarityActivityDisplay + 0.20f * act.clarity;
        reverbActivityDisplay  = 0.80f * reverbActivityDisplay  + 0.20f * act.reverb;
        clickActivityDisplay   = 0.80f * clickActivityDisplay   + 0.20f * act.click;
        repaint();
    }
}

void VXRepairEditor::showIdleState() {
    analyseButton.setVisible(uiState == UIState::Idle);
    promptLabel.setVisible(uiState == UIState::Idle);
    collectingLabel.setVisible(uiState == UIState::Collecting);
    phaseLabel.setVisible(uiState == UIState::Collecting);

    for (auto& row : rows) {
        row.nameLabel.setVisible(false);
        row.confidenceLabel.setVisible(false);
        row.strengthSlider.setVisible(false);
        row.strengthLabel.setVisible(false);
        row.listenButton.setVisible(false);
        row.bypassButton.setVisible(false);
    }
    resetButton.setVisible(false);
    deepFilterToggle.setVisible(false);
    deepFilterStatus.setVisible(false);
    makeupSlider.setVisible(false);
    makeupLabel.setVisible(false);
    resized();
    repaint();
}

void VXRepairEditor::buildRepairRows() {
    const float scores[4] = { lastAssessment.noiseScore,
                               lastAssessment.humMudScore,
                               lastAssessment.reverbScore,
                               lastAssessment.clickScore };
    const auto& theme = repairProcessor.getProductIdentity().theme;
    const auto text = fromRgb(theme.textRgb);

    for (int i = 0; i < 4; ++i) {
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
    phaseLabel.setVisible(false);

    for (auto& row : rows) {
        row.nameLabel.setVisible(true);
        row.confidenceLabel.setVisible(true);
        row.strengthSlider.setVisible(true);
        row.strengthLabel.setVisible(true);
        row.listenButton.setVisible(true);
        row.bypassButton.setVisible(true);
    }
    resetButton.setVisible(true);
    deepFilterToggle.setVisible(true);
    // deepFilterStatus visibility is managed by timerCallback
    makeupSlider.setVisible(true);
    makeupLabel.setVisible(true);
    resized();
    repaint();
}

void VXRepairEditor::paint(juce::Graphics& g) {
    const auto& theme = repairProcessor.getProductIdentity().theme;
    g.fillAll(fromRgb(theme.backgroundRgb));

    const auto panel = fromRgb(theme.panelRgb);
    g.setColour(panel.brighter(0.06f));
    g.fillRect(0, scaled(kHeaderH + kStatusH), getWidth(), 1);

    if (uiState == UIState::Idle)            paintIdle(g);
    else if (uiState == UIState::Collecting) paintCollecting(g);
    else                                     paintRepair(g);

    g.setColour(panel.brighter(0.04f));
    g.fillRect(0, getHeight() - scaled(kFooterH), getWidth(), 1);
}

void VXRepairEditor::paintIdle(juce::Graphics& /*g*/) {}

void VXRepairEditor::paintCollecting(juce::Graphics& g) {
    const auto& theme = repairProcessor.getProductIdentity().theme;
    const auto accent = fromRgb(theme.accentRgb);
    const auto text   = fromRgb(theme.textRgb);

    const float prog = repairProcessor.getAnalysisProgress();
    const float cx = getWidth() * 0.5f;
    const int bodyTop = scaled(kHeaderH + kStatusH);
    const int bodyH   = getHeight() - bodyTop - scaled(kFooterH);
    const float cy = static_cast<float>(bodyTop) + static_cast<float>(bodyH) * 0.34f;
    const float radius = static_cast<float>(scaled(42));

    // Progress ring  -  second phase shown in slightly brighter accent
    const auto phase = repairProcessor.getAnalysisPhase();
    const float ringAlpha = (phase == VXRepairAudioProcessor::AnalysisPhase::Phase2) ? 0.14f : 0.10f;
    g.setColour(accent.withAlpha(ringAlpha));
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 2.5f);
    if (prog > 0.0f) {
        juce::Path arc;
        const float startAngle = -juce::MathConstants<float>::halfPi;
        arc.addArc(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f,
                   startAngle, startAngle + juce::MathConstants<float>::twoPi * prog, true);
        g.setColour(accent.withAlpha(0.90f));
        g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour(text.withAlpha(0.80f));
    g.setFont(juce::FontOptions().withHeight(static_cast<float>(scaled(18))).withStyle("Bold"));
    g.drawText(juce::String(static_cast<int>(prog * 100)) + "%",
               static_cast<int>(cx - radius), static_cast<int>(cy - radius),
               static_cast<int>(radius * 2.0f), static_cast<int>(radius * 2.0f),
               juce::Justification::centred);

    // Live spectrum bars below the ring
    const int barAreaTop  = static_cast<int>(cy) + scaled(52);
    const int barAreaH    = scaled(70);
    const int barAreaX    = scaled(40);
    const int barAreaW    = getWidth() - scaled(80);
    const float barW      = static_cast<float>(barAreaW) / 24.0f;
    const float gap       = barW * 0.18f;

    for (int i = 0; i < 24; ++i) {
        const float v    = smoothedBands[static_cast<size_t>(i)];
        const float barH = std::max(3.0f, v * static_cast<float>(barAreaH));
        const float x    = static_cast<float>(barAreaX) + static_cast<float>(i) * barW + gap * 0.5f;
        const float w    = barW - gap;
        const float y    = static_cast<float>(barAreaTop + barAreaH) - barH;

        const float hue = 0.08f + static_cast<float>(i) / 23.0f * 0.15f;
        g.setColour(juce::Colour::fromHSV(hue, 0.75f, 0.82f, 0.72f + 0.25f * v));
        g.fillRoundedRectangle(x, y, w, barH, 1.5f);
    }

    g.setColour(text.withAlpha(0.08f));
    g.fillRect(barAreaX, barAreaTop + barAreaH, barAreaW, 1);
}

void VXRepairEditor::paintRepair(juce::Graphics& g) {
    const auto& theme = repairProcessor.getProductIdentity().theme;
    const auto panel  = fromRgb(theme.panelRgb);
    const auto accent = fromRgb(theme.accentRgb);
    const auto text   = fromRgb(theme.textRgb);

    const int bodyTop = scaled(kHeaderH + kStatusH + 4);
    const int rowX    = scaled(20);
    const int rowW    = getWidth() - scaled(40);
    const int rowH    = scaled(kRowH);

    const float scores[4]     = { lastAssessment.noiseScore,
                                   lastAssessment.humMudScore,
                                   lastAssessment.reverbScore,
                                   lastAssessment.clickScore };
    const float activities[4] = { noiseActivityDisplay,
                                   clarityActivityDisplay,
                                   reverbActivityDisplay,
                                   clickActivityDisplay };

    for (int i = 0; i < 4; ++i) {
        const int   y     = bodyTop + i * scaled(kRowH + kRowPad);
        const float score = scores[i];
        const bool  active = score >= 0.10f;
        const float alpha = active ? 1.0f : 0.60f;

        g.setColour(panel.withAlpha(alpha));
        g.fillRoundedRectangle(static_cast<float>(rowX), static_cast<float>(y),
                               static_cast<float>(rowW), static_cast<float>(rowH),
                               static_cast<float>(scaled(6)));

        const auto stripColour = active ? scoreColour(score) : scoreColour(0.0f);
        g.setColour(stripColour.withAlpha(active ? 0.90f : 0.30f));
        g.fillRoundedRectangle(static_cast<float>(rowX), static_cast<float>(y),
                               static_cast<float>(scaled(4)), static_cast<float>(rowH),
                               static_cast<float>(scaled(3)));

        const int barX  = rowX + scaled(28);
        const int barY  = y + scaled(62);
        const int barW  = scaled(132);
        const int barH2 = scaled(6);

        g.setColour(text.withAlpha(0.08f));
        g.fillRoundedRectangle(static_cast<float>(barX), static_cast<float>(barY),
                               static_cast<float>(barW), static_cast<float>(barH2),
                               static_cast<float>(barH2 / 2));

        if (score > 0.01f) {
            const float fill = juce::jlimit(0.0f, 1.0f, score);
            const auto  barCol = scoreColour(score);
            juce::ColourGradient grad(barCol.withAlpha(0.90f), static_cast<float>(barX), 0.0f,
                                      barCol.withAlpha(0.30f), static_cast<float>(barX) + fill * static_cast<float>(barW), 0.0f,
                                      false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(static_cast<float>(barX), static_cast<float>(barY),
                                   fill * static_cast<float>(barW), static_cast<float>(barH2),
                                   static_cast<float>(barH2 / 2));
        }

        const float act = activities[i];
        if (act > 0.005f) {
            const int meterX = rowX + rowW - scaled(10);
            const int meterW = scaled(4);
            const float meterH = static_cast<float>(rowH - scaled(16)) * act;
            const float meterY = static_cast<float>(y + scaled(8)) + static_cast<float>(rowH - scaled(16)) * (1.0f - act);
            g.setColour(accent.withAlpha(0.25f));
            g.fillRoundedRectangle(static_cast<float>(meterX), static_cast<float>(y + scaled(8)),
                                   static_cast<float>(meterW), static_cast<float>(rowH - scaled(16)),
                                   static_cast<float>(meterW / 2));
            g.setColour(accent.withAlpha(0.80f + 0.20f * act));
            g.fillRoundedRectangle(static_cast<float>(meterX), meterY,
                                   static_cast<float>(meterW), meterH,
                                   static_cast<float>(meterW / 2));
        }
    }
}

void VXRepairEditor::resized() {
    uiScale = static_cast<float>(getWidth()) / static_cast<float>(kEditorW);

    // Header
    const int headerTop = scaled(12);
    suiteLabel.setBounds(scaled(24), headerTop, scaled(120), scaled(20));
    productLabel.setBounds(scaled(24), headerTop + scaled(18), scaled(200), scaled(32));

    // Help button  -  top-right of header
    const int helpW = scaled(58);
    const int helpH = scaled(24);
    helpButton.setBounds(getWidth() - scaled(20) - helpW, headerTop + scaled(20), helpW, helpH);

    statusLabel.setBounds(getWidth() - scaled(280), headerTop + scaled(22), scaled(256) - helpW - scaled(8), scaled(20));

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
        phaseLabel.setBounds((getWidth() - labelW) / 2,
                             bodyTop + bodyH / 2 + scaled(52) + scaled(26),
                             labelW, scaled(22));

    } else {  // Repair
        const int rowX = scaled(20);
        const int rowW = getWidth() - scaled(40);

        for (int i = 0; i < 4; ++i) {
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

        // DeepFilter toggle  -  Noise row (row 0), below the confidence label
        {
            const int row0Top = bodyTop;
            const int pad = scaled(28);
            deepFilterToggle.setBounds(rowX + pad, row0Top + scaled(76), scaled(112), scaled(22));
            deepFilterStatus.setBounds(rowX + pad + scaled(116), row0Top + scaled(78), scaled(260), scaled(18));
        }

        // Footer
        const int footerY = getHeight() - scaled(kFooterH);
        resetButton.setBounds(scaled(24), footerY + scaled(11), scaled(120), scaled(28));

        // Makeup gain knob  -  right side of footer
        const int mkSize = scaled(46);
        const int mkX    = getWidth() - scaled(22) - mkSize;
        const int mkY    = footerY + scaled(8);
        makeupSlider.setBounds(mkX, mkY, mkSize, mkSize + scaled(16));
        makeupLabel.setBounds(mkX - scaled(6), footerY + scaled(2), mkSize + scaled(12), scaled(14));
    }
}
