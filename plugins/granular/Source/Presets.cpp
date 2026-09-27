#include "Presets.h"
#include "PluginProcessor.h"

namespace thf::grain
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        // Sources: 1 Saw Pad, 2 Voice, 3 Bell, 4 Noise, 5 Glass, 6 Vocal Phrase, 7 Chord Stack,
        //          8 Pluck, 9 Keys.
        // LFO targets: 0 Position, 1 Spray, 2 Size, 3 Density, 4 Pitch, 5 Cutoff, 6 Level.
        // LFO shapes: 0 Sine, 1 Triangle, 2 Saw, 3 Square, 4 Random.
        // LFO divisions: 2 = 1 bar, 3 = 1/2, 4 = 1/4, 5 = 1/8, 6 = 1/16.
        // Quantize: 1 Octaves, 2 Fifths, 3 Major, 4 Minor. Sync rates: 3 = 1/16, 5 = 1/32.
        static const std::vector<FactoryPreset> list {
            { "Init", "Basic", {} },

            //------------------------------------------------------------------ Pads
            { "Slow Tide", "Pads", {
                { pid::source, 1 }, { pid::position, 0.2f }, { pid::scan, 0.05f }, { pid::spray, 0.08f },
                { pid::size, 350 }, { pid::density, 18 }, { pid::chaos, 0.5f }, { pid::stereo, 0.8f },
                { pid::attack, 1200 }, { pid::decay, 2000 }, { pid::sustain, 0.9f }, { pid::release, 3500 },
                { pid::cutoff, 6000 }, { pid::lfoRate, 0.08f }, { pid::lfoDepth, 0.3f }, { pid::lfoTarget, 0 },
                { pid::space, 0.4f }, { pid::output, -2 } } },

            { "Glass Hall", "Pads", {
                { pid::source, 5 }, { pid::position, 0.3f }, { pid::spray, 0.3f }, { pid::size, 600 },
                { pid::density, 12 }, { pid::jitter, 0.1f }, { pid::stereo, 1.0f }, { pid::reverse, 0.3f },
                { pid::attack, 900 }, { pid::sustain, 0.85f }, { pid::release, 5000 }, { pid::space, 0.55f }, { pid::output, -3.5f } } },

            { "Low Drone", "Pads", {
                { pid::source, 1 }, { pid::pitch, -12 }, { pid::size, 500 }, { pid::density, 15 },
                { pid::spray, 0.15f }, { pid::cutoff, 1200 }, { pid::resonance, 0.3f }, { pid::drive, 0.3f },
                { pid::voiceMode, 1 }, { pid::glide, 300 }, { pid::attack, 800 }, { pid::release, 2500 },
                { pid::space, 0.3f }, { pid::output, 5 } } },

            { "Octave Bloom", "Pads", {
                { pid::source, 1 }, { pid::quantize, 1 }, { pid::jitter, 12 }, { pid::size, 400 },
                { pid::density, 30 }, { pid::spray, 0.3f }, { pid::reverse, 0.3f }, { pid::stereo, 1.0f },
                { pid::chaos, 0.7f }, { pid::attack, 1500 }, { pid::release, 5000 }, { pid::cutoff, 9000 },
                { pid::space, 0.7f }, { pid::spaceSize, 0.85f }, { pid::output, -2.5f } } },

            { "Warm Chord Cloud", "Pads", {
                { pid::source, 1 }, { pid::position, 0.1f }, { pid::spray, 0.2f }, { pid::size, 300 },
                { pid::density, 25 }, { pid::chaos, 0.6f }, { pid::stereo, 0.9f }, { pid::attack, 400 },
                { pid::release, 3000 }, { pid::cutoff, 7000 }, { pid::space, 0.45f }, { pid::output, 1 } } },

            { "Warble Pad", "Pads", {
                { pid::source, 1 }, { pid::position, 0.55f }, { pid::spray, 0.1f }, { pid::size, 260 },
                { pid::density, 28 }, { pid::chaos, 0.5f }, { pid::jitter, 0.15f }, { pid::stereo, 0.9f },
                { pid::lfoTarget, 4 }, { pid::lfoRate, 0.7f }, { pid::lfoDepth, 0.035f }, { pid::cutoff, 6000 },
                { pid::attack, 600 }, { pid::release, 3000 }, { pid::space, 0.5f }, { pid::output, -1 } } },

            { "Shimmer Fifths", "Pads", {
                { pid::source, 5 }, { pid::quantize, 2 }, { pid::jitter, 19 }, { pid::spray, 0.4f },
                { pid::size, 450 }, { pid::density, 22 }, { pid::chaos, 0.8f }, { pid::stereo, 1.0f },
                { pid::attack, 1200 }, { pid::release, 5000 }, { pid::space, 0.65f }, { pid::spaceSize, 0.9f }, { pid::output, -4 } } },

            //------------------------------------------------------------------ Textures
            { "Choir of Dust", "Textures", {
                { pid::source, 2 }, { pid::position, 0.5f }, { pid::spray, 0.5f }, { pid::size, 180 },
                { pid::density, 40 }, { pid::chaos, 0.8f }, { pid::jitter, 0.15f }, { pid::stereo, 0.9f },
                { pid::attack, 600 }, { pid::release, 2500 }, { pid::cutoff, 5000 }, { pid::resonance, 0.2f },
                { pid::space, 0.4f }, { pid::output, -4 } } },

            { "Frozen Breath", "Textures", {
                { pid::source, 2 }, { pid::freeze, 1 }, { pid::position, 0.35f }, { pid::spray, 0.04f },
                { pid::size, 90 }, { pid::density, 60 }, { pid::stereo, 0.7f }, { pid::window, 0.5f },
                { pid::attack, 200 }, { pid::release, 1500 }, { pid::space, 0.35f }, { pid::output, -0.5f } } },

            { "Reverse Mist", "Textures", {
                { pid::source, 5 }, { pid::reverse, 1.0f }, { pid::size, 400 }, { pid::density, 20 },
                { pid::spray, 0.4f }, { pid::stereo, 0.9f }, { pid::attack, 700 }, { pid::release, 3000 },
                { pid::cutoff, 7000 }, { pid::space, 0.5f }, { pid::output, -2 } } },

            { "Rain on Glass", "Textures", {
                { pid::source, 5 }, { pid::size, 18 }, { pid::density, 140 }, { pid::chaos, 1.0f },
                { pid::spray, 1.0f }, { pid::stereo, 1.0f }, { pid::reverse, 0.5f }, { pid::jitter, 12 },
                { pid::quantize, 3 }, { pid::attack, 300 }, { pid::release, 2500 }, { pid::space, 0.5f }, { pid::output, -3 } } },

            { "Cloud Chamber", "Textures", {
                { pid::source, 1 }, { pid::spray, 0.6f }, { pid::size, 220 }, { pid::density, 45 },
                { pid::chaos, 1.0f }, { pid::jitter, 7 }, { pid::quantize, 2 }, { pid::stereo, 1.0f },
                { pid::filterType, 1 }, { pid::cutoff, 1400 }, { pid::resonance, 0.35f }, { pid::attack, 900 },
                { pid::release, 3500 }, { pid::space, 0.6f }, { pid::output, 8.5f } } },

            //------------------------------------------------------------------ Vocal chops
            { "Chopped Phrase", "Vocal Chops", {
                { pid::source, 6 }, { pid::sync, 1 }, { pid::syncRate, 3 }, { pid::chaos, 0.0f },
                { pid::spray, 0.5f }, { pid::size, 110 }, { pid::window, 0.6f }, { pid::stereo, 0.6f },
                { pid::attack, 5 }, { pid::release, 400 }, { pid::space, 0.35f }, { pid::output, -2.5f } } },

            { "Pitched Stutter", "Vocal Chops", {
                { pid::source, 6 }, { pid::position, 0.0625f }, { pid::sync, 1 }, { pid::syncRate, 5 }, { pid::chaos, 0.0f },
                { pid::size, 45 }, { pid::spray, 0.02f }, { pid::window, 0.5f }, { pid::lfoTarget, 0 },
                { pid::lfoShape, 4 }, { pid::lfoMode, 1 }, { pid::lfoDivision, 5 }, { pid::lfoDepth, 0.06f },
                { pid::attack, 3 }, { pid::release, 300 }, { pid::space, 0.25f }, { pid::output, 2.5f } } },

            { "Vocal Scatter", "Vocal Chops", {
                { pid::source, 6 }, { pid::spray, 1.0f }, { pid::size, 120 }, { pid::density, 18 },
                { pid::chaos, 1.0f }, { pid::jitter, 12 }, { pid::quantize, 4 }, { pid::stereo, 1.0f },
                { pid::attack, 20 }, { pid::release, 1500 }, { pid::space, 0.5f }, { pid::output, -1.5f } } },

            { "Glide Choir", "Vocal Chops", {
                { pid::source, 6 }, { pid::voiceMode, 2 }, { pid::glide, 180 }, { pid::scan, 0.25f },
                { pid::size, 200 }, { pid::density, 30 }, { pid::spray, 0.03f }, { pid::stereo, 0.5f },
                { pid::attack, 60 }, { pid::release, 1200 }, { pid::space, 0.45f }, { pid::output, 1 } } },

            { "Breathy Vox Pad", "Vocal Chops", {
                { pid::source, 6 }, { pid::freeze, 1 }, { pid::position, 0.8125f }, { pid::pitch, -12 }, { pid::spray, 0.05f },
                { pid::size, 250 }, { pid::density, 40 }, { pid::jitter, 0.1f }, { pid::stereo, 0.8f },
                { pid::attack, 800 }, { pid::release, 3000 }, { pid::space, 0.6f }, { pid::output, -8 } } },

            { "Formant Rain", "Vocal Chops", {
                { pid::source, 6 }, { pid::size, 30 }, { pid::density, 110 }, { pid::spray, 0.8f },
                { pid::chaos, 1.0f }, { pid::jitter, 24 }, { pid::quantize, 3 }, { pid::stereo, 1.0f },
                { pid::attack, 150 }, { pid::release, 2000 }, { pid::space, 0.5f }, { pid::output, -2 } } },

            //------------------------------------------------------------------ Chords
            { "Future Stab", "Chords", {
                { pid::source, 7 }, { pid::voiceMode, 1 }, { pid::position, 0.0f }, { pid::spray, 0.01f }, { pid::size, 350 },
                { pid::density, 45 }, { pid::chaos, 0.2f }, { pid::stereo, 0.7f }, { pid::attack, 2 },
                { pid::decay, 450 }, { pid::sustain, 0.5f }, { pid::release, 350 }, { pid::filterEnv, 0.35f },
                { pid::filterDecay, 250 }, { pid::cutoff, 3500 }, { pid::resonance, 0.3f }, { pid::drive, 0.25f },
                { pid::space, 0.3f }, { pid::output, 9 } } },

            { "Pumping Chords", "Chords", {
                { pid::source, 1 }, { pid::spray, 0.08f }, { pid::size, 180 }, { pid::density, 40 },
                { pid::stereo, 0.8f }, { pid::lfoTarget, 6 }, { pid::lfoShape, 5 }, { pid::lfoMode, 1 },
                { pid::lfoDivision, 4 }, { pid::lfoDepth, 0.85f }, { pid::attack, 5 }, { pid::release, 400 },
                { pid::space, 0.3f }, { pid::output, 0 } } },

            { "Wobble Chords", "Chords", {
                { pid::source, 1 }, { pid::spray, 0.05f }, { pid::size, 200 }, { pid::density, 35 },
                { pid::lfoTarget, 5 }, { pid::lfoMode, 1 }, { pid::lfoDivision, 5 }, { pid::lfoDepth, 0.5f },
                { pid::cutoff, 1800 }, { pid::resonance, 0.35f }, { pid::drive, 0.08f }, { pid::space, 0.25f }, { pid::output, -2.5f } } },

            { "Chord Freeze", "Chords", {
                { pid::source, 7 }, { pid::voiceMode, 1 }, { pid::freeze, 1 }, { pid::position, 0.1f }, { pid::spray, 0.1f },
                { pid::size, 500 }, { pid::density, 20 }, { pid::chaos, 0.8f }, { pid::stereo, 1.0f },
                { pid::attack, 1200 }, { pid::release, 4000 }, { pid::space, 0.6f }, { pid::output, 4 } } },

            { "Tape Chords", "Chords", {
                { pid::source, 1 }, { pid::spray, 0.04f }, { pid::size, 240 }, { pid::density, 30 },
                { pid::lfoTarget, 4 }, { pid::lfoRate, 0.6f }, { pid::lfoDepth, 0.025f }, { pid::jitter, 0.08f },
                { pid::drive, 0.35f }, { pid::cutoff, 5000 }, { pid::release, 900 }, { pid::space, 0.3f }, { pid::output, -1.5f } } },

            //------------------------------------------------------------------ Keys
            { "Bell Garden", "Keys", {
                { pid::source, 3 }, { pid::position, 0.02f }, { pid::spray, 0.01f }, { pid::size, 250 },
                { pid::density, 30 }, { pid::chaos, 0.2f }, { pid::attack, 2 }, { pid::decay, 1500 },
                { pid::sustain, 0.3f }, { pid::release, 1500 }, { pid::stereo, 0.6f }, { pid::space, 0.35f }, { pid::output, 1.5f } } },

            { "Lofi Keys", "Keys", {
                { pid::source, 9 }, { pid::position, 0.03f }, { pid::spray, 0.02f }, { pid::size, 200 }, { pid::density, 30 },
                { pid::chaos, 0.3f }, { pid::cutoff, 4500 }, { pid::drive, 0.2f }, { pid::lfoTarget, 4 },
                { pid::lfoRate, 0.5f }, { pid::lfoDepth, 0.015f }, { pid::attack, 3 }, { pid::release, 800 },
                { pid::space, 0.3f }, { pid::output, 1 } } },

            { "Broken Keys", "Keys", {
                { pid::source, 9 }, { pid::scan, 0.5f }, { pid::jitter, 12 }, { pid::quantize, 1 },
                { pid::reverse, 0.25f }, { pid::spray, 0.1f }, { pid::size, 160 }, { pid::density, 25 },
                { pid::chaos, 0.6f }, { pid::stereo, 0.8f }, { pid::release, 1500 }, { pid::space, 0.4f }, { pid::output, -2 } } },

            { "Grain Pluck", "Keys", {
                { pid::source, 8 }, { pid::position, 0.0f }, { pid::spray, 0.004f }, { pid::size, 250 },
                { pid::density, 14 }, { pid::chaos, 0.1f }, { pid::window, 0.9f }, { pid::attack, 1 },
                { pid::decay, 1500 }, { pid::sustain, 0.3f }, { pid::release, 700 }, { pid::stereo, 0.4f },
                { pid::space, 0.35f }, { pid::output, 8 } } },

            { "Kalimba Dust", "Keys", {
                { pid::source, 8 }, { pid::spray, 0.25f }, { pid::size, 60 }, { pid::density, 60 },
                { pid::chaos, 1.0f }, { pid::jitter, 12 }, { pid::quantize, 3 }, { pid::stereo, 1.0f },
                { pid::attack, 2 }, { pid::decay, 1200 }, { pid::sustain, 0.35f }, { pid::release, 1200 },
                { pid::space, 0.5f }, { pid::output, 7 } } },

            //------------------------------------------------------------------ Leads
            { "Lead Glide", "Leads", {
                { pid::source, 2 }, { pid::voiceMode, 2 }, { pid::glide, 120 }, { pid::size, 70 },
                { pid::density, 80 }, { pid::chaos, 0.1f }, { pid::spray, 0.02f }, { pid::attack, 5 },
                { pid::sustain, 0.9f }, { pid::release, 300 }, { pid::stereo, 0.3f }, { pid::space, 0.2f }, { pid::output, 0.5f } } },

            { "Vox Lead", "Leads", {
                { pid::source, 6 }, { pid::voiceMode, 2 }, { pid::glide, 90 }, { pid::freeze, 1 },
                { pid::position, 0.0625f }, { pid::size, 80 }, { pid::density, 70 }, { pid::chaos, 0.1f },
                { pid::spray, 0.01f }, { pid::attack, 10 }, { pid::release, 400 }, { pid::space, 0.25f }, { pid::output, 6.5f } } },

            { "Grain Saw Lead", "Leads", {
                { pid::source, 1 }, { pid::voiceMode, 1 }, { pid::glide, 60 }, { pid::size, 60 },
                { pid::density, 90 }, { pid::chaos, 0.05f }, { pid::spray, 0.02f }, { pid::cutoff, 5000 },
                { pid::drive, 0.3f }, { pid::attack, 3 }, { pid::release, 250 }, { pid::space, 0.15f }, { pid::output, 3.5f } } },

            //------------------------------------------------------------------ Bass
            { "Grain Sub", "Bass", {
                { pid::source, 1 }, { pid::voiceMode, 1 }, { pid::pitch, -24 }, { pid::size, 150 },
                { pid::density, 60 }, { pid::chaos, 0.1f }, { pid::spray, 0.01f }, { pid::stereo, 0.1f },
                { pid::cutoff, 600 }, { pid::drive, 0.4f }, { pid::attack, 2 }, { pid::release, 200 }, { pid::output, 6.5f } } },

            { "Wobble Bass", "Bass", {
                { pid::source, 1 }, { pid::voiceMode, 1 }, { pid::pitch, -24 }, { pid::size, 120 },
                { pid::density, 70 }, { pid::chaos, 0.05f }, { pid::spray, 0.01f }, { pid::stereo, 0.1f },
                { pid::lfoTarget, 5 }, { pid::lfoMode, 1 }, { pid::lfoDivision, 5 }, { pid::lfoShape, 1 },
                { pid::lfoDepth, 0.6f }, { pid::cutoff, 700 }, { pid::resonance, 0.5f }, { pid::drive, 0.5f },
                { pid::attack, 2 }, { pid::release, 150 }, { pid::output, 6.5f } } },

            //------------------------------------------------------------------ Motion
            { "Tape Scrub", "Motion", {
                { pid::source, 1 }, { pid::scan, 0.5f }, { pid::spray, 0.01f }, { pid::size, 60 },
                { pid::density, 50 }, { pid::chaos, 0.1f }, { pid::stereo, 0.4f }, { pid::release, 600 },
                { pid::space, 0.2f }, { pid::output, -2 } } },

            { "Crushed Motion", "Motion", {
                { pid::source, 1 }, { pid::drive, 0.7f }, { pid::density, 90 }, { pid::size, 25 },
                { pid::chaos, 0.6f }, { pid::spray, 0.1f }, { pid::scan, -0.3f }, { pid::space, 0.2f }, { pid::output, -1 } } },

            { "Scanning Choir", "Motion", {
                { pid::source, 6 }, { pid::scan, 0.15f }, { pid::spray, 0.06f }, { pid::size, 250 },
                { pid::density, 25 }, { pid::chaos, 0.4f }, { pid::stereo, 0.8f }, { pid::attack, 400 },
                { pid::release, 2500 }, { pid::space, 0.5f }, { pid::output, -4.5f } } },

            { "Drifting Glass", "Motion", {
                { pid::source, 5 }, { pid::lfoTarget, 0 }, { pid::lfoRate, 0.05f }, { pid::lfoDepth, 0.8f },
                { pid::size, 300 }, { pid::density, 20 }, { pid::spray, 0.1f }, { pid::stereo, 1.0f },
                { pid::attack, 1000 }, { pid::release, 4000 }, { pid::space, 0.6f }, { pid::output, -1.5f } } },

            //------------------------------------------------------------------ Rhythmic
            { "Grain Pulse", "Rhythmic", {
                { pid::source, 1 }, { pid::sync, 1 }, { pid::syncRate, 3 }, { pid::window, 1.0f },
                { pid::size, 80 }, { pid::chaos, 0.0f }, { pid::spray, 0.02f }, { pid::filterEnv, 0.4f },
                { pid::filterDecay, 150 }, { pid::cutoff, 800 }, { pid::resonance, 0.5f }, { pid::space, 0.2f }, { pid::output, -4 } } },

            { "Stutter Voice", "Rhythmic", {
                { pid::source, 2 }, { pid::sync, 1 }, { pid::syncRate, 5 }, { pid::chaos, 0.0f },
                { pid::size, 40 }, { pid::window, 0.6f }, { pid::spray, 0.2f }, { pid::stereo, 0.6f },
                { pid::space, 0.2f }, { pid::output, -0 } } },

            { "Sidechain Pad", "Rhythmic", {
                { pid::source, 1 }, { pid::spray, 0.1f }, { pid::size, 300 }, { pid::density, 25 },
                { pid::chaos, 0.5f }, { pid::stereo, 0.9f }, { pid::lfoTarget, 6 }, { pid::lfoShape, 5 },
                { pid::lfoMode, 1 }, { pid::lfoDivision, 4 }, { pid::lfoDepth, 0.9f }, { pid::attack, 300 },
                { pid::release, 1500 }, { pid::space, 0.4f }, { pid::output, -0.5f } } },

            { "Gated Glass", "Rhythmic", {
                { pid::source, 5 }, { pid::spray, 0.2f }, { pid::size, 200 }, { pid::density, 30 },
                { pid::stereo, 0.8f }, { pid::lfoTarget, 6 }, { pid::lfoShape, 3 }, { pid::lfoMode, 1 },
                { pid::lfoDivision, 6 }, { pid::lfoDepth, 1.0f }, { pid::release, 800 }, { pid::space, 0.3f }, { pid::output, 1 } } },

            //------------------------------------------------------------------ FX
            { "Wind Tunnel", "FX", {
                { pid::source, 4 }, { pid::scan, 0.2f }, { pid::spray, 0.2f }, { pid::size, 200 },
                { pid::density, 40 }, { pid::filterType, 1 }, { pid::cutoff, 1500 }, { pid::resonance, 0.35f },
                { pid::lfoTarget, 5 }, { pid::lfoRate, 0.15f }, { pid::lfoDepth, 0.5f }, { pid::attack, 1000 },
                { pid::release, 3000 }, { pid::space, 0.4f }, { pid::output, 6 } } },

            { "Starfield", "FX", {
                { pid::source, 3 }, { pid::spray, 1.0f }, { pid::size, 40 }, { pid::density, 25 },
                { pid::chaos, 1.0f }, { pid::jitter, 12 }, { pid::stereo, 1.0f }, { pid::reverse, 0.5f },
                { pid::release, 4000 }, { pid::space, 0.6f }, { pid::output, -1 } } },

            { "Reverse Swell", "FX", {
                { pid::source, 1 }, { pid::reverse, 1.0f }, { pid::size, 500 }, { pid::density, 20 },
                { pid::spray, 0.2f }, { pid::stereo, 1.0f }, { pid::attack, 2500 }, { pid::release, 2000 },
                { pid::space, 0.7f }, { pid::output, -2 } } },

            { "Rising Noise", "FX", {
                { pid::source, 4 }, { pid::scan, 0.25f }, { pid::spray, 0.1f }, { pid::size, 150 },
                { pid::density, 60 }, { pid::lfoTarget, 5 }, { pid::lfoShape, 2 }, { pid::lfoMode, 1 },
                { pid::lfoDivision, 0 }, { pid::lfoDepth, 0.8f }, { pid::cutoff, 1200 }, { pid::resonance, 0.4f },
                { pid::attack, 200 }, { pid::release, 2000 }, { pid::space, 0.5f }, { pid::output, -2 } } },

            //------------------------------------------------------------------ More
            { "Neon Stab Chords", "Chords", {
                { pid::source, 1 }, { pid::position, 0.5f }, { pid::freeze, 1 }, { pid::spray, 0.01f },
                { pid::size, 180 }, { pid::density, 45 }, { pid::chaos, 0.6f }, { pid::window, 0.45f },
                { pid::jitter, 0.15f }, { pid::stereo, 0.35f }, { pid::attack, 1 }, { pid::decay, 380 },
                { pid::sustain, 0.4f }, { pid::release, 180 }, { pid::cutoff, 3200 }, { pid::resonance, 0.25f },
                { pid::filterEnv, 0.55f }, { pid::filterDecay, 220 }, { pid::space, 0.25f }, { pid::spaceSize, 0.5f },
                { pid::hq, 1 }, { pid::output, 5.5f } } },

            { "Sidechain Saw Wall", "Chords", {
                { pid::source, 1 }, { pid::position, 0.45f }, { pid::freeze, 1 }, { pid::spray, 0.02f },
                { pid::size, 90 }, { pid::density, 90 }, { pid::chaos, 0.3f }, { pid::window, 0.7f },
                { pid::jitter, 0.2f }, { pid::stereo, 0.5f }, { pid::attack, 3 }, { pid::decay, 1000 },
                { pid::sustain, 0.85f }, { pid::release, 350 }, { pid::cutoff, 7500 }, { pid::resonance, 0.1f },
                { pid::lfoTarget, 6 }, { pid::lfoShape, 5 }, { pid::lfoMode, 1 }, { pid::lfoDivision, 4 },
                { pid::lfoDepth, 0.7f }, { pid::space, 0.2f }, { pid::spaceSize, 0.55f }, { pid::output, -2.5f } } },

            { "Octave Halo", "Pads", {
                { pid::source, 5 }, { pid::position, 0.3f }, { pid::spray, 0.15f }, { pid::size, 700 },
                { pid::density, 16 }, { pid::chaos, 0.9f }, { pid::window, 0.3f }, { pid::quantize, 1 },
                { pid::jitter, 12 }, { pid::reverse, 0.25f }, { pid::stereo, 1.0f }, { pid::attack, 1800 },
                { pid::decay, 3000 }, { pid::sustain, 0.9f }, { pid::release, 6000 }, { pid::cutoff, 11000 },
                { pid::lfoTarget, 2 }, { pid::lfoRate, 0.07f }, { pid::lfoDepth, 0.3f }, { pid::space, 0.8f },
                { pid::spaceSize, 0.95f }, { pid::hq, 1 }, { pid::output, -8.5f } } },

            { "Ghost Garage Vox", "Vocal Chops", {
                { pid::source, 6 }, { pid::voiceMode, 2 }, { pid::glide, 70 }, { pid::position, 0.0625f },
                { pid::scan, 0.35f }, { pid::scanMode, 1 }, { pid::spray, 0.01f }, { pid::size, 140 },
                { pid::density, 22 }, { pid::chaos, 0.2f }, { pid::window, 0.55f }, { pid::reverse, 0.2f },
                { pid::stereo, 0.4f }, { pid::attack, 8 }, { pid::release, 700 }, { pid::filterType, 1 },
                { pid::cutoff, 1100 }, { pid::resonance, 0.05f }, { pid::space, 0.45f }, { pid::spaceSize, 0.7f }, { pid::output, 4.5f } } },

            { "Syllable Stutter", "Vocal Chops", {
                { pid::source, 6 }, { pid::voiceMode, 1 }, { pid::freeze, 1 }, { pid::position, 0.8125f },
                { pid::pitch, -12 }, { pid::sync, 1 }, { pid::syncRate, 3 }, { pid::chaos, 0.0f },
                { pid::spray, 0.004f }, { pid::size, 95 }, { pid::window, 0.75f }, { pid::lfoTarget, 0 },
                { pid::lfoShape, 4 }, { pid::lfoMode, 1 }, { pid::lfoDivision, 5 }, { pid::lfoDepth, 0.25f },
                { pid::attack, 1 }, { pid::sustain, 1.0f }, { pid::release, 120 }, { pid::cutoff, 9000 },
                { pid::stereo, 0.2f }, { pid::space, 0.2f }, { pid::output, -2.5f } } },

            { "Candy Bell Sparkle", "Keys", {
                { pid::source, 3 }, { pid::freeze, 1 }, { pid::position, 0.255f }, { pid::spray, 0.004f },
                { pid::size, 180 }, { pid::density, 45 }, { pid::chaos, 0.2f }, { pid::window, 0.9f },
                { pid::quantize, 1 }, { pid::jitter, 12 }, { pid::stereo, 0.6f }, { pid::attack, 1 },
                { pid::decay, 900 }, { pid::sustain, 0.15f }, { pid::release, 900 }, { pid::cutoff, 14000 },
                { pid::space, 0.4f }, { pid::spaceSize, 0.6f }, { pid::hq, 1 }, { pid::output, 4 } } },

            { "Dusty Tape Keys", "Keys", {
                { pid::source, 9 }, { pid::freeze, 1 }, { pid::position, 0.03f }, { pid::spray, 0.01f },
                { pid::size, 220 }, { pid::density, 30 }, { pid::chaos, 0.4f }, { pid::window, 0.6f },
                { pid::lfoTarget, 4 }, { pid::lfoRate, 0.45f }, { pid::lfoDepth, 0.02f }, { pid::cutoff, 3800 },
                { pid::resonance, 0.15f }, { pid::attack, 2 }, { pid::decay, 1400 }, { pid::sustain, 0.35f },
                { pid::release, 700 }, { pid::drive, 0.15f }, { pid::stereo, 0.5f }, { pid::space, 0.25f },
                { pid::spaceSize, 0.5f }, { pid::output, 4.5f } } },

            { "Vowel Snow", "Pads", {
                { pid::source, 2 }, { pid::freeze, 1 }, { pid::position, 0.12f }, { pid::spray, 0.03f },
                { pid::size, 450 }, { pid::density, 20 }, { pid::chaos, 0.9f }, { pid::jitter, 0.12f },
                { pid::window, 0.2f }, { pid::stereo, 0.9f }, { pid::attack, 1500 }, { pid::decay, 2000 },
                { pid::sustain, 0.9f }, { pid::release, 5000 }, { pid::cutoff, 6500 }, { pid::space, 0.6f },
                { pid::spaceSize, 0.8f }, { pid::output, -5 } } },

            { "Reverse Drop Riser", "FX", {
                { pid::source, 1 }, { pid::freeze, 1 }, { pid::position, 0.5f }, { pid::reverse, 1.0f },
                { pid::spray, 0.03f }, { pid::size, 900 }, { pid::density, 14 }, { pid::chaos, 0.6f },
                { pid::window, 0.1f }, { pid::stereo, 0.9f }, { pid::lfoTarget, 5 }, { pid::lfoShape, 2 },
                { pid::lfoMode, 1 }, { pid::lfoDivision, 0 }, { pid::lfoDepth, 0.6f }, { pid::cutoff, 1500 },
                { pid::resonance, 0.35f }, { pid::attack, 3000 }, { pid::release, 400 }, { pid::space, 0.5f },
                { pid::spaceSize, 0.7f }, { pid::output, -4.5f } } },

            { "Pentatonic Rain", "Keys", {
                { pid::source, 8 }, { pid::voices, 4 }, { pid::spray, 1.0f }, { pid::size, 70 },
                { pid::density, 24 }, { pid::chaos, 1.0f }, { pid::quantize, 3 }, { pid::jitter, 12 },
                { pid::window, 0.85f }, { pid::stereo, 1.0f }, { pid::attack, 1 }, { pid::decay, 2000 },
                { pid::sustain, 0.6f }, { pid::release, 2500 }, { pid::cutoff, 12000 }, { pid::space, 0.55f },
                { pid::spaceSize, 0.8f }, { pid::output, 2.5f } } },

            { "Hyper Vox Lead", "Leads", {
                { pid::source, 6 }, { pid::voiceMode, 2 }, { pid::glide, 45 }, { pid::freeze, 1 },
                { pid::position, 0.0625f }, { pid::pitch, 12 }, { pid::spray, 0.002f }, { pid::size, 45 },
                { pid::density, 140 }, { pid::chaos, 0.05f }, { pid::window, 0.6f }, { pid::jitter, 0.08f },
                { pid::stereo, 0.2f }, { pid::lfoTarget, 4 }, { pid::lfoRate, 5.5f }, { pid::lfoDepth, 0.02f },
                { pid::cutoff, 12000 }, { pid::attack, 4 }, { pid::release, 180 }, { pid::space, 0.2f }, { pid::hq, 1 }, { pid::output, 7 } } },
        };
        return list;
    }

    //==============================================================================
    PresetManager::PresetManager (GrainProcessor& p) : processor (p) {}

    juce::File PresetManager::userFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Presets/thf/thf Grain");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("thf/thf Grain/Presets");
       #endif
    }

    std::vector<PresetManager::Entry> PresetManager::list() const
    {
        std::vector<Entry> entries;
        const auto& factory = factoryPresets();
        for (size_t i = 0; i < factory.size(); ++i)
            entries.push_back ({ factory[i].name, factory[i].category, (int) i, {} });

        auto files = userFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
        for (auto& f : files)
            entries.push_back ({ f.getFileNameWithoutExtension(), "User", -1, f });
        return entries;
    }

    void PresetManager::resetToDefaults()
    {
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
    }

    PresetManager::SampleKeep PresetManager::captureLockedSample() const
    {
        SampleKeep keep;
        if (! processor.isSampleLocked())
            return keep;
        keep.active = true;
        for (auto* id : { pid::source, pid::regionStart, pid::regionEnd, pid::root, pid::fine, pid::normalize })
            keep.values.push_back ({ id, processor.param (id)->getValue() });
        for (int i = 0; i < 8; ++i)
            keep.cues[(size_t) i] = processor.getCue (i);
        return keep;
    }

    void PresetManager::restoreLockedSample (const SampleKeep& keep)
    {
        if (! keep.active)
            return;
        for (const auto& [id, value] : keep.values)
            processor.param (id)->setValueNotifyingHost (value);
        for (int i = 0; i < 8; ++i)
            processor.setCue (i, keep.cues[(size_t) i]);
    }

    void PresetManager::loadFactory (int index)
    {
        const auto& factory = factoryPresets();
        if (index < 0 || index >= (int) factory.size())
            return;
        const auto keep = captureLockedSample();
        const auto before = processor.captureSnapshot (true);
        processor.fadeForChange();
        resetToDefaults();
        for (const auto& [id, value] : factory[(size_t) index].values)
            if (auto* p = processor.param (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        for (int i = 0; i < 8; ++i)
            processor.setCue (i, -1.0f);
        restoreLockedSample (keep);
        processor.recordChange ("Preset", before);
        currentName = factory[(size_t) index].name;
        if (onChange) onChange();
    }

    bool PresetManager::loadFile (const juce::File& file)
    {
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("thfGrainPreset"))
            return false;

        const auto keep = captureLockedSample();
        const auto before = processor.captureSnapshot (true);
        processor.fadeForChange();
        resetToDefaults();
        for (auto* child : xml->getChildWithTagNameIterator ("PARAM"))
            if (auto* p = processor.param (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        if (auto* extra = xml->getChildByName ("Extra"))
        {
            // Keep the current sample if the preset does not carry one.
            auto tree = juce::ValueTree::fromXml (*extra);
            if (! tree.getChildWithName ("Sample").isValid())
            {
                if (auto current = processor.getUserSample())
                {
                    const auto currentSample = processor.saveExtraState (false).getChildWithName ("Sample");
                    if (currentSample.isValid())
                        tree.addChild (currentSample.createCopy(), -1, nullptr);
                }
            }
            const bool presetHasSample = tree.getChildWithName ("Sample").hasProperty ("path")
                                         || tree.getChildWithName ("Sample").hasProperty ("flac");
            processor.restoreExtraState (tree, false);
            if (! presetHasSample)
                restoreLockedSample (keep);
        }
        else
        {
            restoreLockedSample (keep);
        }
        processor.recordChange ("Preset", before);
        currentName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        if (onChange) onChange();
        return true;
    }

    void PresetManager::load (const Entry& e)
    {
        if (e.factoryIndex >= 0) loadFactory (e.factoryIndex);
        else                     loadFile (e.file);
    }

    bool PresetManager::saveUser (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty())
            return false;
        auto folder = userFolder();
        if (! folder.createDirectory())
            return false;

        juce::XmlElement xml ("thfGrainPreset");
        xml.setAttribute ("name", name.trim());
        xml.setAttribute ("version", GrainProcessor::stateVersion);
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", ranged->getParameterID());
                e->setAttribute ("value", (double) ranged->convertFrom0to1 (ranged->getValue()));
            }
        auto extra = processor.saveExtraState (false);
        if (auto sample = extra.getChildWithName ("Sample"); sample.isValid())
            if (const auto* block = sample.getProperty ("flac").getBinaryData())
                sample.setProperty ("flac", block->toBase64Encoding(), nullptr);
        if (auto extraXml = extra.createXml())
            xml.addChildElement (extraXml.release());

        const auto file = folder.getChildFile (clean + extension);
        if (! xml.writeTo (file))
            return false;
        currentName = name.trim();
        if (onChange) onChange();
        return true;
    }

    bool PresetManager::remove (const Entry& e)
    {
        return e.factoryIndex < 0 && e.file.existsAsFile() && e.file.deleteFile();
    }

    void PresetManager::step (int delta)
    {
        const auto entries = list();
        if (entries.empty())
            return;
        int current = -1;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == currentName) { current = (int) i; break; }
        const auto next = ((current < 0 ? 0 : current + delta) % (int) entries.size() + (int) entries.size())
                          % (int) entries.size();
        load (entries[(size_t) next]);
    }
}
