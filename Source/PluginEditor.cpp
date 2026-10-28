#include "PluginEditor.h"

#include <BinaryData.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    // Flat controls with no borders: pale discs are interactive, white and pale type is selected,
    // and the quieter grey labels identify controls.
    namespace Theme
    {
        const juce::Colour background { 0xff131210 }, panel { 0xff22201b }, text { 0xffffffff },
                           label { 0xffada79a }, accent { 0xffe0a83f }, pale { 0xfff2ead8 },
                           clipping { 0xffff5a5a }, field { 0xff3e3a32 };

        // The meter face: the panel brown, a shade lighter; its hood is the panel colour.
        const juce::Colour face { 0xff2a2620 }, hood = panel;

        juce::Font make (const juce::Typeface::Ptr& typeface, float height, float tracking)
        {
            return juce::Font (juce::FontOptions (typeface).withHeight (height)).withExtraKerningFactor (tracking);
        }

        /** Labels are upper-case and tracked out; pass upper-case text. */
        juce::Font labelFont (float height = 11.0f)  { return make (juce::SharedResourcePointer<EditorFonts>()->inter, height, 0.09f); }
        juce::Font titleFont (float height)          { return make (juce::SharedResourcePointer<EditorFonts>()->inter, height, 0.18f); }
        juce::Font bodyFont (float height = 12.0f)   { return make (juce::SharedResourcePointer<EditorFonts>()->inter, height, 0.01f); }
        juce::Font valueFont (float height = 12.0f)  { return make (juce::SharedResourcePointer<EditorFonts>()->mono, height, 0.0f); }

        int widthOf (const juce::Font& font, const juce::String& s)
        {
            return juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (font, s)));
        }
    }

    // Spacing grammar of an option row: sibling options closest, the label further from its
    // first option.
    constexpr int kOptionGap = 10, kLabelGap = 18, kRowHeight = 18;

    const juce::Identifier helpId ("help"), helpTitleId ("helpTitle"), editorScaleId ("editorScale");

    constexpr auto kDefaultHelpTitle = "CLASSIC BUS COMPRESSOR";
    constexpr auto kDefaultHelp =
        "A stereo VCA bus compressor whose detector, timing and VCA follow the schematics of the SSL 4000 G "
        "(82E26 and 82E27). Hover over a control to see what it does; double-click a knob for its default, "
        "or its value to type one.";

    void setHelp (juce::Component& c, const juce::String& title, const juce::String& help)
    {
        c.getProperties().set (helpTitleId, title.toUpperCase());
        c.getProperties().set (helpId, help);
    }

    juce::String minusSign() { return juce::String::fromUTF8 ("\xE2\x88\x92"); }

    /** A dB figure with a true minus sign. */
    juce::String dbText (float db, int decimals = 1)
    {
        return (db < 0.0f && juce::String (db, decimals).startsWithChar ('-') ? minusSign() : juce::String()) + juce::String (std::abs (db), decimals);
    }

    // The gain-reduction scale: 20 dB at the left end, 0 at the right. The law is compressed
    // towards the small readings so the last few dB are easy to read.
    constexpr float kHalfSweep = 0.87f;    // radians either side of 12 o'clock
    constexpr float kArcTop = 25.0f;       // from the top of the face to the top of the arc
    constexpr float kHoodHeight = 19.0f, kCorner = 4.0f;
    constexpr float kScaleDb = 20.0f, kLaw = 0.64f;
    constexpr std::array<float, 10> kMarks { 20.0f, 15.0f, 10.0f, 7.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 0.0f };

    constexpr float kBarFloorDb = -60.0f;
}

//==============================================================================
EditorFonts::EditorFonts()
    : inter (juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize)),
      mono (juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize))
{
}

//==============================================================================
void FlatLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
    const float angle = startAngle + position * (endAngle - startAngle);
    const float dot = juce::jmax (2.5f, radius * 0.11f);

    g.setColour (slider.isMouseOverOrDragging() ? Theme::pale.brighter (0.1f) : Theme::pale);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

    g.setColour (Theme::background);
    g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (centre.getPointOnCircumference (radius * 0.72f, angle)));
}

void FlatLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // Three short diagonals in the label grey, quiet until touched.
    g.setColour (Theme::label.withAlpha (isMouseOver || isMouseDragging ? 0.8f : 0.3f));
    const float size = (float) juce::jmin (w, h);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w - size * i, (float) h - 2.0f, (float) w - 2.0f, (float) h - size * i, 1.0f);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& nameToUse, const juce::String& help,
            int diameterToUse, KnobFormat formatToUse)
    : name (nameToUse.toUpperCase()), diameter (diameterToUse), attachment (*state.getParameter (paramId), slider),
      format (std::move (formatToUse))
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        choices = choice->choices;
    else
        slider.textFromValueFunction = [f = format] (double v) { return juce::String (v, f.decimals) + f.suffix; };

    const float halfSweep = juce::degreesToRadians (135.0f), twoPi = juce::MathConstants<float>::twoPi;
    slider.setRotaryParameters (twoPi - halfSweep, twoPi + halfSweep, true);
    slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift: fine
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);

    editor.setJustification (juce::Justification::centred);
    editor.setFont (Theme::valueFont (12.0f));
    editor.setIndents (0, 0);
    editor.setColour (juce::TextEditor::backgroundColourId, Theme::field);
    editor.setColour (juce::TextEditor::textColourId, Theme::pale);
    editor.setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.5f));
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor.onReturnKey = [this] { finishEditing (true); };
    editor.onEscapeKey = [this] { finishEditing (false); };
    editor.onFocusLost = [this] { finishEditing (true); };
    addChildComponent (editor);

    setHelp (*this, name, help);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (kLabelHeight + kGap - 1);
    slider.setBounds (area.removeFromTop (diameter + 2).withSizeKeepingCentre (diameter + 2, diameter + 2));
    editor.setBounds (getLocalBounds().removeFromBottom (kLabelHeight).withSizeKeepingCentre (64, kLabelHeight));
}

void Knob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (Theme::label);
    g.setFont (Theme::labelFont());
    g.drawText (name, area.removeFromTop (kLabelHeight), juce::Justification::centred, false);

    g.setFont (Theme::valueFont (12.0f));
    g.drawText (slider.getTextFromValue (slider.getValue()), area.removeFromBottom (kLabelHeight),
                juce::Justification::centred, false);
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < getHeight() - kLabelHeight - 2)
        return;

    editor.setText (slider.getTextFromValue (slider.getValue()), false);
    editor.setVisible (true);
    editor.selectAll();
    editor.grabKeyboardFocus();
}

bool Knob::parseText (const juce::String& typed, double& value) const
{
    const auto text = typed.trim().toLowerCase();
    if (text.isEmpty())
        return false;

    const bool hasDigit = text.containsAnyOf ("0123456789");

    if (choices.isEmpty())
    {
        if (! hasDigit)
            return false;
        value = text.getDoubleValue();
        return true;
    }

    // A choice: its own name, a number that names one ("0.3" for "0.3 ms"), or the start of a word.
    for (int i = 0; i < choices.size(); ++i)
    {
        const auto option = choices[i].toLowerCase();
        const bool named = option == text || (! hasDigit && option.startsWith (text))
                           || (hasDigit && juce::CharacterFunctions::isDigit (option[0]) && std::abs (option.getDoubleValue() - text.getDoubleValue()) < 1.0e-9);
        if (named)
        {
            value = i;
            return true;
        }
    }
    return false;
}

void Knob::finishEditing (bool apply)
{
    if (! editor.isVisible())
        return;

    editor.setVisible (false);

    // Only text that names a value; anything else leaves the control as it was.
    double value = 0.0;
    if (apply && parseText (editor.getText(), value))
        slider.setValue (value, juce::sendNotificationSync);
    repaint();
}

//==============================================================================
OptionRow::OptionRow (juce::RangedAudioParameter& param, const juce::String& labelToUse, juce::StringArray optionsToUse,
                      const juce::String& help)
    : label (labelToUse.toUpperCase()), options (std::move (optionsToUse)),
      attachment (param, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    labelColumn = labelTextWidth() + kLabelGap;
    attachment.sendInitialUpdate();
    setHelp (*this, label, help);
}

int OptionRow::labelTextWidth() const
{
    return Theme::widthOf (Theme::labelFont(), label);
}

