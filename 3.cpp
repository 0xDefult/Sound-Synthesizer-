// yo this is part 3 of the synth series - polyphony + multiple instruments
// OneLoneCoder made the original, we cleaned it up so it actually compiles
// press Z S X C V G B N M K , L . / to play different notes

#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <thread>
#include <mutex>
using namespace std;

#include "input.h"           // cross-platform GetAsyncKeyState shim
#include "olcNoiseMaker.h"  // the audio engine

// a basic note - one key press = one note
struct note
{
    int nKey;
    double dTimeOn;
    double dTimeOff;
    bool bReleased;
    int nChannel;        // which instrument is playing this note
    bool active;         // is it still going?

    note()
    {
        nKey = 0;
        dTimeOn = 0.0;
        dTimeOff = 0.0;
        bReleased = false;
        nChannel = 0;
        active = true;
    }
};

// oscillator types
const int OSC_SINE = 0;
const int OSC_SQUARE = 1;
const int OSC_TRIANGLE = 2;
const int OSC_SAW_ANA = 3;
const int OSC_SAW_DIG = 4;
const int OSC_NOISE = 5;

// turns a midi-like note number into Hz
// 440 Hz at note 69, doubling every 12 notes
double noteToHz(int nNote)
{
    return pow(2.0, (nNote - 69.0) / 12.0) * 440.0;
}

// the main oscillator function
double osc(double dTime, double dHertz, const int nType = OSC_SINE,
    double dLFOHertz = 0.0, double dLFOAmplitude = 0.0, double dCustom = 50.0)
{
    // phase with optional LFO modulation (vibrato effect)
    double dFrequency = dHertz * 2.0 * PI * dTime
        + dLFOAmplitude * dHertz * sin(dTime * 2.0 * PI * dLFOHertz);

    switch (nType)
    {
    case OSC_SINE:
        return sin(dFrequency);
    case OSC_SQUARE:
        return sin(dFrequency) > 0.0 ? 1.0 : -1.0;
    case OSC_TRIANGLE:
        return asin(sin(dFrequency)) * (2.0 / PI);
    case OSC_SAW_ANA:
        {
            double dOutput = 0.0;
            for (double n = 1.0; n < dCustom; n++)
                dOutput += (sin(n * dFrequency)) / n;
            return dOutput * (2.0 / PI);
        }
    case OSC_SAW_DIG:
        return (2.0 / PI) * (dHertz * PI * fmod(dTime, 1.0 / dHertz) - (PI / 2.0));
    case OSC_NOISE:
        return 2.0 * ((double)rand() / (double)RAND_MAX) - 1.0;
    default:
        return 0.0;
    }
}

// converts a note number on the SCALE to actual Hz
// 256 Hz is roughly middle C area, then 2^(1/12) per semitone
double scale(int nNoteID)
{
    return 256.0 * pow(1.0594630943592952645618252949463, nNoteID);
}

// ADSR envelope - controls volume over the life of a note
struct envelope_adsr
{
    double dAttackTime = 0.1;
    double dDecayTime = 0.1;
    double dSustainAmplitude = 1.0;
    double dReleaseTime = 0.2;
    double dStartAmplitude = 1.0;

    // returns the amplitude at time dTime, given when the note started/ended
    double amplitude(double dTime, double dTimeOn, double dTimeOff)
    {
        double dAmplitude = 0.0;
        double dReleaseAmplitude = 0.0;

        if (dTimeOn > dTimeOff) // note is still being held
        {
            double dLifeTime = dTime - dTimeOn;

            if (dLifeTime <= dAttackTime)
                dAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude;
            else if (dLifeTime <= (dAttackTime + dDecayTime))
                dAmplitude = ((dLifeTime - dAttackTime) / dDecayTime)
                    * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;
            else
                dAmplitude = dSustainAmplitude;
        }
        else // note has been released, time to fade out
        {
            double dLifeTime = dTimeOff - dTimeOn;

            if (dLifeTime <= dAttackTime)
                dReleaseAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude;
            else if (dLifeTime <= (dAttackTime + dDecayTime))
                dReleaseAmplitude = ((dLifeTime - dAttackTime) / dDecayTime)
                    * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;
            else
                dReleaseAmplitude = dSustainAmplitude;

            dAmplitude = ((dTime - dTimeOff) / dReleaseTime)
                * (0.0 - dReleaseAmplitude) + dReleaseAmplitude;
        }

        if (dAmplitude <= 0.000)
            dAmplitude = 0.0;

        return dAmplitude;
    }
};

