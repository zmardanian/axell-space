#pragma once
#include <axell/gui/LookAndFeel.h>
#include "PluginProcessor.h"

namespace gui
{
constexpr int kWidth = 1080;
constexpr int kHeight = 660;
}

// the space at a glance: the reverb's decay (its level over time from the pre-delay to -60 dB) and the delay's
// repeats, on one time axis; ESS DUCK and VOICE DUCK activity at the right
class SpaceView : public juce::Component, public juce::SettableTooltipClient
{
public:
    void set (const axs::Params& p, int lookSamples, double fs, float essP, float duckDb);
    void paint (juce::Graphics&) override;

private:
    axs::Params params;
    int look = 384;
    double rate = 48000.0;
    float ess = 0.0f, duck = 0.0f;
};

class SpaceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpaceEditor (SpaceProcessor&);
    ~SpaceEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (SpaceEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        SpaceEditor& owner;
    };
    void timerCallback() override;
    void layout();
    void showPresetMenu();
    void updateVisibility();

    SpaceProcessor& proc;
    ui::LookAndFeel lnf;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 700 };
    juce::TextButton presetPrev { "<" }, presetNext { ">" };
    ui::MenuButton presetName;
    juce::TextButton slotA { "A" }, slotB { "B" }, slotCopy { "COPY" };
    SpaceView view;
    // REVERB
    ui::Toggle reverbOn { "REVERB", ui::col::teal, "power" };
    ui::Segmented type { { "BOOTH", "ROOM", "PLATE", "HALL" }, ui::col::teal };
    ui::Knob size { "SIZE", ui::col::teal }, decay { "DECAY", ui::col::teal }, preDelay { "PRE-DELAY", ui::col::teal },
             damping { "DAMPING", ui::col::teal }, early { "EARLY", ui::col::teal }, modulation { "MOD", ui::col::teal },
             revLow { "LOW CUT", ui::col::teal }, revHigh { "HIGH CUT", ui::col::teal }, width { "WIDTH", ui::col::teal },
             revLevel { "LEVEL", ui::col::teal };
    // DELAY
    ui::Toggle delayOn { "DELAY", ui::col::accent, "power" };
    ui::Toggle sync { "SYNC", ui::col::accent, "pill" }, pingPong { "PING-PONG", ui::col::accent, "pill" };
    ui::Choice note;
    ui::Knob time { "TIME", ui::col::accent }, feedback { "FEEDBACK", ui::col::accent }, dlyLow { "LOW CUT", ui::col::accent },
             dlyHigh { "HIGH CUT", ui::col::accent }, drive { "DRIVE", ui::col::accent }, dlyLevel { "LEVEL", ui::col::accent },
             toReverb { "INTO REVERB", ui::col::accent };
    // DUCKING + OUTPUT
    ui::Toggle essDuck { "ESS DUCK", ui::col::warn, "pill" };
    ui::Knob essDb { "ESS AMOUNT", ui::col::warn }, voiceDuck { "VOICE DUCK", ui::col::global }, release { "RELEASE", ui::col::global },
             mix { "MIX", ui::col::global, 64 }, output { "OUTPUT", ui::col::global };
    ui::LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    juce::Rectangle<int> headerArea, viewArea, reverbArea, delayArea, bottomArea, noteArea;
    bool constructed = false;
    int lastOnState = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceEditor)
};