std::vector<juce::Range<int>> OptionRow::optionRanges() const
{
    std::vector<juce::Range<int>> ranges;
    const auto font = Theme::labelFont();
    int x = labelColumn;

    for (const auto& option : options)
    {
        const int width = Theme::widthOf (font, option.toUpperCase());
        ranges.push_back ({ x, x + width });
        x += width + kOptionGap;
    }
    return ranges;
}

int OptionRow::preferredWidth() const
{
    const auto ranges = optionRanges();
    return ranges.empty() ? labelColumn : ranges.back().getEnd();
}

int OptionRow::optionAt (int x) const
{
    const auto ranges = optionRanges();
    for (int i = 0; i < (int) ranges.size(); ++i)
        if (ranges[(size_t) i].expanded (kOptionGap / 2).contains (x))
            return i;
    return -1;
}

void OptionRow::paint (juce::Graphics& g)
{
    g.setFont (Theme::labelFont());
    g.setColour (Theme::label);
    g.drawText (label, 0, 0, labelColumn, getHeight(), juce::Justification::centredLeft, false);

    const auto ranges = optionRanges();
    for (int i = 0; i < options.size(); ++i)
    {
        const auto range = ranges[(size_t) i];
        g.setColour (i == selected ? Theme::pale : (i == hovered ? Theme::text : Theme::label.withAlpha (0.7f)));
        g.drawText (options[i].toUpperCase(), range.getStart(), 0, range.getLength() + 4, getHeight(),
                    juce::Justification::centredLeft, false);
    }
}

void OptionRow::mouseDown (const juce::MouseEvent& e)
{
    const int option = optionAt (e.x);
    if (option >= 0)
        attachment.setValueAsCompleteGesture ((float) option);
}

void OptionRow::mouseMove (const juce::MouseEvent& e)   { hovered = optionAt (e.x); repaint(); }
void OptionRow::mouseExit (const juce::MouseEvent&)     { hovered = -1; repaint(); }

//==============================================================================
GrMeter::GrMeter()
{
    setHelp (*this, "Gain reduction",
             "How far the compressor is turning the signal down, in dB: it rests at 0 on the right and swings left as it "
             "works, to 20 dB. Both channels share one reduction. The needle is smoothed.");
}

float GrMeter::angleFor (float reductionDb) noexcept
{
    const float fraction = std::pow (juce::jlimit (0.0f, 1.0f, reductionDb / kScaleDb), kLaw);
    return kHalfSweep * (1.0f - 2.0f * fraction);
}

juce::Point<float> GrMeter::pivot() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getCentreX(), r.getY() + kArcTop + radius() };
}

float GrMeter::radius() const
{
    return (float) getWidth() * 0.42f / std::sin (kHalfSweep);
}

void GrMeter::setReading (float newNeedleDb)
{
    const bool needleMoved = std::abs (angleFor (newNeedleDb) - angleFor (needleDb)) > 0.0005f;

    if (needleMoved)
    {
        needleDb = newNeedleDb;
        repaint();
    }
}

void GrMeter::paint (juce::Graphics& g)
{
    // The face, and at its foot the hood over the needle's pivot.
    const auto faceArea = getLocalBounds().toFloat();
    const auto hood = faceArea.withTop (faceArea.getBottom() - kHoodHeight);
    g.setColour (Theme::face);
    g.fillRoundedRectangle (faceArea, kCorner);
    juce::Path hoodShape;
    hoodShape.addRoundedRectangle (hood.getX(), hood.getY(), hood.getWidth(), hood.getHeight(), kCorner, kCorner, false, false, true, true);
    g.setColour (Theme::hood);
    g.fillPath (hoodShape);

    const auto centre = pivot();
    const float r = radius();

    juce::Path line;
    line.addCentredArc (centre.x, centre.y, r, r, 0.0f, angleFor (kScaleDb), angleFor (0.0f), true);
    g.setColour (Theme::label.withAlpha (0.7f));
    g.strokePath (line, juce::PathStrokeType (1.0f));

    // Marks and figures.
    g.setFont (Theme::valueFont (8.0f));
    for (const float mark : kMarks)
    {
        const float a = angleFor (mark);
        g.setColour (Theme::label);
        g.drawLine ({ centre.getPointOnCircumference (r, a), centre.getPointOnCircumference (r + 5.0f, a) }, mark == 0.0f ? 1.5f : 1.0f);

        const auto at = centre.getPointOnCircumference (r + 11.0f, a);
        g.drawText (juce::String ((int) mark), juce::Rectangle<float> (22.0f, 10.0f).withCentre (at), juce::Justification::centred, false);
    }

    g.setColour (Theme::label.withAlpha (0.5f));
    g.setFont (Theme::titleFont (10.0f));
    g.drawText ("dB", juce::Rectangle<float> (faceArea.getX(), centre.y - r + 35.0f, faceArea.getWidth(), 12.0f),
                juce::Justification::centred, false);

    // The needle, from its pivot under the hood.
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (faceArea.withBottom (hood.getY()).toNearestInt());
    g.setColour (Theme::pale);
    g.drawLine ({ centre, centre.getPointOnCircumference (r + 2.0f, angleFor (needleDb)) }, 1.25f);
}