// simple wrapper so we can pass envelope + times easily
double getEnvelopeAmp(double dTime, envelope_adsr &envelope, double dTimeOn, double dTimeOff)
{
    return envelope.amplitude(dTime, dTimeOn, dTimeOff);
}

// base class for instruments
struct instrument_base
{
    double dVolume = 1.0;
    envelope_adsr env;
    virtual double sound(double dTime, note n, bool &bNoteFinished) = 0;
};

// BELL - sounds like a church bell or glockenspiel
struct instrument_bell : public instrument_base
{
    instrument_bell()
    {
        env.dAttackTime = 0.01;
        env.dDecayTime = 1.0;
        env.dSustainAmplitude = 0.0;
        env.dReleaseTime = 1.0;
    }

    double sound(double dTime, note n, bool &bNoteFinished) override
    {
        double dAmplitude = getEnvelopeAmp(dTime, this->env, n.dTimeOn, n.dTimeOff);
        if (dAmplitude <= 0.0) bNoteFinished = true;

        // 3 sine waves stacked an octave apart for the bell tone
        double dSound =
            + 1.00 * osc(n.dTimeOn - dTime, scale(n.nKey + 12), OSC_SINE, 5.0, 0.001)
            + 0.50 * osc(n.dTimeOn - dTime, scale(n.nKey + 24))
            + 0.25 * osc(n.dTimeOn - dTime, scale(n.nKey + 36));

        return dAmplitude * dSound * dVolume;
    }
};

// HARMONICA - sounds like a mouth harmonica
struct instrument_harmonica : public instrument_base
{
    instrument_harmonica()
    {
        env.dAttackTime = 0.05;
        env.dDecayTime = 1.0;
        env.dSustainAmplitude = 0.95;
        env.dReleaseTime = 0.1;
    }

    double sound(double dTime, note n, bool &bNoteFinished) override
    {
        double dAmplitude = getEnvelopeAmp(dTime, this->env, n.dTimeOn, n.dTimeOff);
        if (dAmplitude <= 0.0) bNoteFinished = true;

        // square waves + a bit of noise = harmonica body
        double dSound =
            + 1.00 * osc(n.dTimeOn - dTime, scale(n.nKey),      OSC_SQUARE, 5.0, 0.001)
            + 0.50 * osc(n.dTimeOn - dTime, scale(n.nKey + 12), OSC_SQUARE)
            + 0.05 * osc(n.dTimeOn - dTime, scale(n.nKey + 24), OSC_NOISE);

        return dAmplitude * dSound * dVolume;
    }
};

// global state - shared between audio thread and main thread
vector<note> vecNotes;
mutex muxNotes;
instrument_bell instBell;
instrument_harmonica instHarm;

// safe_remove: like std::remove_if but actually erases properly
typedef bool(*notePredicate)(note const&);
template<class T>
void safe_remove(T &v, notePredicate f)
{
    auto n = v.begin();
    while (n != v.end())
        if (!f(*n))
            n = v.erase(n);
        else
            ++n;
}

