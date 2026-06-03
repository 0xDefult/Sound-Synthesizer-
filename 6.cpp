// 6.cpp - this used to be a Windows-only olcNoiseMaker header
// it's been replaced by the cross-platform olcNoiseMaker.h
// this file now just demonstrates that the new header works on macOS

#include "olcNoiseMaker.h"

// just a quick smoke test - same idea as 5.cpp but with int,FTYPE signature
static FTYPE testCallback(int, FTYPE dTime) { return sin(dTime * 2.0 * PI * 440.0) * 0.5; }

int main()
{
    auto devices = olcNoiseMaker<short>::Enumerate();
    std::wcout << L"6.cpp: int,FTYPE user function also works. Found "
               << devices.size() << L" device(s)." << std::endl;

    olcNoiseMaker<short> sound(devices[0], 44100, 1, 8, 256);
    sound.SetUserFunction(testCallback);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sound.Stop();
    return 0;
}
