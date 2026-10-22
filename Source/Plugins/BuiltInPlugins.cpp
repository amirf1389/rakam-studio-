#include "Plugins/BuiltInPlugins.h"
#include "Plugins/PluginEditor.h"

namespace
{
    using APF = juce::AudioParameterFloat;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    juce::String hzText (float v, int)  { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                                              : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String dbText (float v, int)  { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String msText (float v, int)  { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
    juce::String ratioText (float v, int) { return v >= 19.95f ? juce::String ("inf:1") : juce::String (v, 1) + ":1"; }
    juce::String plainText (float v, int) { return juce::String (v, 2); }

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }

    juce::NormalisableRange<float> linRange (float lo, float hi, float step = 0.0f) { return { lo, hi, step }; }

    std::unique_ptr<APF> makeParam (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range,
                                    float def, std::function<juce::String (float, int)> text)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text)));
    }

    float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -100.0f); }

    // Shared drawing for gain-reduction style meters
    void drawMeterBar (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, float valueDb, float rangeDb)
    {
        g.setColour (Rakam::Colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label, r.removeFromTop (18.0f), juce::Justification::centredLeft);

        auto bar = r.removeFromTop (14.0f);
        g.setColour (Rakam::Colours::background);
        g.fillRoundedRectangle (bar, 3.0f);

        const float amount = juce::jlimit (0.0f, 1.0f, -valueDb / rangeDb);
        g.setColour (Rakam::Colours::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * amount), 3.0f);

        g.setColour (Rakam::Colours::text);
        g.drawText (juce::String (valueDb, 1) + " dB", r.removeFromTop (18.0f), juce::Justification::centredRight);
    }

    //==============================================================================
    class EqPlugin : public BuiltInPlugin
    {
    public:
        EqPlugin() : BuiltInPlugin ("EQ", "EQ", layout())
        {
            presets = {
                { "Flat", {} },
                { "Vocal presence", { { "lowCut", 90.0f }, { "p1Freq", 300.0f }, { "p1Gain", -2.5f }, { "p1Q", 1.2f },
                                      { "p2Freq", 3500.0f }, { "p2Gain", 3.0f }, { "p2Q", 0.9f }, { "hsGain", 2.5f } } },
                { "Kick punch",     { { "lsFreq", 60.0f }, { "lsGain", 4.0f }, { "p1Freq", 350.0f }, { "p1Gain", -5.0f },
                                      { "p2Freq", 4000.0f }, { "p2Gain", 3.0f } } },
                { "Bass tighten",   { { "lowCut", 35.0f }, { "lsFreq", 80.0f }, { "lsGain", 2.0f }, { "p1Freq", 250.0f }, { "p1Gain", -3.0f } } },
                { "Telephone",      { { "lowCut", 400.0f }, { "highCut", 3200.0f }, { "p1Freq", 1500.0f }, { "p1Gain", 6.0f } } },
                { "Air & sparkle",  { { "hsFreq", 10000.0f }, { "hsGain", 4.0f } } },
                { "Mud cut",        { { "p1Freq", 300.0f }, { "p1Gain", -4.0f }, { "p1Q", 0.8f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("lowCut",  "Low Cut",     logRange (20.0f, 1000.0f), 20.0f, hzText));
            l.add (makeParam ("lsFreq",  "Low Freq",    logRange (20.0f, 1000.0f), 100.0f, hzText));
            l.add (makeParam ("lsGain",  "Low Gain",    linRange (-18.0f, 18.0f), 0.0f, dbText));
            l.add (makeParam ("p1Freq",  "Mid 1 Freq",  logRange (60.0f, 8000.0f), 500.0f, hzText));
            l.add (makeParam ("p1Gain",  "Mid 1 Gain",  linRange (-18.0f, 18.0f), 0.0f, dbText));
            l.add (makeParam ("p1Q",     "Mid 1 Q",     logRange (0.2f, 10.0f), 1.0f, plainText));
            l.add (makeParam ("p2Freq",  "Mid 2 Freq",  logRange (300.0f, 16000.0f), 3000.0f, hzText));
            l.add (makeParam ("p2Gain",  "Mid 2 Gain",  linRange (-18.0f, 18.0f), 0.0f, dbText));
            l.add (makeParam ("p2Q",     "Mid 2 Q",     logRange (0.2f, 10.0f), 1.0f, plainText));
            l.add (makeParam ("hsFreq",  "High Freq",   logRange (1000.0f, 20000.0f), 8000.0f, hzText));
            l.add (makeParam ("hsGain",  "High Gain",   linRange (-18.0f, 18.0f), 0.0f, dbText));
            l.add (makeParam ("highCut", "High Cut",    logRange (1000.0f, 20000.0f), 20000.0f, hzText));
            l.add (makeParam ("output",  "Output",      linRange (-18.0f, 18.0f), 0.0f, dbText));
            return l;
        }

        using Values = std::array<float, 12>;

        Values read() const
        {
            return { param ("lowCut"), param ("lsFreq"), param ("lsGain"), param ("p1Freq"), param ("p1Gain"), param ("p1Q"),
                     param ("p2Freq"), param ("p2Gain"), param ("p2Q"), param ("hsFreq"), param ("hsGain"), param ("highCut") };
        }

        static std::array<Coeffs::Ptr, 6> makeCoeffs (const Values& v, double sr)
        {
            const auto nyq = (float) sr * 0.48f;
            auto f = [nyq] (float x) { return juce::jmin (x, nyq); };
            std::array<Coeffs::Ptr, 6> c;

            if (v[0] > 20.5f)                c[0] = Coeffs::makeHighPass (sr, f (v[0]), 0.707f);
            if (std::abs (v[2]) > 0.05f)     c[1] = Coeffs::makeLowShelf (sr, f (v[1]), 0.707f, dbToGain (v[2]));
            if (std::abs (v[4]) > 0.05f)     c[2] = Coeffs::makePeakFilter (sr, f (v[3]), v[5], dbToGain (v[4]));
            if (std::abs (v[7]) > 0.05f)     c[3] = Coeffs::makePeakFilter (sr, f (v[6]), v[8], dbToGain (v[7]));
            if (std::abs (v[10]) > 0.05f)    c[4] = Coeffs::makeHighShelf (sr, f (v[9]), 0.707f, dbToGain (v[10]));
            if (v[11] < 19900.0f && v[11] < nyq) c[5] = Coeffs::makeLowPass (sr, f (v[11]), 0.707f);
            return c;
        }

        void prepareToPlay (double sr, int) override
        {
            sampleRate = sr;
            last.fill (-1.0e9f);
            for (auto& ch : filters)
                for (auto& f : ch)
                    f.reset();
        }

        void process (juce::AudioBuffer<float>& b) override
        {
            const auto v = read();
            if (v != last)
            {
                coeffs = makeCoeffs (v, sampleRate);
                for (auto& ch : filters)
                    for (size_t band = 0; band < 6; ++band)
                        if (coeffs[band] != nullptr)
                            *ch[band].coefficients = *coeffs[band];
                last = v;
            }

            const int n = b.getNumSamples();
            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            {
                auto* data = b.getWritePointer (ch);
                for (size_t band = 0; band < 6; ++band)
                {
                    if (coeffs[band] == nullptr)
                        continue;
                    auto& filter = filters[(size_t) ch][band];
                    for (int i = 0; i < n; ++i)
                        data[i] = filter.processSample (data[i]);
                }
            }

            b.applyGain (dbToGain (param ("output")));
        }

        int getDisplayHeight() const override { return 170; }

        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r) override
        {
            using namespace Rakam::Colours;
            g.setColour (background);
            g.fillRoundedRectangle (r, 6.0f);

            const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            auto c = makeCoeffs (read(), sr);
            const float range = 18.0f;
            auto xFor = [&r] (double hz) { return r.getX() + r.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
            auto yFor = [&r, range] (float db) { return r.getCentreY() - (db / range) * (r.getHeight() * 0.45f); };

            g.setFont (juce::FontOptions (10.0f));
            for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                const float x = xFor (hz);
                g.setColour (grid);
                g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
                g.setColour (textDim);
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x + 2.0f, r.getBottom() - 14.0f, 30.0f, 12.0f), juce::Justification::left);
            }
            for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
            {
                g.setColour (juce::exactlyEqual (db, 0.0f) ? gridStrong : grid);
                g.drawHorizontalLine ((int) yFor (db), r.getX(), r.getRight());
            }

            juce::Path curve;
            const int steps = juce::jmax (2, (int) r.getWidth());
            for (int i = 0; i <= steps; ++i)
            {
                const double hz = 20.0 * std::pow (1000.0, (double) i / steps);
                double mag = dbToGain (param ("output"));
                for (auto& co : c)
                    if (co != nullptr)
                        mag *= co->getMagnitudeForFrequency (hz, sr);
                const float y = juce::jlimit (r.getY(), r.getBottom(), yFor (juce::Decibels::gainToDecibels ((float) mag, -60.0f)));
                if (i == 0) curve.startNewSubPath (r.getX(), y);
                else        curve.lineTo (xFor (hz), y);
            }

            juce::Path fill (curve);
            fill.lineTo (r.getRight(), yFor (0.0f));
            fill.lineTo (r.getX(), yFor (0.0f));
            fill.closeSubPath();
            g.setColour (accent.withAlpha (0.15f));
            g.fillPath (fill);
            g.setColour (accent);
            g.strokePath (curve, juce::PathStrokeType (2.0f));
        }

    private:
        double sampleRate = 48000.0;
        Values last {};
        std::array<Coeffs::Ptr, 6> coeffs;
        std::array<std::array<juce::dsp::IIR::Filter<float>, 6>, 2> filters;
    };

    //==============================================================================
    class CompressorPlugin : public BuiltInPlugin
    {
    public:
        CompressorPlugin() : BuiltInPlugin ("Compressor", "Dynamics", layout())
        {
            presets = {
                { "Default", {} },
                { "Vocal smooth",  { { "threshold", -20.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 120.0f }, { "makeup", 4.0f } } },
                { "Drum bus glue", { { "threshold", -14.0f }, { "ratio", 2.0f }, { "attack", 30.0f }, { "release", 150.0f }, { "makeup", 2.0f } } },
                { "Punchy kick",   { { "threshold", -16.0f }, { "ratio", 4.0f }, { "attack", 25.0f }, { "release", 80.0f }, { "makeup", 3.0f } } },
                { "Bass even",     { { "threshold", -22.0f }, { "ratio", 5.0f }, { "attack", 5.0f }, { "release", 200.0f }, { "makeup", 5.0f } } },
                { "Parallel smash",{ { "threshold", -35.0f }, { "ratio", 12.0f }, { "attack", 1.0f }, { "release", 60.0f }, { "makeup", 12.0f }, { "mix", 35.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("threshold", "Threshold", linRange (-60.0f, 0.0f), -18.0f, dbText));
            l.add (makeParam ("ratio",     "Ratio",     logRange (1.0f, 20.0f), 4.0f, ratioText));
            l.add (makeParam ("attack",    "Attack",    logRange (0.1f, 100.0f), 10.0f, msText));
            l.add (makeParam ("release",   "Release",   logRange (10.0f, 1000.0f), 120.0f, msText));
            l.add (makeParam ("makeup",    "Makeup",    linRange (0.0f, 24.0f), 0.0f, dbText));
            l.add (makeParam ("mix",       "Mix",       linRange (0.0f, 100.0f), 100.0f, pctText));
            return l;
        }

        void prepareToPlay (double sr, int bs) override
        {
            comp.prepare ({ sr, (juce::uint32) bs, 2 });
            dry.setSize (2, bs);
        }

        void process (juce::AudioBuffer<float>& b) override
        {
            const int n = b.getNumSamples(), chans = juce::jmin (2, b.getNumChannels());
            comp.setThreshold (param ("threshold"));
            comp.setRatio (param ("ratio"));
            comp.setAttack (param ("attack"));
            comp.setRelease (param ("release"));

            dry.setSize (2, n, false, false, true);
            for (int ch = 0; ch < chans; ++ch)
                dry.copyFrom (ch, 0, b, ch, 0, n);

            const float inPeak = b.getMagnitude (0, n);
            juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
            comp.process (juce::dsp::ProcessContextReplacing<float> (block));
            const float outPeak = b.getMagnitude (0, n);

            const float gr = inPeak > 1.0e-5f ? juce::jmin (0.0f, juce::Decibels::gainToDecibels (outPeak / inPeak)) : 0.0f;
            reductionDb = juce::jmin (gr, reductionDb.load() * 0.9f + gr * 0.1f);

            const float mix = param ("mix") / 100.0f;
            const float makeup = dbToGain (param ("makeup"));
            for (int ch = 0; ch < chans; ++ch)
            {
                b.applyGain (ch, 0, n, makeup * mix);
                b.addFrom (ch, 0, dry, ch, 0, n, 1.0f - mix);
            }
        }

        int getDisplayHeight() const override { return 64; }
        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r) override
        {
            drawMeterBar (g, r, "Gain reduction", reductionDb.load(), 24.0f);
        }

    private:
        juce::dsp::Compressor<float> comp;
        juce::AudioBuffer<float> dry;
        std::atomic<float> reductionDb { 0.0f };
    };

    //==============================================================================
    class ReverbPlugin : public BuiltInPlugin
    {
    public:
        ReverbPlugin() : BuiltInPlugin ("Reverb", "Reverb", layout())
        {
            tailSeconds = 6.0;
            presets = {
                { "Room",        { { "size", 0.35f }, { "damping", 0.6f }, { "mix", 18.0f } } },
                { "Vocal plate", { { "size", 0.55f }, { "damping", 0.35f }, { "mix", 22.0f }, { "width", 100.0f } } },
                { "Hall",        { { "size", 0.8f },  { "damping", 0.45f }, { "mix", 28.0f } } },
                { "Huge space",  { { "size", 0.95f }, { "damping", 0.25f }, { "mix", 40.0f } } },
                { "Bus (100% wet)", { { "size", 0.7f }, { "damping", 0.5f }, { "mix", 100.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("size",    "Size",    linRange (0.0f, 1.0f), 0.5f, plainText));
            l.add (makeParam ("damping", "Damping", linRange (0.0f, 1.0f), 0.5f, plainText));
            l.add (makeParam ("width",   "Width",   linRange (0.0f, 100.0f), 100.0f, pctText));
            l.add (makeParam ("mix",     "Mix",     linRange (0.0f, 100.0f), 20.0f, pctText));
            return l;
        }

        void prepareToPlay (double sr, int bs) override { reverb.prepare ({ sr, (juce::uint32) bs, 2 }); reverb.reset(); }

        void process (juce::AudioBuffer<float>& b) override
        {
            juce::dsp::Reverb::Parameters p;
            const float mix = param ("mix") / 100.0f;
            p.roomSize = param ("size");
            p.damping  = param ("damping");
            p.width    = param ("width") / 100.0f;
            p.wetLevel = mix * 0.5f;
            p.dryLevel = 1.0f - mix;
            reverb.setParameters (p);

            juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), (size_t) juce::jmin (2, b.getNumChannels()), (size_t) b.getNumSamples());
            reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::Reverb reverb;
    };

    //==============================================================================
    class DelayPlugin : public BuiltInPlugin
    {
    public:
        DelayPlugin() : BuiltInPlugin ("Delay", "Delay", layout())
        {
            tailSeconds = 8.0;
            presets = {
                { "1/4 echo",      { { "sync", 1.0f }, { "feedback", 30.0f }, { "level", 25.0f } } },
                { "1/8 dotted",    { { "sync", 3.0f }, { "feedback", 40.0f }, { "level", 22.0f } } },
                { "Ping-pong 1/8", { { "sync", 2.0f }, { "feedback", 45.0f }, { "level", 25.0f }, { "pingpong", 1.0f } } },
                { "Slapback",      { { "sync", 0.0f }, { "time", 110.0f }, { "feedback", 5.0f }, { "level", 30.0f }, { "tone", 5000.0f } } },
                { "Dub",           { { "sync", 1.0f }, { "feedback", 70.0f }, { "level", 30.0f }, { "tone", 2500.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "sync", 1 }, "Sync",
                                                                 juce::StringArray { "Off (ms)", "1/4", "1/8", "1/8 dotted", "1/16", "1/4 triplet", "1/2" }, 1));
            l.add (makeParam ("time",     "Time",     logRange (1.0f, 2000.0f), 375.0f, msText));
            l.add (makeParam ("feedback", "Feedback", linRange (0.0f, 95.0f), 35.0f, pctText));
            l.add (makeParam ("tone",     "Tone",     logRange (1000.0f, 20000.0f), 8000.0f, hzText));
            l.add (makeParam ("level",    "Level",    linRange (0.0f, 100.0f), 25.0f, pctText));
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "pingpong", 1 }, "Ping-pong", false));
            return l;
        }

        void prepareToPlay (double sr, int bs) override
        {
            sampleRate = sr;
            line.setMaximumDelayInSamples ((int) (sr * 4.0) + 8);
            line.prepare ({ sr, (juce::uint32) bs, 2 });
            line.reset();
            lp[0] = lp[1] = 0.0f;
        }

        void process (juce::AudioBuffer<float>& b) override
        {
            static constexpr double beats[] = { 0.0, 1.0, 0.5, 0.75, 0.25, 2.0 / 3.0, 2.0 };
            const int sync = juce::roundToInt (param ("sync"));
            double seconds = sync == 0 ? param ("time") / 1000.0 : beats[juce::jlimit (0, 6, sync)] * 60.0 / getHostBpm();
            seconds = juce::jlimit (0.001, 3.9, seconds);
            line.setDelay ((float) (seconds * sampleRate));

            const float fb = param ("feedback") / 100.0f;
            const float level = param ("level") / 100.0f;
            const float toneCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * param ("tone") / (float) sampleRate);
            const bool pingpong = param ("pingpong") > 0.5f && b.getNumChannels() > 1;
            const bool stereo = b.getNumChannels() > 1;

            auto* l = b.getWritePointer (0);
            auto* r = stereo ? b.getWritePointer (1) : nullptr;

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float inL = l[i], inR = stereo ? r[i] : inL;
                const float d0 = line.popSample (0), d1 = line.popSample (1);
                lp[0] += (d0 - lp[0]) * toneCoef;
                lp[1] += (d1 - lp[1]) * toneCoef;

                if (pingpong)
                {
                    line.pushSample (0, (inL + inR) * 0.5f + fb * lp[1]);
                    line.pushSample (1, fb * lp[0]);
                }
                else
                {
                    line.pushSample (0, inL + fb * lp[0]);
                    line.pushSample (1, inR + fb * lp[1]);
                }

                l[i] = inL + level * d0;
                if (stereo)
                    r[i] = inR + level * d1;
            }
        }

    private:
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 192000 };
        double sampleRate = 48000.0;
        float lp[2] {};
    };

    //==============================================================================
    class ChorusPlugin : public BuiltInPlugin
    {
    public:
        ChorusPlugin() : BuiltInPlugin ("Chorus", "Modulation", layout())
        {
            presets = {
                { "Subtle widen", { { "rate", 0.4f }, { "depth", 0.2f }, { "mix", 35.0f } } },
                { "Classic",      { { "rate", 0.8f }, { "depth", 0.4f }, { "mix", 50.0f } } },
                { "Flanger",      { { "rate", 0.25f }, { "depth", 0.7f }, { "delay", 2.0f }, { "feedback", 0.6f }, { "mix", 50.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("rate",     "Rate",     logRange (0.05f, 5.0f), 0.8f, [] (float v, int) { return juce::String (v, 2) + " Hz"; }));
            l.add (makeParam ("depth",    "Depth",    linRange (0.0f, 1.0f), 0.4f, plainText));
            l.add (makeParam ("delay",    "Delay",    linRange (1.0f, 30.0f), 8.0f, msText));
            l.add (makeParam ("feedback", "Feedback", linRange (-0.9f, 0.9f), 0.0f, plainText));
            l.add (makeParam ("mix",      "Mix",      linRange (0.0f, 100.0f), 50.0f, pctText));
            return l;
        }

        void prepareToPlay (double sr, int bs) override { chorus.prepare ({ sr, (juce::uint32) bs, 2 }); chorus.reset(); }

        void process (juce::AudioBuffer<float>& b) override
        {
            chorus.setRate (param ("rate"));
            chorus.setDepth (param ("depth"));
            chorus.setCentreDelay (param ("delay"));
            chorus.setFeedback (param ("feedback"));
            chorus.setMix (param ("mix") / 100.0f);
            juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), (size_t) juce::jmin (2, b.getNumChannels()), (size_t) b.getNumSamples());
            chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::Chorus<float> chorus;
    };

    //==============================================================================
    class PhaserPlugin : public BuiltInPlugin
    {
    public:
        PhaserPlugin() : BuiltInPlugin ("Phaser", "Modulation", layout())
        {
            presets = {
                { "Slow sweep", { { "rate", 0.2f }, { "depth", 0.7f }, { "feedback", 0.4f }, { "mix", 50.0f } } },
                { "Funky",      { { "rate", 1.5f }, { "depth", 0.8f }, { "centre", 800.0f }, { "feedback", 0.6f }, { "mix", 50.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("rate",     "Rate",     logRange (0.05f, 10.0f), 0.5f, [] (float v, int) { return juce::String (v, 2) + " Hz"; }));
            l.add (makeParam ("depth",    "Depth",    linRange (0.0f, 1.0f), 0.6f, plainText));
            l.add (makeParam ("centre",   "Centre",   logRange (100.0f, 5000.0f), 1300.0f, hzText));
            l.add (makeParam ("feedback", "Feedback", linRange (-0.9f, 0.9f), 0.3f, plainText));
            l.add (makeParam ("mix",      "Mix",      linRange (0.0f, 100.0f), 50.0f, pctText));
            return l;
        }

        void prepareToPlay (double sr, int bs) override { phaser.prepare ({ sr, (juce::uint32) bs, 2 }); phaser.reset(); }

        void process (juce::AudioBuffer<float>& b) override
        {
            phaser.setRate (param ("rate"));
            phaser.setDepth (param ("depth"));
            phaser.setCentreFrequency (param ("centre"));
            phaser.setFeedback (param ("feedback"));
            phaser.setMix (param ("mix") / 100.0f);
            juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), (size_t) juce::jmin (2, b.getNumChannels()), (size_t) b.getNumSamples());
            phaser.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::Phaser<float> phaser;
    };

    //==============================================================================
    class SaturatorPlugin : public BuiltInPlugin
    {
    public:
        SaturatorPlugin() : BuiltInPlugin ("Saturator", "Distortion", layout())
        {
            presets = {
                { "Warm tape",   { { "drive", 6.0f },  { "tone", 9000.0f },  { "mix", 100.0f } } },
                { "Crunch",      { { "drive", 18.0f }, { "tone", 7000.0f },  { "mix", 100.0f }, { "output", -4.0f } } },
                { "Bass grit",   { { "drive", 14.0f }, { "tone", 4000.0f },  { "mix", 40.0f } } },
                { "Fuzz",        { { "drive", 32.0f }, { "tone", 5000.0f },  { "mix", 100.0f }, { "output", -8.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("drive",  "Drive",  linRange (0.0f, 36.0f), 6.0f, dbText));
            l.add (makeParam ("tone",   "Tone",   logRange (1000.0f, 20000.0f), 12000.0f, hzText));
            l.add (makeParam ("mix",    "Mix",    linRange (0.0f, 100.0f), 100.0f, pctText));
            l.add (makeParam ("output", "Output", linRange (-24.0f, 6.0f), 0.0f, dbText));
            return l;
        }

        void prepareToPlay (double sr, int) override { sampleRate = sr; lp[0] = lp[1] = 0.0f; }

        void process (juce::AudioBuffer<float>& b) override
        {
            const float g = dbToGain (param ("drive"));
            const float makeup = 1.0f / std::sqrt (g);
            const float mix = param ("mix") / 100.0f;
            const float out = dbToGain (param ("output"));
            const float coef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * param ("tone") / (float) sampleRate);

            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            {
                auto* d = b.getWritePointer (ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float wet = std::tanh (g * d[i]) * makeup;
                    lp[ch] += (wet - lp[ch]) * coef;
                    d[i] = (lp[ch] * mix + d[i] * (1.0f - mix)) * out;
                }
            }
        }

    private:
        double sampleRate = 48000.0;
        float lp[2] {};
    };

    //==============================================================================
    class LimiterPlugin : public BuiltInPlugin
    {
    public:
        LimiterPlugin() : BuiltInPlugin ("Limiter", "Dynamics", layout())
        {
            presets = {
                { "Safe master",   { { "gain", 0.0f },  { "ceiling", -1.0f }, { "release", 100.0f } } },
                { "Loud master",   { { "gain", 6.0f },  { "ceiling", -0.3f }, { "release", 60.0f } } },
                { "Streaming -1",  { { "gain", 3.0f },  { "ceiling", -1.0f }, { "release", 120.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("gain",    "Gain",    linRange (0.0f, 24.0f), 0.0f, dbText));
            l.add (makeParam ("ceiling", "Ceiling", linRange (-12.0f, 0.0f), -0.3f, dbText));
            l.add (makeParam ("release", "Release", logRange (1.0f, 1000.0f), 100.0f, msText));
            return l;
        }

        void prepareToPlay (double sr, int) override { sampleRate = sr; envelope = 1.0f; }

        void process (juce::AudioBuffer<float>& b) override
        {
            const float inGain = dbToGain (param ("gain"));
            const float ceiling = dbToGain (param ("ceiling"));
            const float rel = 1.0f - std::exp (-1.0f / (0.001f * param ("release") * (float) sampleRate));
            const int chans = juce::jmin (2, b.getNumChannels());
            float minGain = 1.0f;

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float peak = 0.0f;
                for (int ch = 0; ch < chans; ++ch)
                    peak = juce::jmax (peak, std::abs (b.getSample (ch, i) * inGain));

                const float target = peak > ceiling ? ceiling / peak : 1.0f;
                envelope = target < envelope ? target : envelope + (target - envelope) * rel;
                minGain = juce::jmin (minGain, envelope);

                for (int ch = 0; ch < chans; ++ch)
                {
                    const float v = b.getSample (ch, i) * inGain * envelope;
                    b.setSample (ch, i, juce::jlimit (-ceiling, ceiling, v));
                }
            }

            const float gr = juce::Decibels::gainToDecibels (minGain);
            reductionDb = juce::jmin (gr, reductionDb.load() * 0.9f + gr * 0.1f);
        }

        int getDisplayHeight() const override { return 64; }
        void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r) override
        {
            drawMeterBar (g, r, "Gain reduction", reductionDb.load(), 12.0f);
        }

    private:
        double sampleRate = 48000.0;
        float envelope = 1.0f;
        std::atomic<float> reductionDb { 0.0f };
    };

    //==============================================================================
    class GatePlugin : public BuiltInPlugin
    {
    public:
        GatePlugin() : BuiltInPlugin ("Gate", "Dynamics", layout())
        {
            presets = {
                { "Remove hiss",  { { "threshold", -55.0f }, { "ratio", 10.0f }, { "attack", 1.0f }, { "release", 150.0f } } },
                { "Tight drums",  { { "threshold", -30.0f }, { "ratio", 40.0f }, { "attack", 0.5f }, { "release", 60.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("threshold", "Threshold", linRange (-80.0f, 0.0f), -50.0f, dbText));
            l.add (makeParam ("ratio",     "Ratio",     logRange (1.0f, 100.0f), 10.0f, ratioText));
            l.add (makeParam ("attack",    "Attack",    logRange (0.1f, 50.0f), 1.0f, msText));
            l.add (makeParam ("release",   "Release",   logRange (5.0f, 1000.0f), 100.0f, msText));
            return l;
        }

        void prepareToPlay (double sr, int bs) override { gate.prepare ({ sr, (juce::uint32) bs, 2 }); gate.reset(); }

        void process (juce::AudioBuffer<float>& b) override
        {
            gate.setThreshold (param ("threshold"));
            gate.setRatio (param ("ratio"));
            gate.setAttack (param ("attack"));
            gate.setRelease (param ("release"));
            juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), (size_t) juce::jmin (2, b.getNumChannels()), (size_t) b.getNumSamples());
            gate.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        juce::dsp::NoiseGate<float> gate;
    };

    //==============================================================================
    class FilterPlugin : public BuiltInPlugin
    {
    public:
        FilterPlugin() : BuiltInPlugin ("Filter", "Filter", layout())
        {
            presets = {
                { "Low pass",     { { "mode", 0.0f }, { "cutoff", 2000.0f }, { "resonance", 0.2f } } },
                { "High pass",    { { "mode", 1.0f }, { "cutoff", 300.0f }, { "resonance", 0.1f } } },
                { "Radio band",   { { "mode", 2.0f }, { "cutoff", 1200.0f }, { "resonance", 0.4f } } },
                { "Acid squelch", { { "mode", 0.0f }, { "cutoff", 900.0f }, { "resonance", 0.8f }, { "drive", 3.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode",
                                                                 juce::StringArray { "Low pass", "High pass", "Band pass" }, 0));
            l.add (makeParam ("cutoff",    "Cutoff",    logRange (20.0f, 20000.0f), 20000.0f, hzText));
            l.add (makeParam ("resonance", "Resonance", linRange (0.0f, 1.0f), 0.1f, plainText));
            l.add (makeParam ("drive",     "Drive",     linRange (1.0f, 10.0f), 1.0f, plainText));
            return l;
        }

        void prepareToPlay (double sr, int bs) override { sampleRate = sr; filter.prepare ({ sr, (juce::uint32) bs, 2 }); filter.reset(); }

        void process (juce::AudioBuffer<float>& b) override
        {
            using M = juce::dsp::LadderFilterMode;
            const int mode = juce::roundToInt (param ("mode"));
            filter.setMode (mode == 1 ? M::HPF24 : mode == 2 ? M::BPF24 : M::LPF24);
            filter.setCutoffFrequencyHz (juce::jmin (param ("cutoff"), (float) sampleRate * 0.45f));
            filter.setResonance (param ("resonance"));
            filter.setDrive (param ("drive"));
            juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), (size_t) juce::jmin (2, b.getNumChannels()), (size_t) b.getNumSamples());
            filter.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

    private:
        double sampleRate = 48000.0;
        juce::dsp::LadderFilter<float> filter;
    };

    //==============================================================================
    class UtilityPlugin : public BuiltInPlugin
    {
    public:
        UtilityPlugin() : BuiltInPlugin ("Utility", "Utility", layout())
        {
            presets = {
                { "Default", {} },
                { "Mono check", { { "mono", 1.0f } } },
                { "Wide",       { { "width", 150.0f } } },
            };
        }

        static Layout layout()
        {
            Layout l;
            l.add (makeParam ("gain",  "Gain",  linRange (-36.0f, 24.0f), 0.0f, dbText));
            l.add (makeParam ("pan",   "Pan",   linRange (-100.0f, 100.0f), 0.0f, [] (float v, int)
            {
                const int p = juce::roundToInt (v);
                return p == 0 ? juce::String ("C") : juce::String (std::abs (p)) + (p < 0 ? "L" : "R");
            }));
            l.add (makeParam ("width", "Width", linRange (0.0f, 200.0f), 100.0f, pctText));
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "mono", 1 }, "Mono", false));
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "invert", 1 }, "Phase invert", false));
            return l;
        }

        void prepareToPlay (double, int) override {}

        void process (juce::AudioBuffer<float>& b) override
        {
            const float gain = dbToGain (param ("gain")) * (param ("invert") > 0.5f ? -1.0f : 1.0f);
            const float pan = param ("pan") / 100.0f;
            const float width = param ("mono") > 0.5f ? 0.0f : param ("width") / 100.0f;

            if (b.getNumChannels() < 2)
            {
                b.applyGain (gain);
                return;
            }

            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            const float gl = gain * juce::jmin (1.0f, 1.0f - pan), gr = gain * juce::jmin (1.0f, 1.0f + pan);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float mid = (l[i] + r[i]) * 0.5f, side = (l[i] - r[i]) * 0.5f * width;
                l[i] = (mid + side) * gl;
                r[i] = (mid - side) * gr;
            }
        }
    };
}

//==============================================================================
BuiltInPlugin::BuiltInPlugin (const juce::String& name, const juce::String& category, Layout layout)
    : juce::AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PARAMS", std::move (layout)),
      pluginName (name), pluginCategory (category)
{
}

void BuiltInPlugin::fillInPluginDescription (juce::PluginDescription& d) const
{
    d.name              = pluginName;
    d.descriptiveName   = "Rakam " + pluginName;
    d.pluginFormatName  = BuiltInPluginFormat::formatName;
    d.category          = pluginCategory;
    d.manufacturerName  = "Rakam Studio";
    d.version           = "1.0";
    d.fileOrIdentifier  = identifierPrefix() + pluginName;
    d.uniqueId          = pluginName.hashCode();
    d.deprecatedUid     = d.uniqueId;
    d.isInstrument      = false;
    d.numInputChannels  = 2;
    d.numOutputChannels = 2;
}

bool BuiltInPlugin::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet(), out = layout.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

juce::AudioProcessorEditor* BuiltInPlugin::createEditor()
{
    return new RakamPluginEditor (*this);
}

void BuiltInPlugin::resetToDefaults()
{
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
}

void BuiltInPlugin::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentPreset = index;
    resetToDefaults();

    for (auto& [id, value] : presets[(size_t) index].values)
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
}

const juce::String BuiltInPlugin::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? presets[(size_t) index].name : juce::String ("Default");
}

