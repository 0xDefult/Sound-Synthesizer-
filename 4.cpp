// yo this is the synth project fr fr - sound go brrr
// made by OneLoneCoder but we cleaned it up a bit cuz it was kinda mid ngl

#include <iostream>       // cout go brrr
#include <string>         // for wstring and all that
#include <cmath>          // sin, pow, fmod - math stuff
#include <vector>         // vecNotes and vecChannel vibes
#include <algorithm>      // find_if for searching notes
#include <thread>         // threads for the audio thingy
#include <atomic>         // atomics for safe stuff
#include <mutex>          // mutexes to lock things properly
#include <chrono>         // time stuff for the sequencer

using namespace std;

#define FTYPE double      // type used for audio samples
#include "input.h"          // cross-platform GetAsyncKeyState shim
#include "olcNoiseMaker.h"  // the noise maker class, included AFTER FTYPE define

namespace synth
{
    // yo this turns hertz (like normal frequency) into angular velocity
    // basically just multiplies by 2*PI cuz that's how radians work
    FTYPE w(const FTYPE dHertz)
    {
        return dHertz * 2.0 * PI;
    }

    struct instrument_base;  // forward declaration so note can point to it

    // a note is basically just a sound playing in the synth
    // like when u press a key on a piano, a note is born
    struct note
    {
        int id;                    // where the note is on the scale (which key basically)
        FTYPE on;                  // when the note was hit
        FTYPE off;                 // when the note was released
        bool active;               // is it still playing or nah
        instrument_base *channel;  // which instrument is playing this note

        // default constructor cuz we need to init everything
        note()
        {
            id = 0;
            on = 0.0;
            off = 0.0;
            active = false;
            channel = nullptr;  // no instrument by default
        }
    };

    //////////////////////////////////////////////////////////////////////////////
    // Multi-Function Oscillator
    // these are the different waveform types we can make
    // sine = smooth, square = chiptune vibes, saw = edgy, noise = random chaos
    const int OSC_SINE = 0;
    const int OSC_SQUARE = 1;
    const int OSC_TRIANGLE = 2;
    const int OSC_SAW_ANA = 3;   // analog saw, sounds warm and fuzzy
    const int OSC_SAW_DIG = 4;   // digital saw, sounds more sharp
    const int OSC_NOISE = 5;     // pure static, great for drums

