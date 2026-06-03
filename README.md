
# Sound Synthesizer

Cross-platform C++ software synthesizer based on OneLoneCoder's Synthesizer series, ported from Windows (winmm) to macOS (CoreAudio).

## Quick Start

### macOS

```bash
clang++ -std=c++17 1.cpp -o synth \
-framework AudioToolbox \
-framework CoreAudio \
-framework AudioUnit
./synth
```

### Windows

```bash
cl /EHsc /std:c++17 1.cpp user32.lib winmm.lib
```

## Project Structure

| File    | Purpose                              |
| ------- | ------------------------------------ |
| `1.cpp` | Single sine oscillator               |
| `2.cpp` | Multiple oscillators + ADSR envelope |
| `3.cpp` | Polyphony + multiple instruments     |
| `4.cpp` | Full synthesizer + sequencer         |
| `5.cpp` | Audio engine smoke test              |
| `6.cpp` | Callback API test                    |

### Core Components

* `olcNoiseMaker.h` — Header-only audio engine

  * macOS: CoreAudio
  * Windows: winmm
* `input.h` — Cross-platform keyboard input layer

## Architecture

Two-thread design:

1. **Main Thread**

   * Keyboard input
   * Note management
   * UI / sequencer

2. **Audio Thread**

   * Calls `MakeNoise()`
   * Generates audio samples
   * Sends output to CoreAudio/winmm

Audio generation is callback-based:

```cpp
sound.SetUserFunction(MakeNoise);
```

## Feature Progression

### 1.cpp

* Single sine wave
* Monophonic
* No envelope

### 2.cpp

* ADSR envelope
* Multiple oscillators
* Analog saw synthesis

### 3.cpp

* Polyphony
* Multiple instruments
* Per-note envelopes
* Shared note vector protected by mutex

### 4.cpp

* Instrument bank
* Drum sequencer
* Console UI
* Closest thing to a complete synthesizer

## Keyboard Controls

Play notes using:

```text
Z S X C F V G B N J M K , L . /
```

Press `Ctrl+C` to quit.

## macOS Notes

* Accessibility permission is required for keyboard polling.
* Uses the system default audio output.
* `Enumerate()` returns only `"Default Output"`.
* Console UI from `4.cpp` is not implemented on macOS.

## Known Limitations

* No clean shutdown (`Ctrl+C` required).
* Linux audio is currently a stub.
* Mixing can clip at high polyphony.
* Audio device selection is not supported on macOS.
* Threading is simplified and not real-time optimized.

## Common Build Issue

If you see linker errors such as:

```text
AudioComponentFindNext
AudioUnitInitialize
AudioOutputUnitStart
```

or many `std::__1::*` errors:

* Use `clang++` (not `clang`)
* Link AudioToolbox/CoreAudio/AudioUnit frameworks

Example:

```bash
clang++ 3.cpp -std=c++17 \
-framework AudioToolbox \
-framework CoreAudio \
-framework AudioUnit
```
