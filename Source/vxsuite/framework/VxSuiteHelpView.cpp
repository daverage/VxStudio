#include "VxSuiteHelpView.h"
#include "VxSuiteParameters.h"

#include <algorithm>
#include <memory>

namespace vxsuite {

namespace {

struct TextStyle {
    float height = 15.0f;
    bool bold = false;
    bool italic = false;
    bool code = false;
    juce::Colour colour = juce::Colours::white;
};

juce::Font makeFont(const TextStyle& style) {
    juce::FontOptions options;
    options = options.withHeight(style.height);
    if (style.code)
        options = options.withName("Menlo");
    if (style.bold)
        options = options.withStyle(style.italic ? "Bold Italic" : "Bold");
    else if (style.italic)
        options = options.withStyle("Italic");
    return juce::Font(options);
}

void appendText(juce::AttributedString& out, const juce::String& text, const TextStyle& style) {
    if (text.isEmpty())
        return;
    out.append(text, makeFont(style), style.colour);
}

void appendChildren(const juce::XmlElement& element,
                    juce::AttributedString& out,
                    const TextStyle& style);

void appendElement(const juce::XmlElement& element,
                   juce::AttributedString& out,
                   const TextStyle& parentStyle) {
    auto style = parentStyle;
    const auto tag = element.getTagName().toLowerCase();

    if (tag == "h1") {
        style.height = 24.0f;
        style.bold = true;
        appendChildren(element, out, style);
        appendText(out, "\n\n", parentStyle);
        return;
    }
    if (tag == "h2") {
        style.height = 19.0f;
        style.bold = true;
        appendChildren(element, out, style);
        appendText(out, "\n\n", parentStyle);
        return;
    }
    if (tag == "h3") {
        style.height = 16.0f;
        style.bold = true;
        appendChildren(element, out, style);
        appendText(out, "\n", parentStyle);
        return;
    }
    if (tag == "p") {
        appendChildren(element, out, style);
        appendText(out, "\n\n", parentStyle);
        return;
    }
    if (tag == "strong" || tag == "b") {
        style.bold = true;
        appendChildren(element, out, style);
        return;
    }
    if (tag == "em" || tag == "i") {
        style.italic = true;
        appendChildren(element, out, style);
        return;
    }
    if (tag == "code") {
        style.code = true;
        style.height = std::max(13.0f, style.height - 1.0f);
        style.colour = juce::Colour(0xff9ae6ff);
        appendChildren(element, out, style);
        return;
    }
    if (tag == "br") {
        appendText(out, "\n", parentStyle);
        return;
    }
    if (tag == "ul") {
        forEachXmlChildElement(element, child) {
            if (child->hasTagName("li")) {
                appendText(out, "• ", style);
                appendChildren(*child, out, style);
                appendText(out, "\n", parentStyle);
            }
        }
        appendText(out, "\n", parentStyle);
        return;
    }
    if (tag == "ol") {
        int index = 1;
        forEachXmlChildElement(element, child) {
            if (child->hasTagName("li")) {
                appendText(out, juce::String(index++) + ". ", style);
                appendChildren(*child, out, style);
                appendText(out, "\n", parentStyle);
            }
        }
        appendText(out, "\n", parentStyle);
        return;
    }
    if (tag == "li") {
        appendChildren(element, out, style);
        return;
    }

    appendChildren(element, out, style);
}

void appendChildren(const juce::XmlElement& element,
                    juce::AttributedString& out,
                    const TextStyle& style) {
    for (auto* node = element.getFirstChildElement(); node != nullptr; node = node->getNextElement())
        appendElement(*node, out, style);

    if (element.getNumChildElements() == 0 && element.getAllSubText().isNotEmpty())
        appendText(out, element.getAllSubText(), style);
    else if (element.getNumChildElements() > 0) {
        juce::String text;
        for (const auto& sub : juce::StringArray::fromLines(element.getAllSubText())) {
            if (!sub.trim().isEmpty())
                text += sub;
        }
        juce::ignoreUnused(text);
    }
}

juce::AttributedString makeAttributedHtml(const juce::String& html) {
    juce::AttributedString out;
    out.setJustification(juce::Justification::topLeft);
    out.setWordWrap(juce::AttributedString::WordWrap::byWord);

    const juce::String wrapped = "<document>" + html + "</document>";
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(wrapped));
    TextStyle baseStyle;
    baseStyle.height = 14.0f;
    baseStyle.colour = juce::Colour(0xffebf4ff);