//==============================================================================
LevelBar::LevelBar (const juce::String& labelToUse, const juce::String& help)
    : label (labelToUse.toUpperCase())
{
    setHelp (*this, label, help);
}

void LevelBar::setPeak (float peak)
{
    const float db = peak < 1.0e-5f ? -100.0f : 20.0f * std::log10 (peak);
    const float next = juce::jmax (db, shownDb - 0.7f);

    if (juce::String (next, 1) != juce::String (shownDb, 1))
    {
        shownDb = next;
        repaint();
    }
    else
        shownDb = next;
}

void LevelBar::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    constexpr int labelWidth = 28, valueWidth = 72;

    g.setColour (Theme::label);
    g.setFont (Theme::labelFont());
    g.drawText (label, area.removeFromLeft (labelWidth), juce::Justification::centredLeft, false);

    const bool silent = shownDb <= -90.0f;
    g.setColour (Theme::pale);
    g.setFont (Theme::valueFont (11.0f));
    g.drawText ((silent ? minusSign() + "inf" : dbText (shownDb)) + " dBFS", area.removeFromRight (valueWidth),
                juce::Justification::centredRight, false);

    // A thin track in the label grey, and the level over it.
    const auto track = area.withSizeKeepingCentre (area.getWidth() - 8, 3).toFloat();
    g.setColour (Theme::label.withAlpha (0.25f));
    g.fillRect (track);
    const float fraction = silent ? 0.0f : juce::jlimit (0.0f, 1.0f, (shownDb - kBarFloorDb) / -kBarFloorDb);
    g.setColour (shownDb >= 0.0f ? Theme::clipping : Theme::accent);
    g.fillRect (track.withWidth (track.getWidth() * fraction));
}

