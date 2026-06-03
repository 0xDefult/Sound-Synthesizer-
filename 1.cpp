// yo this is part 1 of the synth series - just a single sine wave
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

// global synth stuff - the audio thread reads dFrequencyOutput every sample
atomic<double> dFrequencyOutput{0.0};
double dOctaveBaseFrequency = 110.0;            // A2 in Hz
double d12thRootOf2 = pow(2.0, 1.0 / 12.0);     // 12 notes per octave in western music

// this gets called by the audio engine for every single sample
// returns the amplitude (between -1.0 and +1.0) at the given time
double MakeNoise(double dTime)
{
    // start with whatever frequency the keyboard set
    double dOutput = dFrequencyOutput;

    // if there's a frequency, make sound - otherwise return silence
    if (dOutput != 0.0)
    {
        // basic sine wave: sin(2 * pi * frequency * time)
        dOutput *= sin(dTime * 2.0 * 3.14159);
    }

    return dOutput;
}

int main()
{
    wcout << "www.OneLoneCoder.com - Synthesizer Part 1" << endl
          << "Single Sine Wave Oscillator, No Polyphony" << endl << endl;

    // grab all the audio output devices the system has
    vector<wstring> devices = olcNoiseMaker<short>::Enumerate();

    for (auto d : devices)
        wcout << "Found Output Device: " << d << endl;
    wcout << "Using Device: " << devices[0] << endl;

    // show the keyboard layout in console
    wcout << endl <<
        "|   |   |   |   |   | |   |   |   |   | |   | |   |   |   |" << endl <<
        "|   | S |   |   | F | | G |   |   | J | | K | | L |   |   |" << endl <<
        "|   |___|   |   |___| |___|   |   |___| |___| |___|   |   |__" << endl <<
        "|     |     |     |     |     |     |     |     |     |     |" << endl <<
        "|  Z  |  X  |  C  |  V  |  B  |  N  |  M  |  ,  |  .  |  /  |" << endl <<
        "|_____|_____|_____|_____|_____|_____|_____|_____|_____|_____|" << endl << endl;

    // make the sound machine - 44100 Hz, mono, 8 blocks, 512 samples per block
    olcNoiseMaker<short> sound(devices[0], 44100, 1, 8, 512);

    // hook up our MakeNoise function to the audio engine
    sound.SetUserFunction(MakeNoise);

    // main loop - poll the keyboard forever
    int nCurrentKey = -1;
    bool bKeyPressed = false;
    while (1)
    {
        bKeyPressed = false;
        // 16 keys mapped to ZSXCFVGBNJMK,./L  (and some extra)
        for (int k = 0; k < 16; k++)
        {
            if (GetAsyncKeyState((unsigned char)("ZSXCFVGBNJMK\xbcL\xbe\xbf"[k])) & 0x8000)
            {
                if (nCurrentKey != k)
                {
                    // new key was pressed, set the frequency
                    dFrequencyOutput = dOctaveBaseFrequency * pow(d12thRootOf2, k);
                    wcout << "\rNote On : " << sound.GetTime() << "s " << dFrequencyOutput << "Hz";
                    nCurrentKey = k;
                }
                bKeyPressed = true;
            }
        }

        if (!bKeyPressed)
        {
            // no keys held, kill the note
            if (nCurrentKey != -1)
            {
                wcout << "\rNote Off: " << sound.GetTime() << "s                        ";
                nCurrentKey = -1;
            }
            dFrequencyOutput = 0.0;
        }
    }

    return 0;
}
