// yo this is part 2 of the synth series - now with multiple oscillators and ADSR envelope
// OneLoneCoder made the original, we cleaned it up so it actually compiles
// press Z S X C V G B N M K , L . / to play different notes

#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
using namespace std;

#include "input.h"           // cross-platform GetAsyncKeyState shim
#include "olcNoiseMaker.h"  // the audio engine

// turns hertz (cycles per second) into angular velocity (radians per second)
double w(double dHertz)
{
    return dHertz * 2.0 * 3.14159;
}

// oscillator type IDs - we pick one with each call
#define OSC_SINE    0
#define OSC_SQUARE  1
#define OSC_TRIANGLE 2
#define OSC_SAW_ANA 3   // analog saw, sum of harmonics
#define OSC_SAW_DIG 4   // digital saw, math version
#define OSC_NOISE   5

// the main oscillator function - generates a waveform at the given frequency
// nType picks the waveform shape
double osc(double dHertz, double dTime, int nType = OSC_SINE)
{
    switch (nType)
    {
    case OSC_SINE: // smooth wave between -1 and +1
        return sin(w(dHertz) * dTime);

    case OSC_SQUARE: // chiptune wave, hard on/off
        return sin(w(dHertz) * dTime) > 0.0 ? 1.0 : -1.0;

    case OSC_TRIANGLE: // triangle wave, ramps up and down
        return asin(sin(w(dHertz) * dTime)) * (2.0 / 3.14159);

    case OSC_SAW_ANA: // analog saw - sum up to 40 harmonics for a warm sound
        {
            double dOutput = 0.0;
            for (double n = 1.0; n < 40.0; n++)
                dOutput += (sin(w(dHertz * n) * dTime) / n);
            return dOutput * (2.0 / 3.14159);
        }

    case OSC_SAW_DIG: // digital saw - math shortcut, sounds a bit harsher
        return (2.0 / PI) * (dHertz * PI * fmod(dTime, 1.0 / dHertz) - (3.14159 / 2.0));

    case OSC_NOISE: // pure static
        return 2.0 * ((double)rand() / (double)RAND_MAX) - 1.0;

    default:
        return 0.0;
    }
}

// ADSR envelope - shapes the volume of a note over time
// A = attack (ramp up), D = decay (drop to sustain), S = sustain (hold),
// R = release (fade out after key release)
struct sEnvelopeADSR
{
    double dAttackTime;
    double dDecayTime;
    double dSustainAmplitude;
    double dReleaseTime;

    double dStartAmplitude;
    double dTriggerOnTime;
    double dTriggerOffTime;

    bool bNoteOn;

    sEnvelopeADSR()
    {
        dAttackTime = 0.01;
        dDecayTime = 0.01;
        dSustainAmplitude = 1.0;
        dReleaseTime = 0.01;

        dStartAmplitude = 1.0;
        dTriggerOnTime = 0.0;
        dTriggerOffTime = 0.0;

        bNoteOn = false;
    }

    void NoteOn(double dTimeOn)
    {
        bNoteOn = true;
        dTriggerOnTime = dTimeOn;
    }

    void NoteOff(double dTimeOff)
    {
        bNoteOn = false;
        dTriggerOffTime = dTimeOff;
    }

    // returns the amplitude (0.0 to 1.0) at the given moment
    double GetAmplitude(double dTime)
    {
        double dAmplitude = 0.0;
        double dLifeTime = dTime - dTriggerOnTime;

        if (bNoteOn)
        {
            // ATTACK - ramp from 0 to start amplitude
            if (dLifeTime <= dAttackTime)
            {
                dAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude;
            }
            // DECAY - slide from start down to sustain level
            else if (dLifeTime <= (dAttackTime + dDecayTime))
            {
                dAmplitude = ((dLifeTime - dAttackTime) / dDecayTime)
                    * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;
            }
            // SUSTAIN - hold at sustain level until note is released
            else
            {
                dAmplitude = dSustainAmplitude;
            }
        }
        else
        {
            // RELEASE - fade from sustain down to 0
            dAmplitude = ((dTime - dTriggerOffTime) / dReleaseTime)
                * (0.0 - dSustainAmplitude) + dSustainAmplitude;
        }

        // amplitude cant go below 0
        if (dAmplitude <= 0.0001)
            dAmplitude = 0.0;

        return dAmplitude;
    }
};

// global synth state
atomic<double> dFrequencyOutput{0.0};        // current note frequency
sEnvelopeADSR envelope;                       // the volume envelope
double dOctaveBaseFrequency = 110.0;          // A2
double d12thRootOf2 = pow(2.0, 1.0 / 12.0);   // 12 semitones per octave

// called by audio engine for every sample
// returns amplitude between -1.0 and +1.0
double MakeNoise(double dTime)
{
    // mix a sub-octave sine with the main saw wave, then shape with the envelope
    double dOutput = envelope.GetAmplitude(dTime) *
        (
            + 1.0 * osc(dFrequencyOutput * 0.5, dTime, OSC_SINE)
            + 1.0 * osc(dFrequencyOutput,        dTime, OSC_SAW_ANA)
        );

    return dOutput * 0.4;  // master volume
}

int main()
{
    wcout << "www.OneLoneCoder.com - Synthesizer Part 2" << endl
          << "Multiple Oscillators with Single Amplitude Envelope, No Polyphony" << endl << endl;

    // grab audio output devices
    vector<wstring> devices = olcNoiseMaker<short>::Enumerate();

    for (auto d : devices)
        wcout << "Found Output Device: " << d << endl;
    wcout << "Using Device: " << devices[0] << endl;

    // show keyboard layout
    wcout << endl <<
        "|   |   |   |   |   | |   |   |   |   | |   | |   |   |   |" << endl <<
        "|   | S |   |   | F | | G |   |   | J | | K | | L |   |   |" << endl <<
        "|   |___|   |   |___| |___|   |   |___| |___| |___|   |   |__" << endl <<
        "|     |     |     |     |     |     |     |     |     |     |" << endl <<
        "|  Z  |  X  |  C  |  V  |  B  |  N  |  M  |  ,  |  .  |  /  |" << endl <<
        "|_____|_____|_____|_____|_____|_____|_____|_____|_____|_____|" << endl << endl;

    // start the audio engine
    olcNoiseMaker<short> sound(devices[0], 44100, 1, 8, 512);
    sound.SetUserFunction(MakeNoise);

    // main loop - poll the keyboard forever
    int nCurrentKey = -1;
    bool bKeyPressed = false;
    while (1)
    {
        bKeyPressed = false;
        for (int k = 0; k < 16; k++)
        {
            if (GetAsyncKeyState((unsigned char)("ZSXCFVGBNJMK\xbcL\xbe\xbf"[k])) & 0x8000)
            {
                if (nCurrentKey != k)
                {
                    // new key, set freq and trigger envelope
                    dFrequencyOutput = dOctaveBaseFrequency * pow(d12thRootOf2, k);
                    envelope.NoteOn(sound.GetTime());
                    wcout << "\rNote On : " << sound.GetTime() << "s " << dFrequencyOutput << "Hz";
                    nCurrentKey = k;
                }
                bKeyPressed = true;
            }
        }

        if (!bKeyPressed)
        {
            // no keys held - release the envelope
            if (nCurrentKey != -1)
            {
                wcout << "\rNote Off: " << sound.GetTime() << "s                        ";
                envelope.NoteOff(sound.GetTime());
                nCurrentKey = -1;
            }
        }
    }

    return 0;
}
