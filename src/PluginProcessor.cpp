// Reverso - VST3 wrapper (JUCE) around the DSP engine in dsp.h
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp.h"

namespace {
const char* const kIds[reverso::P_COUNT] = {"mix", "sync", "division", "time", "smooth", "feedback",
                                            "lowcut", "highcut", "diffuse", "width", "output"};
}

class ReversoProcessor final : public juce::AudioProcessor {
public:
    ReversoProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "STATE", createLayout()) {
        for (uint32_t i = 0; i < reverso::P_COUNT; ++i) raw[i] = apvts.getRawParameterValue(kIds[i]);
    }

    const juce::String getName() const override { return "Reverso"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
               l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay(double sampleRate, int) override { engine.prepare(sampleRate); }
    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if (buffer.getNumChannels() < 2) return;

        double bpm = 0.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) bpm = *b;

        using namespace reverso;
        Params p;
        p.mix = *raw[P_MIX] / 100.0;
        p.sync = *raw[P_SYNC];
        p.div = *raw[P_DIV];
        p.timeMs = *raw[P_TIME];
        p.smooth = *raw[P_SMOOTH] / 100.0;
        p.feedback = *raw[P_FEEDBACK] / 100.0;
        p.lowCut = *raw[P_LOWCUT];
        p.highCut = *raw[P_HIGHCUT];
        p.diffuse = *raw[P_DIFFUSE] / 100.0;
        p.width = *raw[P_WIDTH] / 100.0;
        p.outputDb = *raw[P_OUTPUT];

        float* l = buffer.getWritePointer(0);
        float* r = buffer.getWritePointer(1);
        engine.process(l, r, l, r, buffer.getNumSamples(), p, bpm);
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }

    void getStateInformation(juce::MemoryBlock& dest) override {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
    }
    void setStateInformation(const void* data, int size) override {
        if (auto xml = getXmlFromBinary(data, size))
            if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        using namespace juce;
        using namespace reverso;
        AudioProcessorValueTreeState::ParameterLayout l;
        auto id = [](int i) { return ParameterID{kIds[i], 1}; };
        auto flt = [&](int i, const char* name, NormalisableRange<float> r, float def, const char* unit) {
            l.add(std::make_unique<AudioParameterFloat>(id(i), name, r, def, AudioParameterFloatAttributes().withLabel(unit)));
        };
        flt(P_MIX, "Mix", {0.f, 100.f, 0.1f}, 50.f, "%");
        l.add(std::make_unique<AudioParameterBool>(id(P_SYNC), "Tempo Sync", true));
        StringArray divs;
        for (int i = 0; i < kNumDivisions; ++i) divs.add(kDivisionNames[i]);
        l.add(std::make_unique<AudioParameterChoice>(id(P_DIV), "Division", divs, 4));
        flt(P_TIME, "Time (free)", {10.f, 4000.f, 1.f, 0.4f}, 500.f, "ms");
        flt(P_SMOOTH, "Smooth", {0.f, 100.f, 0.1f}, 50.f, "%");
        flt(P_FEEDBACK, "Feedback", {0.f, 95.f, 0.1f}, 25.f, "%");
        flt(P_LOWCUT, "Low Cut", {20.f, 2000.f, 1.f, 0.4f}, 40.f, "Hz");
        flt(P_HIGHCUT, "High Cut", {1000.f, 20000.f, 1.f, 0.4f}, 12000.f, "Hz");
        flt(P_DIFFUSE, "Diffusion", {0.f, 100.f, 0.1f}, 20.f, "%");
        flt(P_WIDTH, "Width", {0.f, 200.f, 0.1f}, 100.f, "%");
        flt(P_OUTPUT, "Output", {-24.f, 12.f, 0.1f}, 0.f, "dB");
        return l;
    }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* raw[reverso::P_COUNT] = {};
    reverso::Engine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReversoProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ReversoProcessor(); }
