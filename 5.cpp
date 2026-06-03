// 5.cpp - this used to be a Windows-only olcNoiseMaker header
// it's been replaced by the cross-platform olcNoiseMaker.h
// this file now just demonstrates that the new header works on macOS

#include "olcNoiseMaker.h"

// just a quick smoke test - make a noise maker, see if it enumerates
int main()
{
    auto devices = olcNoiseMaker<short>::Enumerate();
    std::wcout << L"5.cpp: olcNoiseMaker is cross-platform now. Found "
               << devices.size() << L" device(s)." << std::endl;
    return 0;
}