void BuiltInPlugin::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = state.copyState();
    tree.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, dest);
}

void BuiltInPlugin::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (state.state.getType()))
        {
            currentPreset = tree.getProperty ("preset", 0);
            state.replaceState (tree);
        }
    }
}

double BuiltInPlugin::getHostBpm() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                return juce::jlimit (20.0, 400.0, *bpm);
    return 120.0;
}

//==============================================================================
juce::StringArray BuiltInPluginFormat::getEffectNames()
{
    return { "EQ", "Compressor", "Limiter", "Gate", "Reverb", "Delay", "Chorus", "Phaser", "Saturator", "Filter", "Utility" };
}

std::unique_ptr<BuiltInPlugin> BuiltInPluginFormat::create (const juce::String& nameOrIdentifier)
{
    const auto name = nameOrIdentifier.startsWith (BuiltInPlugin::identifierPrefix())
                          ? nameOrIdentifier.fromFirstOccurrenceOf (BuiltInPlugin::identifierPrefix(), false, false)
                          : nameOrIdentifier;

    if (name == "EQ")         return std::make_unique<EqPlugin>();
    if (name == "Compressor") return std::make_unique<CompressorPlugin>();
    if (name == "Limiter")    return std::make_unique<LimiterPlugin>();
    if (name == "Gate")       return std::make_unique<GatePlugin>();
    if (name == "Reverb")     return std::make_unique<ReverbPlugin>();
    if (name == "Delay")      return std::make_unique<DelayPlugin>();
    if (name == "Chorus")     return std::make_unique<ChorusPlugin>();
    if (name == "Phaser")     return std::make_unique<PhaserPlugin>();
    if (name == "Saturator")  return std::make_unique<SaturatorPlugin>();
    if (name == "Filter")     return std::make_unique<FilterPlugin>();
    if (name == "Utility")    return std::make_unique<UtilityPlugin>();
    return nullptr;
}

juce::Array<juce::PluginDescription> BuiltInPluginFormat::getAllDescriptions()
{
    juce::Array<juce::PluginDescription> list;
    for (auto& name : getEffectNames())
    {
        if (auto p = create (name))
        {
            juce::PluginDescription d;
            p->fillInPluginDescription (d);
            list.add (d);
        }
    }
    return list;
}

void BuiltInPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& id)
{
    if (auto p = create (id))
    {
        auto* d = new juce::PluginDescription();
        p->fillInPluginDescription (*d);
        results.add (d);
    }
}

void BuiltInPluginFormat::createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback callback)
{
    if (auto p = create (d.fileOrIdentifier.isNotEmpty() ? d.fileOrIdentifier : d.name))
        callback (std::move (p), {});
    else
        callback (nullptr, "Unknown built-in effect: " + d.name);
}
