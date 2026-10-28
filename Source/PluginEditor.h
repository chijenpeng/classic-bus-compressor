#pragma once

#include "PluginProcessor.h"

#include <vector>

/** The embedded typefaces. Shared by all open editors and released with the last one, so
    nothing outlives the host's JUCE shutdown. */
struct EditorFonts
{
    EditorFonts();
    juce::Typeface::Ptr inter, mono;
};

/** Flat drawing for the knobs and the resize corner; everything else paints itself. */
class FlatLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};

/** A continuous parameter's value text: `decimals` places and a unit suffix. Choice
    parameters show (and accept) their own option names. */
struct KnobFormat { int decimals = 0; juce::String suffix; };

/** A flat disc with a dot, its name above and its value below. Double-click the disc for the
    default; double-click the value to type one. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& name, const juce::String& help,
          int diameter, KnobFormat format = {});

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int kLabelHeight = 16, kGap = 8;
    /** Label, disc and value. */
    int preferredHeight() const noexcept { return 2 * (kLabelHeight + kGap) + diameter; }

private:
    void finishEditing (bool apply);
    /** The slider value a typed text names, if it names one. */
    bool parseText (const juce::String&, double& value) const;

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String name;
    int diameter;
    juce::TextEditor editor;
    juce::SliderParameterAttachment attachment;
    juce::StringArray choices;
    KnobFormat format;
};

/** "LABEL  OPTION  OPTION ..." as plain text; the selected option is lit. */
class OptionRow final : public juce::Component
{
public:
    OptionRow (juce::RangedAudioParameter&, const juce::String& label, juce::StringArray options,
               const juce::String& help);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** The label's own width; rows stacked together share the widest plus the label gap. */
    int labelTextWidth() const;
    void setLabelColumn (int width) { labelColumn = width; repaint(); }
    int preferredWidth() const;

private:
    std::vector<juce::Range<int>> optionRanges() const;
    int optionAt (int x) const;

    juce::String label;
    juce::StringArray options;
    int labelColumn = 0, selected = 0, hovered = -1;
    juce::ParameterAttachment attachment;
};

/** The gain-reduction movement: the VU face, 20 dB at the left end and 0 at the right, so the
    needle swings left as the compressor works. */
class GrMeter final : public juce::Component
{
public:
    GrMeter();

    /** The needle (dB of reduction, >= 0). Repaints only when the needle visibly moved. */
    void setReading (float needleDb);
    void paint (juce::Graphics&) override;

private:
    juce::Point<float> pivot() const;
    float radius() const;
    /** Angle (radians, clockwise from 12 o'clock) of a reduction in dB. */
    static float angleFor (float reductionDb) noexcept;

    float needleDb = 0.0f;
};

/** A thin flat level bar: IN or OUT, a peak in dBFS. */
class LevelBar final : public juce::Component
{
public:
    LevelBar (const juce::String& label, const juce::String& help);

    /** `peak`: linear, 1 is 0 dBFS. Falls back at about 20 dB/s. */
    void setPeak (float peak);
    void paint (juce::Graphics&) override;

private:
    juce::String label;
    float shownDb = -100.0f;
};

/** Everything inside the window, laid out at the design size; the editor scales it. */
class EditorView final : public juce::Component
{
public:
    static constexpr int kWidth = 752, kHeight = 368;

    explicit EditorView (SslBusProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Reads the processor's meters. */
    void updateMeters();
    /** Shows the help of the control under the mouse (nullptr: the default text). */
    void showHelpFor (juce::Component*);
    /** Shows the help of the control for this parameter ID. */
    void showHelpForParameter (const juce::String& paramId);

private:
    SslBusProcessor& processor;
    Knob threshold, ratio, attack, release, makeup, filter, mix, alignment;
    OptionRow compressorRow, sidechainRow;
    GrMeter grMeter;
    LevelBar inBar, outBar;

    float needleDb = 0.0f;
    juce::String helpTitle, helpText;
    juce::Rectangle<int> helpArea;
};

class SslBusEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit SslBusEditor (SslBusProcessor&);
    ~SslBusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** For the offline snapshot tool, which has no host message loop or mouse. */
    void updateMeters() { view.updateMeters(); }
    void showHelpForParameter (const juce::String& paramId) { view.showHelpForParameter (paramId); }

private:
    void timerCallback() override;

    SslBusProcessor& owner;
    juce::SharedResourcePointer<EditorFonts> fonts;
    FlatLookAndFeel lookAndFeel;
    EditorView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SslBusEditor)
};
