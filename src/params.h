// params.h — los knobs y botones de cada motor: rangos, nombres, secciones y cómo se aplican,
// más los sonidos de fábrica de los 8 huecos de presets. La pantalla se construye con esto.
// Índices: knobs 0..31, botones 32..39.
#pragma once
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "dx.h"
#include "gameboy.h"
#include "juno.h"
#include "moog.h"
#include "voice303.h"
#include "synths2.h"

namespace gb {

// fmt: p porcentaje, h hercios, s segundos, t semitonos, c cents, x ratio, i entero, n nombres, f decimal con unidad
struct PSpec { const char* label; float min, max, step, def; char fmt; bool log; const char* names; const char* grp; };
struct BSpec { const char* label; bool def; };
struct Preset { const char* name; std::vector<std::pair<int, float>> v; };
constexpr int kMaxP = 32, kBtn0 = 32;
struct EngSpec { const char* key; const char* name; const char* desc; int np; PSpec p[kMaxP]; int nb; BSpec b[8]; };

enum Eng { E_JUNO = 0, E_DX, E_MOOG, E_ACID, E_GB, E_WT, E_SAW, E_MODAL, E_COUNT };

inline const EngSpec kEng[E_COUNT] = {
    {"juno", "Juno", "Juno-6: saw and pulse DCO with PWM and sub, IR3109 filter, continuous HPF, one ADSR, LFO with delay, BBD chorus. Engine: KR-106 (measured from real Junos)", 22,
     {{"LFO rate", 0, 1, 0.01f, 0.45f, 'p', false, nullptr, "LFO"}, {"LFO delay", 0, 1, 0.01f, 0, 'p', false, nullptr, "LFO"},
      {"Range", -1, 1, 1, 0, 'n', false, "16'|8'|4'", "DCO"}, {"DCO LFO", 0, 1, 0.01f, 0, 'p', false, nullptr, "DCO"}, {"PWM", 0, 1, 0.01f, 0.4f, 'p', false, nullptr, "DCO"},
      {"PWM mode", 0, 2, 1, 1, 'n', false, "LFO|Manual|Env", "DCO"}, {"Sub", 0, 1, 0.01f, 0.4f, 'p', false, nullptr, "DCO"}, {"Noise", 0, 1, 0.01f, 0, 'p', false, nullptr, "DCO"},
      {"Drift", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "DCO"},
      {"HPF", 0, 1, 0.01f, 0, 'p', false, nullptr, "HPF"},
      {"Cutoff", 0, 1, 0.01f, 0.55f, 'p', false, nullptr, "VCF"}, {"Resonance", 0, 1, 0.01f, 0.15f, 'p', false, nullptr, "VCF"}, {"Env", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "VCF"},
      {"VCF LFO", 0, 1, 0.01f, 0, 'p', false, nullptr, "VCF"}, {"Kybd", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "VCF"},
      {"Attack", 0, 1, 0.01f, 0.05f, 'p', false, nullptr, "ENV"}, {"Decay", 0, 1, 0.01f, 0.45f, 'p', false, nullptr, "ENV"},
      {"Sustain", 0, 1, 0.01f, 0.6f, 'p', false, nullptr, "ENV"}, {"Release", 0, 1, 0.01f, 0.35f, 'p', false, nullptr, "ENV"},
      {"Chorus", 0, 3, 1, 1, 'n', false, "Off|I|II|I+II", "Output"}, {"Velocity", 0, 1, 0.01f, 0.7f, 'p', false, nullptr, "Output"}, {"Volume", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Output"}},
     4, {{"Saw", true}, {"Pulse", false}, {"Env invert", false}, {"VCA gate", false}}},
    {"dx", "DX7", "DX7: 6 operators, 32 algorithms (MSFA engine, the origin of Dexed). Plays the built-in sounds and any DX7 bank (.syx) in the dx7/ folder; the knobs reshape the loaded patch", 16,
     {{"Patch", 0, 1023, 1, 0, 'i', false, nullptr, "Sound"}, {"Brightness", -1, 1, 0.01f, 0, 'b', false, nullptr, "Sound"}, {"Mod decay", -1, 1, 0.01f, 0, 'b', false, nullptr, "Sound"},
      {"Feedback", -1, 1, 0.01f, 0, 'b', false, nullptr, "Sound"}, {"Detune", -1, 1, 0.01f, 0, 'b', false, nullptr, "Sound"}, {"Vibrato", -1, 1, 0.01f, 0, 'b', false, nullptr, "Sound"},
      {"Tone", 0, 1, 0.01f, 1, 'p', false, nullptr, "Sound"}, {"Volume", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Sound"},
      {"Attack", -1, 1, 0.01f, 0, 'b', false, nullptr, "Envelope"}, {"Decay", -1, 1, 0.01f, 0, 'b', false, nullptr, "Envelope"}, {"Sustain", -1, 1, 0.01f, 0, 'b', false, nullptr, "Envelope"},
      {"Release", -1, 1, 0.01f, 0, 'b', false, nullptr, "Envelope"}, {"Transpose", -24, 24, 1, 0, 't', false, nullptr, "Envelope"}, {"Vib rate", -1, 1, 0.01f, 0, 'b', false, nullptr, "Envelope"},
      {"Velocity", -1, 1, 0.01f, 0, 'b', false, nullptr, "Envelope"},
      {"Algorithm", 0, 32, 1, 0, 'n', false, "Patch|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32", "Envelope"}},
     0, {}},
    {"moog", "Moog", "Mono: 3 oscillators, overdriven mixer, 24 dB ladder at 2×, filter and amp envelopes, osc 3 modulation and glide", 29,
     {{"Octave", -2, 2, 1, 0, 'i', false, "oct", "Oscillators"}, {"Wave 1", 0, 4, 1, 1, 'n', false, "Tri|Saw|Square|Pulse|Narrow", "Oscillators"},
      {"Wave 2", 0, 4, 1, 1, 'n', false, "Tri|Saw|Square|Pulse|Narrow", "Oscillators"}, {"Osc 2 semi", -24, 24, 1, 0, 't', false, nullptr, "Oscillators"}, {"Osc 2 fine", -100, 100, 1, 7, 'c', false, nullptr, "Oscillators"},
      {"Wave 3", 0, 4, 1, 1, 'n', false, "Tri|Saw|Square|Pulse|Narrow", "Oscillators"}, {"Osc 3 semi", -24, 24, 1, -12, 't', false, nullptr, "Oscillators"}, {"Osc 3 fine", -100, 100, 1, -3, 'c', false, nullptr, "Oscillators"},
      {"Level 1", 0, 1, 0.01f, 1, 'p', false, nullptr, "Mixer"}, {"Level 2", 0, 1, 0.01f, 0.7f, 'p', false, nullptr, "Mixer"}, {"Level 3", 0, 1, 0.01f, 0.4f, 'p', false, nullptr, "Mixer"},
      {"Noise", 0, 1, 0.01f, 0, 'p', false, nullptr, "Mixer"}, {"Drive", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Mixer"},
      {"Cutoff", 30, 18000, 0, 420, 'h', true, nullptr, "Filter"}, {"Resonance", 0, 1, 0.01f, 0.45f, 'p', false, nullptr, "Filter"}, {"Contour", 0, 1, 0.01f, 0.55f, 'p', false, nullptr, "Filter"},
      {"Key track", 0, 3, 1, 2, 'n', false, "Off|1/3|2/3|Full", "Filter"},
      {"Attack", 0.001f, 3, 0, 0.003f, 's', true, nullptr, "Filter envelope"}, {"Decay", 0.01f, 6, 0, 0.35f, 's', true, nullptr, "Filter envelope"},
      {"Sustain", 0, 1, 0.01f, 0.2f, 'p', false, nullptr, "Filter envelope"}, {"Release", 0.01f, 8, 0, 0.25f, 's', true, nullptr, "Filter envelope"},
      {"Attack", 0.001f, 3, 0, 0.003f, 's', true, nullptr, "Amp envelope"}, {"Decay", 0.01f, 6, 0, 0.5f, 's', true, nullptr, "Amp envelope"},
      {"Sustain", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Amp envelope"}, {"Release", 0.01f, 8, 0, 0.2f, 's', true, nullptr, "Amp envelope"},
      {"Osc 3 mod", 0, 1, 0.01f, 0, 'p', false, nullptr, "Mod & output"}, {"Glide", 0.005f, 1, 0, 0.06f, 's', true, nullptr, "Mod & output"},
      {"Velocity", 0, 1, 0.01f, 0.6f, 'p', false, nullptr, "Mod & output"}, {"Volume", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Mod & output"}},
     5, {{"Glide", true}, {"Osc 3 as LFO", false}, {"Mod → filter", false}, {"Mod → pitch", false}, {"Retrigger", false}}},
    {"acid", "303", "TB-303 style mono with Devilfish extras. Accent at velocity 100+, slide with overlapping notes", 16,
     {{"Cutoff", 0, 1, 0.01f, 0.35f, 'p', false, nullptr, "303"}, {"Resonance", 0, 1, 0.01f, 0.65f, 'p', false, nullptr, "303"}, {"Env mod", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "303"},
      {"Decay", 0, 1, 0.01f, 0.35f, 'p', false, nullptr, "303"}, {"Accent", 0, 1, 0.01f, 0.6f, 'p', false, nullptr, "303"}, {"Wave", 0, 1, 0.01f, 0, 'p', false, nullptr, "303"},
      {"Drive", 0, 1, 0.01f, 0.25f, 'p', false, nullptr, "303"}, {"Volume", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "303"},
      {"Tune", -36, 36, 0, 0, 't', false, nullptr, "Devilfish extras"}, {"Slide", 0.005f, 0.3f, 0, 0.045f, 's', true, nullptr, "Devilfish extras"},
      {"Accent decay", 0.02f, 0.6f, 0, 0.18f, 's', true, nullptr, "Devilfish extras"}, {"Amp decay", 0.05f, 4, 0, 2.5f, 's', true, nullptr, "Devilfish extras"},
      {"Soft attack", 0, 1, 0.01f, 0, 'p', false, nullptr, "Devilfish extras"}, {"Sub", 0, 1, 0.01f, 0, 'p', false, nullptr, "Devilfish extras"}, {"Drive tone", 500, 12000, 0, 8000, 'h', true, nullptr, "Devilfish extras"},
      {"Velocity", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Devilfish extras"}},
     0, {}},
    {"gb", "Game Boy", "The Game Boy chip: pulse with duty and sweep, 4-bit wave and noise, LSDJ-style arpeggio", 16,
     {{"Channel", 0, 2, 1, 0, 'n', false, "Pulse|Wave|Noise", "Channel"}, {"Duty", 0, 3, 1, 2, 'n', false, "12,5 %|25 %|50 %|75 %", "Channel"}, {"Wavetable", 0, 7, 1, 0, 'n', false, "Sine|Tri|Saw|Square|Bass|Organ|Voice|Noisy", "Channel"},
      {"Init vol", 1, 15, 1, 12, 'i', false, nullptr, "Volume"}, {"Envelope", 0, 7, 1, 3, 'n', false, "Fixed|1/64 s|2/64 s|3/64 s|4/64 s|5/64 s|6/64 s|7/64 s", "Volume"},
      {"Sweep time", 0, 7, 1, 0, 'n', false, "Off|1|2|3|4|5|6|7", "Sweep & pan"}, {"Sweep amt", 0, 7, 1, 2, 'i', false, nullptr, "Sweep & pan"}, {"Pan", 0, 3, 1, 1, 'n', false, "Left|Center|Right|Alternate", "Sweep & pan"},
      {"Vib rate", 0, 1, 0.01f, 0.4f, 'p', false, nullptr, "Vibrato & arp"}, {"Vib depth", 0, 1, 0.01f, 0, 'p', false, nullptr, "Vibrato & arp"},
      {"Arpeggio", 0, 7, 1, 0, 'n', false, "Off|Minor|Major|Fifth|Octave|Sus4|Dim|Power", "Vibrato & arp"}, {"Arp speed", 1, 8, 1, 2, 'i', false, "tick", "Vibrato & arp"},
      {"Fine", -50, 50, 1, 0, 'c', false, nullptr, "Output"}, {"Glide", 0, 0.3f, 0.005f, 0, 's', false, nullptr, "Output"}, {"DMG filter", 1000, 20000, 0, 9000, 'h', true, nullptr, "Output"}, {"Level", 0, 1, 0.01f, 0.7f, 'p', false, nullptr, "Output"}},
     7, {{"Env up", false}, {"7-bit noise", false}, {"Sweep up", false}, {"Chord → arp", true}, {"Oct −", false}, {"Oct +", false}, {"Delayed vib", false}}},
    {"wt", "Wavetable", "Wavetable synth: 6 tables with 16 frames each, position modulated by envelope and LFO, unison and a resonant filter", 22,
     {{"Table", 0, 5, 1, 0, 'n', false, "Basic|PWM|Vowels|Digital|Sync|Harmonics", "Oscillator"}, {"Position", 0, 1, 0.01f, 0.2f, 'p', false, nullptr, "Oscillator"},
      {"Env→pos", -1, 1, 0.01f, 0.3f, 'b', false, nullptr, "Oscillator"}, {"LFO→pos", 0, 1, 0.01f, 0, 'p', false, nullptr, "Oscillator"}, {"LFO rate", 0.05f, 12, 0, 0.5f, 'f', true, "Hz", "Oscillator"},
      {"Unison", 1, 3, 1, 1, 'i', false, nullptr, "Oscillator"}, {"Detune", 0, 1, 0.01f, 0.15f, 'p', false, nullptr, "Oscillator"}, {"Sub", 0, 1, 0.01f, 0, 'p', false, nullptr, "Oscillator"},
      {"Cutoff", 30, 18000, 0, 6000, 'h', true, nullptr, "Filter"}, {"Resonance", 0, 1, 0.01f, 0.15f, 'p', false, nullptr, "Filter"}, {"Env→filter", -1, 1, 0.01f, 0.3f, 'b', false, nullptr, "Filter"}, {"Key track", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "Filter"},
      {"Attack", 0.001f, 4, 0, 0.01f, 's', true, nullptr, "Mod envelope"}, {"Decay", 0.01f, 8, 0, 0.6f, 's', true, nullptr, "Mod envelope"}, {"Sustain", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Mod envelope"}, {"Release", 0.01f, 8, 0, 0.5f, 's', true, nullptr, "Mod envelope"},
      {"Attack", 0.001f, 4, 0, 0.005f, 's', true, nullptr, "Amp envelope"}, {"Decay", 0.01f, 8, 0, 0.5f, 's', true, nullptr, "Amp envelope"}, {"Sustain", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Amp envelope"}, {"Release", 0.01f, 8, 0, 0.4f, 's', true, nullptr, "Amp envelope"},
      {"Velocity", 0, 1, 0.01f, 0.7f, 'p', false, nullptr, "Output"}, {"Volume", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Output"}},
     0, {}},
    {"saw", "Supersaw", "Supersaw (7 detuned saws) or string machine (divide-down saws with ensemble), with a resonant filter", 21,
     {{"Mode", 0, 1, 1, 0, 'n', false, "Supersaw|Strings", "Oscillators"}, {"Detune", 0, 1, 0.01f, 0.35f, 'p', false, nullptr, "Oscillators"}, {"Mix", 0, 1, 0.01f, 0.6f, 'p', false, nullptr, "Oscillators"},
      {"Octave up", 0, 1, 0.01f, 0, 'p', false, nullptr, "Oscillators"},
      {"Cutoff", 30, 18000, 0, 9000, 'h', true, nullptr, "Filter"}, {"Resonance", 0, 1, 0.01f, 0.1f, 'p', false, nullptr, "Filter"}, {"High pass", 20, 1000, 0, 60, 'h', true, nullptr, "Filter"},
      {"Env→filter", -1, 1, 0.01f, 0.2f, 'b', false, nullptr, "Filter"}, {"Key track", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Filter"},
      {"Ensemble", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "Ensemble"}, {"Ens. rate", 0, 1, 0.01f, 0.4f, 'p', false, nullptr, "Ensemble"},
      {"Attack", 0.001f, 4, 0, 0.01f, 's', true, nullptr, "Filter envelope"}, {"Decay", 0.01f, 8, 0, 0.8f, 's', true, nullptr, "Filter envelope"}, {"Sustain", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "Filter envelope"}, {"Release", 0.01f, 8, 0, 0.6f, 's', true, nullptr, "Filter envelope"},
      {"Attack", 0.001f, 4, 0, 0.01f, 's', true, nullptr, "Amp envelope"}, {"Decay", 0.01f, 8, 0, 0.5f, 's', true, nullptr, "Amp envelope"}, {"Sustain", 0, 1, 0.01f, 0.85f, 'p', false, nullptr, "Amp envelope"}, {"Release", 0.01f, 8, 0, 0.5f, 's', true, nullptr, "Amp envelope"},
      {"Velocity", 0, 1, 0.01f, 0.6f, 'p', false, nullptr, "Output"}, {"Volume", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Output"}},
     0, {}},
    {"modal", "Modal", "Physical modelling: plucked string or a bank of resonators (bar, bell, plate) struck, plucked or bowed", 12,
     {{"Model", 0, 3, 1, 0, 'n', false, "String|Bar|Bell|Plate", "Resonator"}, {"Exciter", 0, 3, 1, 0, 'n', false, "Pluck|Mallet|Bow|Noise", "Resonator"},
      {"Brightness", 0, 1, 0.01f, 0.6f, 'p', false, nullptr, "Resonator"}, {"Damping", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "Resonator"}, {"Position", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Resonator"},
      {"Structure", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Resonator"},
      {"Exciter level", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Exciter"}, {"Noise", 0, 1, 0.01f, 0.1f, 'p', false, nullptr, "Exciter"},
      {"Release", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Output"}, {"Spread", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "Output"}, {"Velocity", 0, 1, 0.01f, 0.7f, 'p', false, nullptr, "Output"}, {"Volume", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Output"}},
     0, {}},
};

// Sonidos de fábrica: cambios sobre los valores por defecto. Huecos sin nombre = vacíos.
// índice del parámetro Volume de cada motor (Game Boy no tiene)
inline const int kEngVol[E_COUNT] = {21, 7, 28, 7, -1, 21, 20, 11};
// Cuánto bajó el nivel base de cada motor (dB) al pasar a la mezcla de Jorge como punto de partida (7-11-2026)
inline const float kEngShift14[E_COUNT] = {-12.9f, 1.7f, -6.2f, -6.0f, -6.0f, -6.0f, -6.0f, -6.0f};
inline const std::vector<Preset>& factoryPresets(int e) {
    static const std::vector<Preset> P[E_COUNT] = {
        {   // Juno (deslizadores 0..1 como en el Juno-6)
            {"Pad", {{0, 0.35f}, {4, 0.55f}, {5, 0}, {6, 0.3f}, {10, 0.45f}, {11, 0.15f}, {12, 0.25f}, {15, 0.55f}, {16, 0.6f}, {17, 0.8f}, {18, 0.6f}, {19, 2}, {33, 1}}},
            {"Strings", {{9, 0.3f}, {10, 0.6f}, {11, 0.1f}, {12, 0.1f}, {15, 0.45f}, {16, 0.5f}, {17, 0.85f}, {18, 0.55f}, {19, 2}}},
            {"Brass", {{10, 0.35f}, {12, 0.55f}, {15, 0.22f}, {16, 0.45f}, {17, 0.55f}, {18, 0.3f}, {19, 1}}},
            {"Bass", {{2, -1}, {6, 0.6f}, {10, 0.3f}, {11, 0.3f}, {12, 0.5f}, {15, 0}, {16, 0.35f}, {17, 0.2f}, {18, 0.2f}, {19, 0}}},
            {"Pluck", {{10, 0.35f}, {11, 0.35f}, {12, 0.6f}, {15, 0}, {16, 0.3f}, {17, 0}, {18, 0.3f}, {19, 1}}},
            {"Organ", {{32, 0}, {33, 1}, {4, 0}, {6, 0.5f}, {10, 0.8f}, {12, 0}, {15, 0}, {17, 1}, {18, 0.1f}, {35, 1}, {19, 3}}},
            {"", {}}, {"", {}}},
        {   // DX7: cada preset elige un sonido de la biblioteca y ajusta los knobs
            {"E.Piano", {{0, 0}}}, {"FM Bass", {{0, 1}}}, {"Bells", {{0, 2}}}, {"Brass", {{0, 3}}},
            {"Marimba", {{0, 4}}}, {"Lead", {{0, 5}, {5, 0.2f}}}, {"String pad", {{0, 6}, {4, 0.3f}}}, {"Init", {{0, 7}}}},
        {   // Moog
            {"Bass", {{1, 1}, {2, 1}, {4, 6}, {5, 2}, {6, -12}, {10, 0.5f}, {12, 0.45f}, {13, 260}, {14, 0.35f}, {15, 0.6f}, {18, 0.25f}, {19, 0.1f}, {21, 0.002f}, {22, 0.35f}, {23, 0.6f}, {24, 0.1f}, {26, 0.035f}}},
            {"Lead", {{4, 8}, {6, -12}, {10, 0.35f}, {12, 0.5f}, {13, 1000}, {14, 0.5f}, {15, 0.45f}, {17, 0.01f}, {18, 0.8f}, {19, 0.45f}, {21, 0.01f}, {22, 1}, {23, 0.9f}, {24, 0.3f}, {26, 0.07f}}},
            {"Sub", {{0, -1}, {1, 0}, {9, 0}, {10, 0}, {12, 0.2f}, {13, 300}, {14, 0}, {15, 0.2f}, {21, 0.002f}, {22, 1}, {23, 1}, {24, 0.15f}}},
            {"Wobble", {{33, 1}, {6, 0}, {25, 0.6f}, {34, 1}, {13, 500}, {14, 0.6f}, {12, 0.5f}}},
            {"Brass", {{13, 500}, {14, 0.2f}, {15, 0.7f}, {17, 0.08f}, {18, 0.6f}, {19, 0.5f}, {21, 0.05f}, {22, 0.5f}, {23, 0.8f}, {24, 0.25f}}},
            {"", {}}, {"", {}}, {"", {}}},
        {   // 303
            {"Classic acid", {}},
            {"Devilfish", {{13, 0.4f}, {1, 0.85f}, {6, 0.6f}, {11, 1}, {9, 0.08f}, {10, 0.1f}, {12, 0.2f}}},
            {"", {}}, {"", {}}, {"", {}}, {"", {}}, {"", {}}, {"", {}}},
        {   // Game Boy
            {"Lead", {{8, 0.3f}, {9, 0.3f}, {38, 1}}},
            {"Minor arp", {{10, 1}, {11, 2}, {4, 0}}},
            {"Wave bass", {{0, 1}, {2, 4}, {4, 0}, {36, 1}}},
            {"Noise", {{0, 2}, {4, 2}}},
            {"", {}}, {"", {}}, {"", {}}, {"", {}}},
        {   // Wavetable
            {"Glass pad", {{0, 2}, {1, 0.1f}, {2, 0.5f}, {3, 0.2f}, {4, 0.2f}, {5, 3}, {6, 0.2f}, {8, 4000}, {12, 1.5f}, {13, 3}, {16, 0.8f}, {19, 2}}},
            {"Digital bass", {{0, 3}, {1, 0.4f}, {2, -0.4f}, {7, 0.5f}, {8, 900}, {9, 0.35f}, {10, 0.5f}, {13, 0.25f}, {14, 0}, {16, 0.002f}, {18, 0.7f}, {19, 0.1f}}},
            {"Sync lead", {{0, 4}, {1, 0}, {2, 0.8f}, {5, 2}, {6, 0.1f}, {8, 8000}, {13, 1.2f}, {14, 0.2f}, {19, 0.3f}}},
            {"Vowel keys", {{0, 2}, {1, 0}, {2, 1}, {12, 0.3f}, {13, 0.8f}, {14, 0.4f}, {17, 1.2f}, {18, 0.3f}}},
            {"PWM strings", {{0, 1}, {3, 0.4f}, {4, 0.3f}, {5, 3}, {6, 0.3f}, {8, 5000}, {16, 0.6f}, {19, 1.5f}}},
            {"", {}}, {"", {}}, {"", {}}},
        {   // Supersaw
            {"Trance lead", {{1, 0.45f}, {2, 0.7f}, {4, 7000}, {9, 0.2f}, {15, 0.005f}, {18, 0.3f}}},
            {"Hoover-ish", {{1, 0.9f}, {2, 0.9f}, {3, 0.3f}, {4, 2500}, {7, 0.6f}, {12, 0.6f}, {13, 0}}},
            {"Solina strings", {{0, 1}, {3, 0.5f}, {4, 5000}, {9, 0.8f}, {10, 0.35f}, {15, 0.4f}, {18, 1.2f}}},
            {"Dark strings", {{0, 1}, {3, 0.2f}, {4, 1500}, {9, 0.7f}, {15, 0.8f}, {18, 2}}},
            {"", {}}, {"", {}}, {"", {}}, {"", {}}},
        {   // Modal
            {"Plucked string", {}},
            {"Marimba", {{0, 1}, {1, 1}, {2, 0.4f}, {3, 0.35f}, {4, 0.25f}}},
            {"Bell", {{0, 2}, {1, 1}, {2, 0.7f}, {3, 0.85f}, {4, 0.2f}}},
            {"Bowed plate", {{0, 3}, {1, 2}, {2, 0.5f}, {3, 0.8f}, {8, 0.6f}}},
            {"Kalimba", {{0, 1}, {1, 0}, {2, 0.7f}, {3, 0.5f}, {5, 0.6f}, {4, 0.12f}}},
            {"", {}}, {"", {}}, {"", {}}},
    };
    // Igualación medida con --niveles (objetivo −22 dB, ponderación K): el Volume de cada preset sube o baja lo que le falta.
    // Volume actúa al cuadrado. No pasa de 1, así que los sonidos muy cortos pueden quedar algo por debajo.
    const auto& kVolIdx = kEngVol;
    static const float kPreDb[E_COUNT][8] = {{-10.3f, 0.7f, -0.7f, 8, 8, -9.2f}, {-1.4f, 9, -2.1f, -2.4f, 7.1f, 3, -5.3f, 1.3f}, {3.0f, -0.6f, 6.7f, -0.5f, -0.3f}, {}, {},
                                             {1.5f, -2.2f, 0, 4.5f, -2.6f}, {1.4f, 1.1f, -2.5f, -1.4f}, {8.6f, -2, -1.9f, 2.3f, 0}};
    static const std::vector<std::vector<Preset>> T = [] {
        std::vector<std::vector<Preset>> r(P, P + E_COUNT);
        for (int en = 0; en < E_COUNT; ++en) {
            const int vi = kVolIdx[en]; if (vi < 0) continue;
            for (size_t s = 0; s < r[static_cast<size_t>(en)].size() && s < 8; ++s) {
                Preset& pr = r[static_cast<size_t>(en)][s]; if (!pr.name[0] || kPreDb[en][s] == 0) continue;
                float* val = nullptr; for (auto& kv : pr.v) if (kv.first == vi) val = &kv.second;
                const float cur = val ? *val : kEng[en].p[vi].def, nv = std::clamp(cur * std::pow(10.0f, kPreDb[en][s] / 40.0f), 0.05f, 1.0f);
                if (val) *val = nv; else pr.v.push_back({vi, nv});
            }
        }
        return r;
    }();
    return T[static_cast<size_t>(e)];
}

// Capa principal de 16 knobs, igual en todos los motores (dos filas de 8, como en la controladora):
//   arriba: Cutoff, Resonance, Env amount, A, B, C, D, Volume · abajo: ADSR de filtro y ADSR de amplitud.
// A–D son los cuatro parámetros continuos más característicos de cada motor. -1 = ese motor no tiene equivalente.
inline const int kLayer[E_COUNT][16] = {
    /* Juno      */ {10, 11, 12,   4,  6, 13,  0, 21,   15, 16, 17, 18,    9,  7,  3,  1},   // A PWM, B sub, C VCF LFO, D velocidad del LFO; abajo: ADSR (la única del Juno), HPF, ruido, DCO LFO, retardo del LFO
    /* DX7       */ { 1,  2,  3,   4,  5,  0,  6,  7,    8,  9, 10, 11,   12, 13, 14, 15},   // «filtro» = brillo (nivel de los moduladores), «reso» = su caída, «env» = feedback; A desafinación, B vibrato, C sonido, D tono
    /* Moog      */ {13, 14, 15,   4, 10, 12, 26, 28,   17, 18, 19, 20,   21, 22, 23, 24},   // A fino osc 2, B nivel osc 3, C drive, D glide
    /* 303       */ { 0,  1,  2,   4,  6,  8, 13,  7,   12,  3,  5, 10,   14, 11, 15,  9},   // A accent, B drive, C afinación, D sub; abajo: ataque suave, decay, onda, decay del accent, tono del drive, decay de amp, velocidad, slide
    /* Game Boy  */ {14,  1,  2,   9,  8, 13, 12, 15,    3,  4,  5,  6,   10, 11,  7,  0},   // todo en la página 1: «filtro» = DMG, duty y tabla; A prof. vibrato, B vel. vibrato, C glide, D fino; abajo: volumen, envolvente, sweep, arpegio, pan y canal
    /* Wavetable */ { 8,  9, 10,   1,  2,  3,  6, 21,   12, 13, 14, 15,   16, 17, 18, 19},   // A posición, B env→posición, C LFO→posición, D detune
    /* Supersaw  */ { 4,  5,  7,   1,  2,  9,  6, 20,   11, 12, 13, 14,   15, 16, 17, 18},   // A detune, B mezcla, C ensemble, D paso alto
    /* Modal     */ { 2,  5,  6,   4,  3,  7,  9, 11,    0,  1,  8, 10,   -1, -1, -1, -1},   // todo en la página 1 (solo tiene 12): «filtro» = brillo, «reso» = estructura, «env» = excitador; A posición, B amortiguación, C ruido, D apertura; abajo: modelo, excitador, release, velocidad
};
// Segunda página: el resto de parámetros del motor, en su orden
inline std::vector<int> page2(int e) {
    std::vector<int> v;
    for (int i = 0; i < kEng[e].np; ++i) { bool inL = false; for (int k = 0; k < 16; ++k) inL |= kLayer[e][k] == i; if (!inL) v.push_back(i); }
    return v;
}

// Valor real a partir de un CC (0..127)
inline float fromCC(const PSpec& s, int v) {
    const float x = v / 127.0f;
    float y = s.log ? s.min * std::pow(s.max / s.min, x) : s.min + (s.max - s.min) * x;
    if (s.step > 0) y = std::round(y / s.step) * s.step;
    return std::clamp(y, s.min, s.max);
}

// Juego completo de motores de una pista (solo suena el elegido)
struct MeloEngines {
    JunoSynth juno; DxSynth dx; MoogVoice moog; acid::Voice303 acid; GameBoy gbs;
    WavetableSynth wt; SuperSaw saw; ModalSynth modal;;
    int cur = E_JUNO;
    void prepare(float sr) { sr_ = sr; juno.prepare(sr); dx.prepare(sr); moog.prepare(sr); acid.prepare(sr); gbs.prepare(sr); wt.prepare(sr); saw.prepare(sr); modal.prepare(sr); }
    void noteOn(int n, int v) { switch (cur) { case E_JUNO: juno.noteOn(n, v); break; case E_DX: dx.noteOn(n, v); break; case E_MOOG: moog.noteOn(n, v); break; case E_ACID: acid.noteOn(n, v); break; case E_GB: gbs.noteOn(n, v); break; case E_WT: wt.noteOn(n, v); break; case E_SAW: saw.noteOn(n, v); break; case E_MODAL: modal.noteOn(n, v); break; default: modal.noteOn(n, v); } }
    void noteOff(int n) { juno.noteOff(n); dx.noteOff(n); moog.noteOff(n); acid.noteOff(n); gbs.noteOff(n); wt.noteOff(n); saw.noteOff(n); modal.noteOff(n); }
    void allOff() { juno.allNotesOff(); dx.allNotesOff(); moog.allNotesOff(); acid.allNotesOff(); gbs.allNotesOff(); wt.allNotesOff(); saw.allNotesOff(); modal.allNotesOff(); }
    // Cambio de motor: el anterior se apaga en 10 ms y sigue procesándose en silencio unos segundos
    // para que sus notas terminen; así no se quedan colgadas ni «resucitan» al volver a él.
    void select(int e) {
        e = std::clamp(e, 0, E_COUNT - 1); if (e == cur) return;
        allOff(); prev_ = cur; prevG_ = 1.0f; prevN_ = static_cast<int>(sr_ * 4.0f); cur = e;
    }
    // ajuste de nivel por motor (medido): todos suenan parecido al cambiar de uno a otro con los mismos ajustes
    static float trim(int e) { static const float kDb[E_COUNT] = {-1.9f, -0.6f, -11.7f, -8.1f, -3.8f, -4.2f, -6.6f, -13.6f}; return std::exp2(kDb[std::clamp(e, 0, E_COUNT - 1)] / 6.0206f); }
    void render(float* L, float* R, float* mono, int n) {
        renderEng(cur, L, R, mono, n);
        { const float g = trim(cur); for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; } }
        if (prev_ >= 0 && prev_ != cur) {
            float pl[256], pr[256], pm[256];
            for (int o = 0; o < n; o += 256) {
                const int k = std::min(256, n - o);
                renderEng(prev_, pl, pr, pm, k);
                const float step = 1.0f / (0.01f * sr_);
                const float tg = trim(prev_);
                for (int i = 0; i < k; ++i) { if (prevG_ > 0) { L[o + i] += pl[i] * prevG_ * tg; R[o + i] += pr[i] * prevG_ * tg; prevG_ = std::max(0.0f, prevG_ - step); } }
            }
            prevN_ -= n; if (prevN_ <= 0) prev_ = -1;
        } else prev_ = -1;
    }
    void renderEng(int e, float* L, float* R, float* mono, int n) {
        switch (e) {
            case E_JUNO: juno.render(L, R, n); break;
            case E_DX: dx.render(L, R, n); break;
            case E_MOOG: moog.render(mono, n); for (int i = 0; i < n; ++i) L[i] = R[i] = mono[i]; break;
            case E_ACID: acid.render(mono, n); for (int i = 0; i < n; ++i) L[i] = R[i] = mono[i] * 0.8f; break;
            case E_GB: gbs.render(L, R, n); break;
            case E_WT: wt.render(L, R, n); break;
            case E_SAW: saw.render(L, R, n); break;
            case E_MODAL: modal.render(L, R, n); break;
            default: modal.render(L, R, n); break;
        }
    }
    int prev_ = -1, prevN_ = 0; float prevG_ = 0, sr_ = 48000;
    void set(int e, int i, float v) {
        const bool b = v >= 0.5f; const int iv = static_cast<int>(std::lround(v));
        switch (e) {
        case E_JUNO: { auto& p = juno.p;
            switch (i) { case 0: p.lfoRate = v; break; case 1: p.lfoDelay = v; break; case 2: p.range = iv; break; case 3: p.dcoLfo = v; break; case 4: p.pwm = v; break; case 5: p.pwmMode = iv; break;
                case 6: p.sub = v; break; case 7: p.noise = v; break; case 8: p.drift = v; break; case 9: p.hpf = v; break; case 10: p.cutoff = v; break; case 11: p.res = v; break;
                case 12: p.env = v; break; case 13: p.vcfLfo = v; break; case 14: p.kbd = v; break; case 15: p.A = v; break; case 16: p.D = v; break; case 17: p.S = v; break; case 18: p.R = v; break;
                case 19: p.chorus = iv; break; case 20: p.vel = v; break; case 21: p.volume = v; break;
                case 32: p.saw = b; break; case 33: p.pulse = b; break; case 34: p.inv = b; break; case 35: p.gate = b; break; }
            break; }
        case E_DX: { auto& p = dx.p;
            const float c = (v + 1.0f) * 0.5f;   // knobs bipolares: centro = el patch tal cual
            switch (i) { case 0: p.patch = iv; break; case 1: p.m.bright = c; break; case 2: p.m.modDecay = c; break; case 3: p.m.feedback = c; break; case 4: p.m.detune = c; break;
                case 5: p.m.vibrato = c; break; case 6: p.tone = v; break; case 7: p.volume = v; break; case 8: p.m.attack = c; break; case 9: p.m.decay = c; break; case 10: p.m.sustain = c; break;
                case 11: p.m.release = c; break; case 12: p.m.transpose = iv; break; case 13: p.m.vibRate = c; break; case 14: p.m.velocity = c; break; case 15: p.m.algo = iv; break; }
            break; }
        case E_MOOG: { auto& p = moog.p;
            switch (i) { case 0: p.oct = iv; break; case 1: p.wave1 = iv; break; case 2: p.wave2 = iv; break; case 3: p.semi2 = v; break; case 4: p.fine2 = v; break; case 5: p.wave3 = iv; break;
                case 6: p.semi3 = v; break; case 7: p.fine3 = v; break; case 8: p.lvl1 = v; break; case 9: p.lvl2 = v; break; case 10: p.lvl3 = v; break; case 11: p.noise = v; break; case 12: p.drive = v; break;
                case 13: p.cutoff = v; break; case 14: p.emph = v; break; case 15: p.contour = v; break; case 16: p.kbd = iv; break;
                case 17: p.fA = v; break; case 18: p.fD = v; break; case 19: p.fS = v; break; case 20: p.fR = v; break; case 21: p.A = v; break; case 22: p.D = v; break; case 23: p.S = v; break; case 24: p.R = v; break;
                case 25: p.mod = v; break; case 26: p.glide = v; break; case 27: p.vel = v; break; case 28: p.volume = v; break;
                case 32: p.glideOn = b; break; case 33: p.o3lfo = b; break; case 34: p.modFilt = b; break; case 35: p.modPitch = b; break; case 36: p.retrig = b; break; }
            break; }
        case E_ACID: { auto& p = acid.p;
            switch (i) { case 0: p.cutoff = v; break; case 1: p.resonance = v; break; case 2: p.envmod = v; break; case 3: p.decay = v; break; case 4: p.accent = v; break; case 5: p.wave = v; break;
                case 6: p.drive = v; break; case 7: p.volume = v; break; case 8: p.tune = v; break; case 9: p.slide = v; break; case 10: p.accDecay = v; break; case 11: p.ampDecay = v; break;
                case 12: p.softAtk = v; break; case 13: p.sub = v; break; case 14: p.dtone = v; break; case 15: p.vel = v; break; }
            break; }
        case E_GB: { auto& p = gbs.p;
            switch (i) { case 0: p.chan = iv; break; case 1: p.duty = iv; break; case 2: p.wtab = iv; break; case 3: p.vol = iv; break; case 4: p.envStep = iv; break; case 5: p.sweepT = iv; break;
                case 6: p.sweepN = iv; break; case 7: p.pan = iv; break; case 8: p.vibRate = v; break; case 9: p.vibDepth = v; break; case 10: p.arp = iv; break; case 11: p.arpSpd = iv; break;
                case 12: p.fine = v; break; case 13: p.glide = v; break; case 14: p.dmg = v; break; case 15: p.lvl = v; break;
                case 32: p.envUp = b; break; case 33: p.n7 = b; break; case 34: p.swUp = b; break; case 35: p.carp = b; break; case 36: p.octD = b; break; case 37: p.octU = b; break; case 38: p.vibD = b; break; }
            break; }
        case E_WT: { auto& p = wt.p;
            switch (i) { case 0: p.table = iv; break; case 1: p.pos = v; break; case 2: p.envPos = v; break; case 3: p.lfoPos = v; break; case 4: p.lfoRate = v; break; case 5: p.unison = iv; break;
                case 6: p.detune = v; break; case 7: p.sub = v; break; case 8: p.cutoff = v; break; case 9: p.res = v; break; case 10: p.envFilt = v; break; case 11: p.kbd = v; break;
                case 12: p.mA = v; break; case 13: p.mD = v; break; case 14: p.mS = v; break; case 15: p.mR = v; break; case 16: p.A = v; break; case 17: p.D = v; break; case 18: p.S = v; break; case 19: p.R = v; break;
                case 20: p.vel = v; break; case 21: p.volume = v; break; }
            break; }
        case E_SAW: { auto& p = saw.p;
            switch (i) { case 0: p.mode = iv; break; case 1: p.detune = v; break; case 2: p.mix = v; break; case 3: p.octave = v; break; case 4: p.cutoff = v; break; case 5: p.res = v; break; case 6: p.hpf = v; break;
                case 7: p.envFilt = v; break; case 8: p.kbd = v; break; case 9: p.ens = v; break; case 10: p.ensRate = v; break; case 11: p.fA = v; break; case 12: p.fD = v; break; case 13: p.fS = v; break; case 14: p.fR = v; break;
                case 15: p.A = v; break; case 16: p.D = v; break; case 17: p.S = v; break; case 18: p.R = v; break; case 19: p.vel = v; break; case 20: p.volume = v; break; }
            break; }
        case E_MODAL: { auto& p = modal.p;
            switch (i) { case 0: p.model = iv; break; case 1: p.exciter = iv; break; case 2: p.bright = v; break; case 3: p.damping = v; break; case 4: p.position = v; break; case 5: p.structure = v; break;
                case 6: p.exLevel = v; break; case 7: p.noise = v; break; case 8: p.release = v; break; case 9: p.spread = v; break; case 10: p.vel = v; break; case 11: p.volume = v; break; }
            break; }
        }
    }
};

inline std::string jstr(const char* s) {
    std::string o = "\"";
    for (const char* c = s; c && *c; ++c) { if (*c == '"' || *c == '\\') o += '\\'; o += *c; }
    return o + "\"";
}
inline std::string jnum(float v) { char b[32]; std::snprintf(b, sizeof b, "%.6g", v); return b; }

// Descripción completa para la pantalla
inline std::string specJson() {
    std::string o = "{\"engines\":[";
    for (int e = 0; e < E_COUNT; ++e) {
        const EngSpec& s = kEng[e];
        if (e) o += ",";
        o += "{\"key\":" + jstr(s.key) + ",\"name\":" + jstr(s.name) + ",\"desc\":" + jstr(s.desc) + ",\"params\":[";
        for (int i = 0; i < s.np; ++i) {
            const PSpec& p = s.p[i];
            if (i) o += ",";
            // DX7: el knob Patch recorre la biblioteca, con los nombres de los sonidos
            if (e == E_DX && i == 0) {
                std::string nm; const int n = dx7::libCount(); for (int k = 0; k < n; ++k) { if (k) nm += "|"; std::string x = dx7::libName(k); for (char& c : x) if (c == '|') c = '/'; nm += std::to_string(k + 1) + " " + x; }
                o += "{\"l\":" + jstr(p.label) + ",\"min\":0,\"max\":" + std::to_string(std::max(0, n - 1)) + ",\"step\":1,\"def\":0,\"fmt\":\"n\",\"log\":false,\"names\":" + jstr(nm.c_str()) + ",\"g\":" + jstr(p.grp) + "}";
                continue;
            }
            o += "{\"l\":" + jstr(p.label) + ",\"min\":" + jnum(p.min) + ",\"max\":" + jnum(p.max) + ",\"step\":" + jnum(p.step) + ",\"def\":" + jnum(p.def) +
                 ",\"fmt\":\"" + std::string(1, p.fmt) + "\",\"log\":" + (p.log ? "true" : "false") + (p.names ? ",\"names\":" + jstr(p.names) : "") + ",\"g\":" + jstr(p.grp) + "}";
        }
        o += "],\"btns\":[";
        for (int i = 0; i < s.nb; ++i) { if (i) o += ","; o += "{\"l\":" + jstr(s.b[i].label) + ",\"def\":" + (s.b[i].def ? "true" : "false") + "}"; }
        o += "],\"layer\":[";
        for (int k = 0; k < 16; ++k) { if (k) o += ","; o += std::to_string(kLayer[e][k]); }
        o += "],\"presets\":[";
        const auto& fp = factoryPresets(e);
        for (size_t k = 0; k < 8; ++k) { if (k) o += ","; o += jstr(k < fp.size() ? fp[k].name : ""); }
        o += "]}";
    }
    o += "],\"kits\":[\"808\",\"909\",\"FM\",\"Chip\",\"Experimental\"],\"lanes\":[\"Kick\",\"Snare\",\"Clap\",\"Rim\",\"Tom\",\"Closed HH\",\"Open HH\",\"Perc\"]}";
    return o;
}

}  // namespace gb