//==============================================================================
EditorView::EditorView (SslBusProcessor& p)
    : processor (p),
      threshold (p.parameters, ParamID::threshold, "Threshold",
                 "The position of the original 50k linear threshold pot, from -100 to +100 %. Lower settings compress more. "
                 "It is the wiper position, not a level in dBu.",
                 64, { 1, "%" }),
      ratio (p.parameters, ParamID::ratio, "Ratio",
             "Switches the original resistor and diode ratio network between 2:1, 4:1 and 10:1. A higher ratio turns "
             "the signal down harder once it is over the threshold.",
             64),
      attack (p.parameters, ParamID::attack, "Attack",
              "Selects one of the original R1 to R6 timing resistors: 0.1, 0.3, 1, 3, 10 or 30 ms. Short times clamp "
              "down quickly; long ones let more of a transient through before the gain falls.",
              64),
      release (p.parameters, ParamID::release, "Release",
               "Selects one of the original RC branches: 0.1, 0.3, 0.6 or 1.2 s. AUTO solves the original's two coupled "
               "capacitor nodes, so how fast the gain recovers depends on the material.",
               64),
      makeup (p.parameters, ParamID::makeup, "Make up",
              "The position of the original 25k linear make-up pot, 0 to 100 %, which is roughly 0 to 14.7 dB of gain "
              "after the compressor.",
              64, { 1, "%" }),
      filter (p.parameters, ParamID::sidechainFilter, "S/C HPF",
              "A high-pass filter on the detector only; the audio you hear is not filtered. OFF, or 30 to 185 Hz: "
              "the higher it is, the less low frequencies push the compressor. A modern extension, not in the original.",
              48),
      mix (p.parameters, ParamID::mix, "Mix",
           "Dry/wet parallel compression. 100 % is the classic, fully compressed sound; lower settings blend the "
           "uncompressed signal back in.",
           48, { 0, "%" }),
      alignment (p.parameters, ParamID::alignment, "0 dBFS align",
                 "The analogue peak voltage that 0 dBFS stands for, +12 to +30 dBu (default +22). The circuit's "
                 "voltages are fixed, so this sets where a given track level meets the threshold.",
                 36, { 1, " dBu" }),
      compressorRow (*p.parameters.getParameter (ParamID::in), "Compressor", { "Out", "In" },
                     "OUT is a bit-exact dry path: the signal is not touched. IN engages the compressor."),
      sidechainRow (*p.parameters.getParameter (ParamID::externalSidechain), "Sidechain", { "Int", "Ext" },
                    "What the detector listens to. INT: the signal being compressed. EXT: the host's sidechain input, when "
                    "one is connected; without it the compressor listens to its own input."),
      inBar ("In", "The peak level of the louder channel going into the compressor, in dBFS. It falls back slowly "
                   "so it can be read; red at 0 dBFS."),
      outBar ("Out", "The peak level of the louder channel leaving the plug-in, in dBFS, after make-up gain and "
                     "mix. It falls back slowly so it can be read; red at 0 dBFS.")
{
    const std::pair<juce::Component*, const char*> controls[] {
        { &threshold, ParamID::threshold }, { &ratio, ParamID::ratio }, { &attack, ParamID::attack },
        { &release, ParamID::release }, { &makeup, ParamID::makeup }, { &filter, ParamID::sidechainFilter },
        { &mix, ParamID::mix }, { &alignment, ParamID::alignment }, { &compressorRow, ParamID::in },
        { &sidechainRow, ParamID::externalSidechain }
    };
    for (const auto& [component, id] : controls)
    {
        component->setComponentID (id);
        addAndMakeVisible (component);
    }
    addAndMakeVisible (grMeter);
    addAndMakeVisible (inBar);
    addAndMakeVisible (outBar);

    showHelpFor (nullptr);
    updateMeters();
    setSize (kWidth, kHeight);
}

void EditorView::updateMeters()
{
    const float gr = juce::jlimit (0.0f, 30.0f, processor.getGainReductionDb());

    // The needle has the weight of a meter movement: quick to fall in, slower to come back.
    needleDb += (gr - needleDb) * (gr > needleDb ? 0.36f : 0.14f);
    grMeter.setReading (needleDb);
    inBar.setPeak (processor.getInputPeak());
    outBar.setPeak (processor.getOutputPeak());
}

void EditorView::showHelpFor (juce::Component* component)
{
    juce::String title = kDefaultHelpTitle, text = kDefaultHelp;

    // The help belongs to the nearest ancestor that has any (a knob's slider has none itself).
    for (auto* c = component; c != nullptr && c != this; c = c->getParentComponent())
    {
        if (c->getProperties().contains (helpId))
        {
            title = c->getProperties()[helpTitleId].toString();
            text = c->getProperties()[helpId].toString();
            break;
        }
    }

    if (title != helpTitle || text != helpText)
    {
        helpTitle = title;
        helpText = text;
        repaint (helpArea);
    }
}

void EditorView::showHelpForParameter (const juce::String& paramId)
{
    for (auto* child : getChildren())
        if (child->getComponentID() == paramId)
            showHelpFor (child);
}

void EditorView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Title: the unit, and quieter, which one.
    const auto title = Theme::titleFont (15.0f), subtitle = Theme::titleFont (11.0f);
    const float baseline = 33.0f;
    g.setColour (Theme::pale);
    g.setFont (title);
    g.drawSingleLineText ("CLASSIC BUS COMPRESSOR", 24, (int) baseline);
    g.setColour (Theme::label);
    g.setFont (subtitle);
    g.drawSingleLineText (juce::String::fromUTF8 ("4000 G \xC2\xB7 STEREO VCA"), 24 + Theme::widthOf (title, "CLASSIC BUS COMPRESSOR") + 12, (int) baseline);

    // Help, bottom left: what the control under the mouse does.
    auto help = helpArea;
    g.setColour (Theme::pale);
    g.setFont (Theme::labelFont());
    g.drawText (helpTitle, help.removeFromTop (16), juce::Justification::topLeft, false);
    g.setColour (Theme::label);
    g.setFont (Theme::bodyFont());
    g.drawFittedText (helpText, help, juce::Justification::topLeft, 3, 1.0f);
}