    // the main oscillator function - this is where the magic happens
    // LFO = low frequency oscillator, basically a slow wobble effect
    FTYPE osc(const FTYPE dTime, const FTYPE dHertz, const int nType = OSC_SINE,
        const FTYPE dLFOHertz = 0.0, const FTYPE dLFOAmplitude = 0.0, FTYPE dCustom = 50.0)
    {
        // calculate the phase with optional LFO modulation
        // the LFO adds a wobbly vibrato effect, kinda like singers do
        FTYPE dFreq = w(dHertz) * dTime + dLFOAmplitude * dHertz * (sin(w(dLFOHertz) * dTime));

        switch (nType)
        {
        case OSC_SINE: // smooth wave between -1 and +1, the chill one
            return sin(dFreq);

        case OSC_SQUARE: // chiptune wave, super retro vibes
            return sin(dFreq) > 0 ? 1.0 : -1.0;

        case OSC_TRIANGLE: // triangle wave, kinda in between sine and square
            return asin(sin(dFreq)) * (2.0 / PI);

        case OSC_SAW_ANA: // analog saw wave - sum of harmonics, sounds phat
        {
            FTYPE dOutput = 0.0;
            // adding up a bunch of sines to fake a saw wave
            // more iterations = more harmonics = more accurate but slower
            for (FTYPE n = 1.0; n < dCustom; n++)
                dOutput += (sin(n*dFreq)) / n;
            return dOutput * (2.0 / PI);
        }

        case OSC_SAW_DIG: // digital saw, math is faster but sounds harsher
            return (2.0 / PI) * (dHertz * PI * fmod(dTime, 1.0 / dHertz) - (PI / 2.0));

        case OSC_NOISE: // random static, perfect for hi-hats and snares
            return 2.0 * ((FTYPE)rand() / (FTYPE)RAND_MAX) - 1.0;

        default: // if u pass something weird just return silence
            return 0.0;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    // Scale to Frequency conversion
    // turns a note number (like middle C = 60) into actual Hz
    // 1.05946... is the 12th root of 2 cuz western music has 12 notes per octave

    const int SCALE_DEFAULT = 0;

    FTYPE scale(const int nNoteID, const int nScaleID = SCALE_DEFAULT)
    {
        switch (nScaleID)
        {
        case SCALE_DEFAULT:
        default:
            // 8 Hz base, then multiply by 2^(n/12) for each semitone
            return 8 * pow(1.0594630943592952645618252949463, nNoteID);
            
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    // Envelopes
    // envelopes shape how a note sounds over time
    // attack = how fast it gets loud
    // decay = how fast it drops to sustain level
    // sustain = the steady volume while held
    // release = how fast it fades after u let go

    struct envelope
    {
        // pure virtual function, every envelope has to implement this
        virtual FTYPE amplitude(const FTYPE dTime, const FTYPE dTimeOn, const FTYPE dTimeOff) = 0;
    };

    // ADSR envelope = the classic shape most synths use
    struct envelope_adsr : public envelope
    {
        FTYPE dAttackTime;
        FTYPE dDecayTime;
        FTYPE dSustainAmplitude;
        FTYPE dReleaseTime;
        FTYPE dStartAmplitude;

        // default settings, sounds pretty decent
        envelope_adsr()
        {
            dAttackTime = 0.1;
            dDecayTime = 0.1;
            dSustainAmplitude = 1.0;
            dReleaseTime = 0.2;
            dStartAmplitude = 1.0;
        }

        // this calculates the volume at any given moment
        virtual FTYPE amplitude(const FTYPE dTime, const FTYPE dTimeOn, const FTYPE dTimeOff)
        {
            FTYPE dAmplitude = 0.0;
            FTYPE dReleaseAmplitude = 0.0;

            if (dTimeOn > dTimeOff) // note is currently being held down
            {
                FTYPE dLifeTime = dTime - dTimeOn;

                // attack phase - ramping up from 0 to start amplitude
                if (dLifeTime <= dAttackTime)
                    dAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude;

                // decay phase - sliding from start amplitude down to sustain
                if (dLifeTime > dAttackTime && dLifeTime <= (dAttackTime + dDecayTime))
                    dAmplitude = ((dLifeTime - dAttackTime) / dDecayTime) * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;

                // sustain phase - just hold at sustain amplitude forever
                if (dLifeTime > (dAttackTime + dDecayTime))
                    dAmplitude = dSustainAmplitude;
            }
            else // note has been released, time to fade out
            {
                FTYPE dLifeTime = dTimeOff - dTimeOn;

                // figure out what the amplitude was when the note was released
                if (dLifeTime <= dAttackTime)
                    dReleaseAmplitude = (dLifeTime / dAttackTime) * dStartAmplitude;

                if (dLifeTime > dAttackTime && dLifeTime <= (dAttackTime + dDecayTime))
                    dReleaseAmplitude = ((dLifeTime - dAttackTime) / dDecayTime) * (dSustainAmplitude - dStartAmplitude) + dStartAmplitude;

                if (dLifeTime > (dAttackTime + dDecayTime))
                    dReleaseAmplitude = dSustainAmplitude;

                // release phase - fade from release amplitude down to 0
                dAmplitude = ((dTime - dTimeOff) / dReleaseTime) * (0.0 - dReleaseAmplitude) + dReleaseAmplitude;
            }

            // amplitude cant be negative, that would be weird
            if (dAmplitude <= 0.01)
                dAmplitude = 0.0;

            return dAmplitude;
        }
    };

    // wrapper for envelope.amplitude so we can call it like a regular function
    FTYPE env(const FTYPE dTime, envelope &env, const FTYPE dTimeOn, const FTYPE dTimeOff)
    {
        return env.amplitude(dTime, dTimeOn, dTimeOff);
    }

    // base class for all instruments
    // every instrument needs volume, an envelope, a name, and a sound function
    struct instrument_base
    {
        FTYPE dVolume;
        synth::envelope_adsr env;
        FTYPE fMaxLifeTime;        // how long a note can play before being killed (-1 = no limit)
        wstring name;              // display name for the UI
        // pure virtual - every instrument has to make its own sound
        virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished) = 0;
    };

    // BELL - sounds like a church bell or maybe a glockenspiel
    // multiple harmonics stacked to give that classic bell tone
    struct instrument_bell : public instrument_base
    {
        instrument_bell()
        {
            env.dAttackTime = 0.01;       // quick attack, like hitting a bell
            env.dDecayTime = 1.0;         // long decay
            env.dSustainAmplitude = 0.0;  // no sustain, bells just ring out
            env.dReleaseTime = 1.0;
            fMaxLifeTime = 3.0;           // bells die after 3 seconds
            dVolume = 1.0;
            name = L"Bell";
        }

        virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
        {
            FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
            if (dAmplitude <= 0.0) bNoteFinished = true;  // tell main loop we're done

            // stack 3 octaves of sine waves for that classic bell sound
            FTYPE dSound =
                + 1.00 * synth::osc(dTime - n.on, synth::scale(n.id + 12), synth::OSC_SINE, 5.0, 0.001)
                + 0.50 * synth::osc(dTime - n.on, synth::scale(n.id + 24))
                + 0.25 * synth::osc(dTime - n.on, synth::scale(n.id + 36));

            return dAmplitude * dSound * dVolume;
        }
    };

    // 8-BIT BELL - like the bell but uses square waves for chiptune vibes
    // sounds like an NES or something, super retro
    struct instrument_bell8 : public instrument_base
    {
        instrument_bell8()
        {
            env.dAttackTime = 0.01;
            env.dDecayTime = 0.5;
            env.dSustainAmplitude = 0.8;  // has sustain, the bell doesnt
            env.dReleaseTime = 1.0;
            fMaxLifeTime = 3.0;
            dVolume = 1.0;
            name = L"8-Bit Bell";
        }

        virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
        {
            FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
            if (dAmplitude <= 0.0) bNoteFinished = true;

            // square wave + harmonics = chiptune bell sounds
            FTYPE dSound =
                + 1.00 * synth::osc(dTime - n.on, synth::scale(n.id), synth::OSC_SQUARE, 5.0, 0.001)
                + 0.50 * synth::osc(dTime - n.on, synth::scale(n.id + 12))
                + 0.25 * synth::osc(dTime - n.on, synth::scale(n.id + 24));

            return dAmplitude * dSound * dVolume;
        }
    };

    // HARMONICA - sounds like, well, a harmonica lol
    // mix of saw and square waves plus a little noise for breathiness
    struct instrument_harmonica : public instrument_base
    {
        instrument_harmonica()
        {
            env.dAttackTime = 0.00;       // instant attack like blowing into a harmonica
            env.dDecayTime = 1.0;
            env.dSustainAmplitude = 0.95; // strong sustain
            env.dReleaseTime = 0.1;       // quick release
            fMaxLifeTime = -1.0;          // no time limit
            name = L"Harmonica";
            dVolume = 0.3;                // gotta keep this quieter, gets loud otherwise
        }

        virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
        {
            FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
            if (dAmplitude <= 0.0) bNoteFinished = true;

            // saw wave for the body, square waves for the harmonics, noise for breath
            FTYPE dSound =
                + 1.0  * synth::osc(n.on - dTime, synth::scale(n.id-12), synth::OSC_SAW_ANA, 5.0, 0.001, 100)
                + 1.00 * synth::osc(dTime - n.on, synth::scale(n.id), synth::OSC_SQUARE, 5.0, 0.001)
                + 0.50 * synth::osc(dTime - n.on, synth::scale(n.id + 12), synth::OSC_SQUARE)
                + 0.05  * synth::osc(dTime - n.on, synth::scale(n.id + 24), synth::OSC_NOISE);

            return dAmplitude * dSound * dVolume;
        }
    };

    // DRUM KICK - that BOOM BOOM sound, the heartbeat of any beat
    // low sine wave + a tiny bit of noise for that punch
    struct instrument_drumkick : public instrument_base
    {
        instrument_drumkick()
        {
            env.dAttackTime = 0.01;       // super fast attack, kicks hit hard
            env.dDecayTime = 0.15;        // quick decay
            env.dSustainAmplitude = 0.0;  // no sustain
            env.dReleaseTime = 0.0;
            fMaxLifeTime = 1.5;           // kicks are short
            name = L"Drum Kick";
            dVolume = 1.0;
        }

        virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
        {
            FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
            // kill the note if it has been playing too long
            if(fMaxLifeTime > 0.0 && dTime - n.on >= fMaxLifeTime)
                bNoteFinished = true;

            // super low sine + tiny noise = kick drum
            FTYPE dSound =
                + 0.99 * synth::osc(dTime - n.on, synth::scale(n.id - 36), synth::OSC_SINE, 1.0, 1.0)
                + 0.01 * synth::osc(dTime - n.on, 0, synth::OSC_NOISE);

            return dAmplitude * dSound * dVolume;
        }
    };

    // DRUM SNARE - that crispy snappy sound, like a real snare
    // half sine wave for the body, half noise for the crack
    struct instrument_drumsnare : public instrument_base
    {
        instrument_drumsnare()
        {
            env.dAttackTime = 0.0;
            env.dDecayTime = 0.2;
            env.dSustainAmplitude = 0.0;
            env.dReleaseTime = 0.0;
            fMaxLifeTime = 1.0;
            name = L"Drum Snare";
            dVolume = 1.0;
        }

        virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
        {
            FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
            if (fMaxLifeTime > 0.0 && dTime - n.on >= fMaxLifeTime)
                bNoteFinished = true;

            // 50% tone + 50% noise = classic snare recipe
            FTYPE dSound =
                + 0.5 * synth::osc(dTime - n.on, synth::scale(n.id - 24), synth::OSC_SINE, 0.5, 1.0)
                + 0.5 * synth::osc(dTime - n.on, 0, synth::OSC_NOISE);

            return dAmplitude * dSound * dVolume;
        }
    };

    // DRUM HIHAT - the tsssss sound, mostly noise
    // square wave for the metallic shimmer + lots of noise
    struct instrument_drumhihat : public instrument_base
    {
        instrument_drumhihat()
        {
            env.dAttackTime = 0.01;
            env.dDecayTime = 0.05;        // super short decay
            env.dSustainAmplitude = 0.0;
            env.dReleaseTime = 0.0;
            fMaxLifeTime = 1.0;
            name = L"Drum HiHat";
            dVolume = 0.5;                // half volume, hi-hats are usually quieter
        }

        virtual FTYPE sound(const FTYPE dTime, synth::note n, bool &bNoteFinished)
        {
            FTYPE dAmplitude = synth::env(dTime, env, n.on, n.off);
            if (fMaxLifeTime > 0.0 && dTime - n.on >= fMaxLifeTime)
                bNoteFinished = true;

            // mostly noise with a little square wave for that metallic edge
            FTYPE dSound =
                + 0.1 * synth::osc(dTime - n.on, synth::scale(n.id - 12), synth::OSC_SQUARE, 1.5, 1)
                + 0.9 * synth::osc(dTime - n.on, 0, synth::OSC_NOISE);

            return dAmplitude * dSound * dVolume;
        }
    };

    // SEQUENCER - this thing plays patterns automatically
    // like a drum machine or tracker, super useful for making beats
    struct sequencer
    {
    public:
        // a channel is basically one instrument + a pattern of when to play it
        struct channel
        {
            instrument_base* instrument;  // what instrument this channel uses
            wstring sBeat;                // pattern string, X = play, . = rest
        };

    public:
        // tempo = BPM (beats per minute), beats = how many main beats, subbeats = subdivisions per beat
        sequencer(float tempo = 120.0f, int beats = 4, int subbeats = 4)
        {
            nBeats = beats;
            nSubBeats = subbeats;
            fTempo = tempo;
            fBeatTime = (60.0f / fTempo) / (float)nSubBeats;  // time per subbeat in seconds
            nCurrentBeat = 0;
            nTotalBeats = nSubBeats * nBeats;
            fAccumulate = 0;
        }

        // called every frame to advance the sequencer
        // returns how many new notes were created
        int Update(FTYPE fElapsedTime)
        {
            vecNotes.clear();  // clear the new notes list

            // accumulate time and trigger beats when we cross the threshold
            fAccumulate += fElapsedTime;
            while(fAccumulate >= fBeatTime)
            {
                fAccumulate -= fBeatTime;
                nCurrentBeat++;  // move to next beat

                // loop back to start when we reach the end
                if (nCurrentBeat >= nTotalBeats)
                    nCurrentBeat = 0;

                // check each channel to see if it should play on this beat
                int c = 0;
                for (auto v : vecChannel)
                {
                    if (v.sBeat[nCurrentBeat] == L'X')  // X means play!
                    {
                        note n;
                        n.channel = vecChannel[c].instrument;
                        n.active = true;
                        n.id = 64;  // default note id
                        vecNotes.push_back(n);
                    }
                    c++;
                }
            }

            return vecNotes.size();
        }

        // add an instrument channel to the sequencer
        void AddInstrument(instrument_base *inst)
        {
            channel c;
            c.instrument = inst;
            vecChannel.push_back(c);
        }

    public:
        int nBeats;
        int nSubBeats;
        FTYPE fTempo;
        FTYPE fBeatTime;       // duration of one subbeat
        FTYPE fAccumulate;     // time accumulator for advancing beats
        int nCurrentBeat;      // current position in the pattern
        int nTotalBeats;       // total length of the pattern

    public:
        vector<channel> vecChannel;  // all the instrument channels
        vector<note> vecNotes;        // notes generated this update

    private:
        // nothing here lol
    };
}

// global variables - i know globals are bad but this is a demo so it's chill
vector<synth::note> vecNotes;        // all currently playing notes
mutex muxNotes;                       // protects vecNotes from race conditions
synth::instrument_bell instBell;      // a bell instance
synth::instrument_harmonica instHarm; // a harmonica instance (used for keyboard)
synth::instrument_drumkick instKick;  // kick drum instance
synth::instrument_drumsnare instSnare;// snare drum instance
synth::instrument_drumhihat instHiHat;// hi-hat instance

// typedef for a lambda that decides whether to KEEP an item
// (returns true = keep, false = remove)
typedef bool(*lambda)(synth::note const& item);

// this is like remove_if but actually works properly
// the standard library one is kinda broken cuz iterator invalidation stuff
template<class T>
void safe_remove(T &v, lambda f)
{
    auto n = v.begin();
    while (n != v.end())
        if (!f(*n))
            n = v.erase(n);  // erase returns the next valid iterator
        else
            ++n;  // move to next item
}

// this is the function that the audio system calls to get samples
// it mixes all the active notes together into one output
// nChannel = which audio channel (we only use 0), dTime = current time
FTYPE MakeNoise(int nChannel, FTYPE dTime)
{
    unique_lock<mutex> lm(muxNotes);  // lock so audio thread and main thread dont fight
    FTYPE dMixedOutput = 0.0;

    // loop through all the notes and add their sound to the mix
    for (auto &n : vecNotes)
    {
        bool bNoteFinished = false;
        FTYPE dSound = 0;

        // ask the instrument to generate a sample for this note
        if(n.channel != nullptr)
            dSound = n.channel->sound(dTime, n, bNoteFinished);

        // add it to the mix
        dMixedOutput += dSound;

        if (bNoteFinished)  // the note is done, mark it for removal
            n.active = false;
    }
    // remove all the notes that are done playing
    safe_remove<vector<synth::note>>(vecNotes, [](synth::note const& item) { return item.active; });
    return dMixedOutput * 0.2;  // master volume, keeps things from clipping
}

int main()
{
    // the intro, gotta plug the original creator lol
    wcout << "www.OneLoneCoder.com - Synthesizer Part 4" << endl
          << "Multiple FM Oscillators, Sequencing, Polyphony" << endl << endl;

    // grab all the audio devices the system can find
    vector<wstring> devices = olcNoiseMaker<short>::Enumerate();

    // create the sound machine with 44100 Hz, mono, 8 blocks, 256 samples per block
    olcNoiseMaker<short> sound(devices[0], 44100, 1, 8, 256);

    // hook up our MakeNoise function so the audio thread can call it
    sound.SetUserFunction(MakeNoise);

    // make a screen buffer for drawing - this is the retro console UI vibes
    wchar_t *screen = new wchar_t[80 * 30];
    HANDLE hConsole = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    SetConsoleActiveScreenBuffer(hConsole);
    DWORD dwBytesWritten = 0;

    // helper to write text onto the screen buffer
    // x,y is position, s is the string to write
    auto draw = [&screen](int x, int y, wstring s)
    {
        for (size_t i = 0; i < s.size(); i++)
            screen[y * 80 + x + i] = s[i];
    };

    // timing variables for the main loop
    auto clock_old_time = chrono::high_resolution_clock::now();
    auto clock_real_time = chrono::high_resolution_clock::now();
    double dElapsedTime = 0.0;
    double dWallTime = 0.0;

    // set up the sequencer with 90 BPM (pretty chill tempo)
    synth::sequencer seq(90.0);
    seq.AddInstrument(&instKick);   // channel 0 = kick
    seq.AddInstrument(&instSnare);  // channel 1 = snare
    seq.AddInstrument(&instHiHat);  // channel 2 = hi-hat

    // the patterns - X plays the note, . is a rest
    // classic rock beat pattern, kinda
    seq.vecChannel.at(0).sBeat = L"X...X...X..X.X..";  // kick
    seq.vecChannel.at(1).sBeat = L"..X...X...X...X.";  // snare
    seq.vecChannel.at(2).sBeat = L"X.X.X.X.X.X.X.XX";  // hi-hat (lots of hits)

    // MAIN LOOP - this runs forever until u close the program
    while (1)
    {
        // === SOUND STUFF ===

        // figure out how much time passed since last frame
        clock_real_time = chrono::high_resolution_clock::now();
        auto time_last_loop = clock_real_time - clock_old_time;
        clock_old_time = clock_real_time;
        dElapsedTime = chrono::duration<FTYPE>(time_last_loop).count();
        dWallTime += dElapsedTime;
        FTYPE dTimeNow = sound.GetTime();  // audio engine time

        // === SEQUENCER ===
        // update the sequencer and copy any new notes into our main note list
        int newNotes = seq.Update(dElapsedTime);
        muxNotes.lock();
        for (int a = 0; a < newNotes; a++)
        {
            seq.vecNotes[a].on = dTimeNow;
            vecNotes.emplace_back(seq.vecNotes[a]);
        }
        muxNotes.unlock();

        // === KEYBOARD INPUT ===
        // check each of the 16 keys on the keyboard
        // Z S X C F V G B N J M K , L . /  -- mapped to piano keys
        for (int k = 0; k < 16; k++)
        {
            short nKeyState = GetAsyncKeyState((unsigned char)("ZSXCFVGBNJMK\xbcL\xbe\xbf"[k]));

            // is this note already playing?
            muxNotes.lock();
            auto noteFound = find_if(vecNotes.begin(), vecNotes.end(),
                [&k](synth::note const& item) { return item.id == k+64 && item.channel == &instHarm; });

            if (noteFound == vecNotes.end())
            {
                // note aint playing yet
                if (nKeyState & 0x8000)  // key is held down
                {
                    // start a new note!
                    synth::note n;
                    n.id = k + 64;
                    n.on = dTimeNow;
                    n.active = true;
                    n.channel = &instHarm;
                    vecNotes.emplace_back(n);
                }
                // else key is up and note aint playing, do nothing
            }
            else
            {
                // note is already playing
                if (nKeyState & 0x8000)
                {
                    // key still held - if we were in release phase, retrigger
                    if (noteFound->off > noteFound->on)
                    {
                        noteFound->on = dTimeNow;
                        noteFound->active = true;
                    }
                }
                else
                {
                    // key was released - turn off the note
                    if (noteFound->off < noteFound->on)
                        noteFound->off = dTimeNow;
                }
            }
            muxNotes.unlock();
        }

        // === VISUAL STUFF ===
        // draw the UI in the console

        // clear the screen buffer
        for (int i = 0; i < 80 * 30; i++) screen[i] = L' ';

        // draw the sequencer header
        draw(2, 2, L"SEQUENCER:");

        // draw the beat markers (O for main beat, . for subbeats)
        for (int beats = 0; beats < seq.nBeats; beats++)
        {
            draw(beats*seq.nSubBeats + 20, 2, L"O");
            for (int subbeats = 1; subbeats < seq.nSubBeats; subbeats++)
                draw(beats*seq.nSubBeats + subbeats + 20, 2, L".");
        }

        // draw each channel's pattern
        int n = 0;
        for (auto v : seq.vecChannel)
        {
            draw(2, 3 + n, v.instrument->name);  // channel name
            draw(20, 3 + n, v.sBeat);            // the pattern
            n++;
        }

        // draw the playhead cursor
        draw(20 + seq.nCurrentBeat, 1, L"|");

        // draw the piano keyboard
        draw(2, 8,  L"|   |   |   |   |   | |   |   |   |   | |   | |   |   |   |  ");
        draw(2, 9,  L"|   | S |   |   | F | | G |   |   | J | | K | | L |   |   |  ");
        draw(2, 10, L"|   |___|   |   |___| |___|   |   |___| |___| |___|   |   |__");
        draw(2, 11, L"|     |     |     |     |     |     |     |     |     |     |");
        draw(2, 12, L"|  Z  |  X  |  C  |  V  |  B  |  N  |  M  |  ,  |  .  |  /  |");
        draw(2, 13, L"|_____|_____|_____|_____|_____|_____|_____|_____|_____|_____|");

        // draw the stats line at the bottom
        wstring stats = L"Notes: " + to_wstring(vecNotes.size())
                      + L" Wall Time: " + to_wstring(dWallTime)
                      + L" CPU Time: " + to_wstring(dTimeNow)
                      + L" Latency: " + to_wstring(dWallTime - dTimeNow);
        draw(2, 15, stats);

        // push the screen buffer to the actual console
        WriteConsoleOutputCharacter(hConsole, screen, 80 * 30, { 0,0 }, &dwBytesWritten);
    }

    return 0;
}