    if (xml == nullptr) {
        appendText(out, html, baseStyle);
        return out;
    }

    forEachXmlChildElement(*xml, child)
        appendElement(*child, out, baseStyle);

    return out;
}

class HtmlDocumentComponent final : public juce::Component {
public:
    explicit HtmlDocumentComponent(juce::String htmlToRender)
        : html(std::move(htmlToRender)) {}

    void updateLayout(const int width) {
        const int safeWidth = std::max(180, width);
        juce::AttributedString attributed = makeAttributedHtml(html);
        layout = std::make_unique<juce::TextLayout>();
        layout->createLayout(attributed, static_cast<float>(safeWidth));
        setSize(safeWidth, std::max(120, juce::roundToInt(layout->getHeight()) + 12));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff111827));
        if (layout != nullptr)
            layout->draw(g, juce::Rectangle<float>(6.0f, 4.0f, static_cast<float>(getWidth() - 12), static_cast<float>(getHeight() - 8)));
    }

private:
    juce::String html;
    std::unique_ptr<juce::TextLayout> layout;
};

class HelpPopupComponent final : public juce::Component {
public:
    explicit HelpPopupComponent(const ProductIdentity& identity)
        : titleLabel({}, identity.helpTitle.empty() ? toJuceString(identity.productName) + " Help"
                                                    : toJuceString(identity.helpTitle)),
          versionLabel({}, "DSP v" + juce::String(identity.dspVersion.data()) +
                              "   Framework v" + juce::String(vxsuite::versions::framework.data())),
          reminderLabel({}, "Documentation contract: keep this help popup and the README in sync."),
          document(std::make_unique<HtmlDocumentComponent>(toJuceString(identity.helpHtml))),
          documentViewport("HelpViewport") {
        addAndMakeVisible(titleLabel);
        addAndMakeVisible(versionLabel);
        addAndMakeVisible(reminderLabel);
        documentViewport.setViewedComponent(document.get(), false);
        documentViewport.setScrollBarsShown(true, false);
        addAndMakeVisible(documentViewport);

        titleLabel.setJustificationType(juce::Justification::centredLeft);
        titleLabel.setFont(juce::FontOptions().withHeight(24.0f).withStyle("Bold"));
        versionLabel.setJustificationType(juce::Justification::centredLeft);
        versionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.68f));
        versionLabel.setFont(juce::FontOptions().withHeight(13.0f));
        reminderLabel.setJustificationType(juce::Justification::centredLeft);
        reminderLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.68f));
        reminderLabel.setFont(juce::FontOptions().withHeight(12.0f));
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff10151f));
        g.setColour(juce::Colour(0xff243042));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 14.0f, 1.0f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16);
        titleLabel.setBounds(area.removeFromTop(30));
        versionLabel.setBounds(area.removeFromTop(18));
        area.removeFromTop(8);
        reminderLabel.setBounds(area.removeFromBottom(18));
        area.removeFromBottom(8);
        documentViewport.setBounds(area);
        document->updateLayout(std::max(180, documentViewport.getMaximumVisibleWidth() - 12));
    }

private:
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label reminderLabel;
    std::unique_ptr<HtmlDocumentComponent> document;
    juce::Viewport documentViewport;
};

} // namespace

HelpButton::HelpButton() : juce::TextButton("Help") {
    setTooltip("Open in-plugin help");
}

void showHelpDialog(juce::Component& parent, const ProductIdentity& identity) {
    if (!identity.hasHelpContent())
        return;

    const auto parentBounds = parent.getScreenBounds();
    const int initialWidth = juce::jlimit(640, 980, parentBounds.getWidth() > 0 ? parentBounds.getWidth() * 3 / 4 : 760);
    const int initialHeight = juce::jlimit(460, 820, parentBounds.getHeight() > 0 ? parentBounds.getHeight() * 4 / 5 : 620);

    auto* popup = new HelpPopupComponent(identity);
    popup->setSize(initialWidth, initialHeight);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(popup);
    options.dialogTitle = identity.helpTitle.empty()
        ? toJuceString(identity.productName) + " Help"
        : toJuceString(identity.helpTitle);
    options.dialogBackgroundColour = juce::Colour(0xff10151f);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    options.useBottomRightCornerResizer = false;
    options.componentToCentreAround = &parent;

    if (auto* dialog = options.launchAsync())
        dialog->setResizeLimits(640, 460, 1200, 900);
}

} // namespace vxsuite