// called by audio engine for every sample
// nChannel is the audio channel, dTime is current time
double MakeNoise(int nChannel, double dTime)
{
    (void)nChannel;
    unique_lock<mutex> lm(muxNotes);
    double dMixedOutput = 0.0;

    // mix all active notes together
    for (auto &n : vecNotes)
    {
        bool bNoteFinished = false;
        double dSound = 0;

        // channel 2 = bell, channel 1 = harmonica
        if (n.nChannel == 2)
            dSound = instBell.sound(dTime, n, bNoteFinished);
        if (n.nChannel == 1)
            dSound = instHarm.sound(dTime, n, bNoteFinished) * 0.5;

        dMixedOutput += dSound;

        if (bNoteFinished && n.dTimeOff > n.dTimeOn)
            n.active = false;
    }

    // clean up finished notes
    safe_remove<vector<note>>(vecNotes, [](note const& item) { return item.active; });

    return dMixedOutput * 0.2;  // master volume
}

int main()
{
    wcout << "www.OneLoneCoder.com - Synthesizer Part 3" << endl
          << "Multiple Oscillators with Polyphony" << endl << endl;

    // grab audio devices
    vector<wstring> devices = olcNoiseMaker<short>::Enumerate();
    for (auto d : devices) wcout << "Found Output Device: " << d << endl;
    wcout << "Using Device: " << devices[0] << endl;

    // show keyboard
    wcout << endl <<
        "|   |   |   |   |   | |   |   |   |   | |   | |   |   |   |" << endl <<
        "|   | S |   |   | F | | G |   |   | J | | K | | L |   |   |" << endl <<
        "|   |___|   |   |___| |___|   |   |___| |___| |___|   |   |__" << endl <<
        "|     |     |     |     |     |     |     |     |     |     |" << endl <<
        "|  Z  |  X  |  C  |  V  |  B  |  N  |  M  |  ,  |  .  |  /  |" << endl <<
        "|_____|_____|_____|_____|_____|_____|_____|_____|_____|_____|" << endl << endl;

    // start audio engine
    olcNoiseMaker<short> sound(devices[0], 44100, 1, 8, 512);
    sound.SetUserFunction(MakeNoise);

    // timing
    auto clock_old_time = chrono::high_resolution_clock::now();
    auto clock_real_time = chrono::high_resolution_clock::now();
    double dElapsedTime = 0.0;

    // main loop - poll keyboard, fire notes on key press
    while (1)
    {
        clock_real_time = chrono::high_resolution_clock::now();
        auto time_last_loop = clock_real_time - clock_old_time;
        clock_old_time = clock_real_time;
        dElapsedTime = chrono::duration<double>(time_last_loop).count();

        for (int k = 0; k < 16; k++)
        {
            short nKeyState = GetAsyncKeyState((unsigned char)("ZSXCFVGBNJMK\xbcL\xbe\xbf"[k]));
            double dTimeNow = sound.GetTime();

            // is this note already in the list?
            muxNotes.lock();
            auto noteFound = find_if(vecNotes.begin(), vecNotes.end(),
                [&k](note const& item) { return item.nKey == k; });

            if (noteFound == vecNotes.end())
            {
                // not playing yet
                if (nKeyState & 0x8000)
                {
            
            
                    // new key press - start a note with the harmonica
                    note n;
                    n.nKey = k;
                    n.dTimeOn = dTimeNow;
                    n.nChannel = 1;
                    n.active = true;
                    vecNotes.emplace_back(n);
                }
                // else: key is up and no note, nothing to do
            }
            else
            {
                // note already playing
                if (nKeyState & 0x8000)
                {
                    // key still held, but if we were in release phase, retrigger
                    if (noteFound->dTimeOff > noteFound->dTimeOn)
                    {
                        noteFound->dTimeOn = dTimeNow;
                        noteFound->active = true;
                    }
                }
                else
                {

                    // key released - turn off the note
                    if (noteFound->dTimeOff < noteFound->dTimeOn)
                    {
                        noteFound->dTimeOff = dTimeNow;
                    }
                }
            }
            muxNotes.unlock();
        }
        wcout << "\rNotes: " << vecNotes.size() << "    ";
        // tiny sleep so the terminal doesnt get spammed into oblivion
        this_thread::sleep_for(chrono::milliseconds(5));
    }

    return 0;
}