void EditorView::resized()
{
    constexpr int margin = 24, header = 50, knobCell = 96, bankWidth = 5 * knobCell, columnGap = 24;
    constexpr int meterHeight = 104, laneGap = 24, laneToKnobs = 4, rowGap = 8, barGap = 12;

    auto area = getLocalBounds().reduced (margin, 0);
    area.removeFromTop (header);

    // Bottom: what the control under the mouse does.
    area.removeFromBottom (10);
    helpArea = area.removeFromBottom (54);
    area.removeFromBottom (16);

    auto bank = area.removeFromLeft (bankWidth);
    area.removeFromLeft (columnGap);
    auto column = area;

    // The two switches share one lane above the knob bank.
    const int laneWidth = compressorRow.preferredWidth() + laneGap + sidechainRow.preferredWidth();
    auto lane = juce::Rectangle<int> (bank.getX() + (bankWidth - laneWidth) / 2, bank.getY(), laneWidth, kRowHeight);
    compressorRow.setBounds (lane.removeFromLeft (compressorRow.preferredWidth()));
    lane.removeFromLeft (laneGap);
    sidechainRow.setBounds (lane.removeFromLeft (sidechainRow.preferredWidth()));
    bank.removeFromTop (kRowHeight + laneToKnobs);

    // Detector and timing, then make-up.
    auto primary = bank.removeFromTop (threshold.preferredHeight());
    for (auto* knob : { &threshold, &ratio, &attack, &release, &makeup })
        knob->setBounds (primary.removeFromLeft (knobCell));

    // The extras: three cells centred under the bank, their discs on one line.
    bank.removeFromTop (rowGap);
    auto secondary = bank.removeFromTop (filter.preferredHeight());
    auto cells = secondary.withSizeKeepingCentre (3 * knobCell, secondary.getHeight());
    const int discCentre = secondary.getY() + Knob::kLabelHeight + Knob::kGap + 24;
    for (auto* knob : { &filter, &mix, &alignment })
    {
        auto cell = cells.removeFromLeft (knobCell);
        knob->setBounds (cell.getX(), discCentre - (Knob::kLabelHeight + Knob::kGap) - (knob->preferredHeight() - 2 * (Knob::kLabelHeight + Knob::kGap)) / 2,
                         knobCell, knob->preferredHeight());
    }

    // The meter and its bars, one block centred on the two knob rows.
    const int blockHeight = meterHeight + barGap + 2 * kRowHeight;
    const int rowsTop = primary.getY(), rowsHeight = secondary.getBottom() - rowsTop;
    auto block = column.withY (rowsTop + (rowsHeight - blockHeight) / 2).withHeight (blockHeight);
    grMeter.setBounds (block.removeFromTop (meterHeight));
    block.removeFromTop (barGap);
    inBar.setBounds (block.removeFromTop (kRowHeight));
    outBar.setBounds (block.removeFromTop (kRowHeight));
}

//==============================================================================
SslBusEditor::SslBusEditor (SslBusProcessor& p)
    : AudioProcessorEditor (&p), owner (p), view (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    // Scales as a whole, 0.75x to 2x, keeping the design's aspect ratio.
    constexpr int w = EditorView::kWidth, h = EditorView::kHeight;
    setResizable (true, true);
    setResizeLimits (w * 3 / 4, h * 3 / 4, w * 2, h * 2);
    getConstrainer()->setFixedAspectRatio ((double) w / (double) h);

    const double scale = juce::jlimit (0.75, 2.0, (double) p.parameters.state.getProperty (editorScaleId, 1.0));
    setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
    startTimerHz (30);
}

SslBusEditor::~SslBusEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SslBusEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void SslBusEditor::resized()
{
    const float scale = (float) getWidth() / (float) EditorView::kWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, EditorView::kWidth, EditorView::kHeight);

    // Remember a size the user chose (a window on screen), not the ones a host passes through
    // while opening or closing it.
    if (isShowing() && getPeer() != nullptr)
        owner.parameters.state.setProperty (editorScaleId, (double) scale, nullptr);
}

void SslBusEditor::timerCallback()
{
    view.updateMeters();

    auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    view.showHelpFor (under != nullptr && view.isParentOf (under) ? under : nullptr);
}
