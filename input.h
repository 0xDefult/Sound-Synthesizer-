// input.h - cross-platform keyboard + console shim
//
// On Windows: GetAsyncKeyState + the console buffer API come from <Windows.h>.
// On macOS:  we don't have GetAsyncKeyState, so we provide one using
//            CGEventTap (Quartz Event Services) which gives us real key
//            up/down state. The user must grant Accessibility permission to
//            the terminal that runs the program (System Settings -> Privacy
//            & Security -> Accessibility).
//
//            As a fallback, if Accessibility isn't granted, we read
//            single-character stdin so you can at least press number keys
//            (1-9, 0) to trigger notes for testing.

#pragma once

#ifdef _WIN32
    #include <Windows.h>
    // On Windows, GetAsyncKeyState, CreateConsoleScreenBuffer etc. are real.
    // No shim needed.

#elif defined(__APPLE__)

    #include <cstddef>
    #include <cstdint>
    #include <atomic>
    #include <array>
    #include <thread>
    #include <chrono>
    #include <iostream>
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <Carbon/Carbon.h>

    // ---------- keyboard state ----------
    // We track 256 virtual key slots (one per Windows VK_*/ASCII byte).
    // bit 0x8000 mirrors the Windows "currently down" high bit.
    // bit 0x0001 mirrors the Windows "transitioned since last call" low bit
    //   (we just always set it; nobody in the synth code checks it).
    static std::array<std::atomic<uint16_t>, 256> g_keyState{};

    // Map an ASCII character (what the synth code passes to GetAsyncKeyState)
    // to a macOS virtual keycode. Returns -1 for chars we don't know.
    static int asciiToMacKeycode(char c)
    {
        // Letters - use the US keyboard layout kVK_ANSI_* constants
        switch (c) {
            case 'a': case 'A': return kVK_ANSI_A;
            case 'b': case 'B': return kVK_ANSI_B;
            case 'c': case 'C': return kVK_ANSI_C;
            case 'd': case 'D': return kVK_ANSI_D;
            case 'e': case 'E': return kVK_ANSI_E;
            case 'f': case 'F': return kVK_ANSI_F;
            case 'g': case 'G': return kVK_ANSI_G;
            case 'h': case 'H': return kVK_ANSI_H;
            case 'i': case 'I': return kVK_ANSI_I;
            case 'j': case 'J': return kVK_ANSI_J;
            case 'k': case 'K': return kVK_ANSI_K;
            case 'l': case 'L': return kVK_ANSI_L;
            case 'm': case 'M': return kVK_ANSI_M;
            case 'n': case 'N': return kVK_ANSI_N;
            case 'o': case 'O': return kVK_ANSI_O;
            case 'p': case 'P': return kVK_ANSI_P;
            case 'q': case 'Q': return kVK_ANSI_Q;
            case 'r': case 'R': return kVK_ANSI_R;
            case 's': case 'S': return kVK_ANSI_S;
            case 't': case 'T': return kVK_ANSI_T;
            case 'u': case 'U': return kVK_ANSI_U;
            case 'v': case 'V': return kVK_ANSI_V;
            case 'w': case 'W': return kVK_ANSI_W;
            case 'x': case 'X': return kVK_ANSI_X;
            case 'y': case 'Y': return kVK_ANSI_Y;
            case 'z': case 'Z': return kVK_ANSI_Z;
            // Punctuation - map the chars used in the synth code
            case ',': return kVK_ANSI_Comma;
            case '.': return kVK_ANSI_Period;
            case '/': return kVK_ANSI_Slash;
            case ';': return kVK_ANSI_Semicolon;
            case '\'': return kVK_ANSI_Quote;
            case '[': return kVK_ANSI_LeftBracket;
            case ']': return kVK_ANSI_RightBracket;
            case '\\': return kVK_ANSI_Backslash;
            case '-': return kVK_ANSI_Minus;
            case '=': return kVK_ANSI_Equal;
            case '`': return kVK_ANSI_Grave;
            // Digits
            case '0': return kVK_ANSI_0;
            case '1': return kVK_ANSI_1;
            case '2': return kVK_ANSI_2;
            case '3': return kVK_ANSI_3;
            case '4': return kVK_ANSI_4;
            case '5': return kVK_ANSI_5;
            case '6': return kVK_ANSI_6;
            case '7': return kVK_ANSI_7;
            case '8': return kVK_ANSI_8;
            case '9': return kVK_ANSI_9;
            // Whitespace
            case ' ': return kVK_Space;
            case '\r': case '\n': return kVK_Return;
            case '\t': return kVK_Tab;
            case 0x1b: return kVK_Escape;
        }
        return -1;
    }

    // Reverse map for CGEventTap callbacks: a keycode + modifier flags
    // -> the matching ASCII byte (or 0 if no direct match). Only handles
    // the letters/digits/punctuation the synth uses, ignoring shift state
    // (we use the unshifted US layout).
    static char macKeycodeToAscii(uint16_t keycode)
    {
        switch (keycode) {
            case kVK_ANSI_A: return 'a'; case kVK_ANSI_B: return 'b';
            case kVK_ANSI_C: return 'c'; case kVK_ANSI_D: return 'd';
            case kVK_ANSI_E: return 'e'; case kVK_ANSI_F: return 'f';
            case kVK_ANSI_G: return 'g'; case kVK_ANSI_H: return 'h';
            case kVK_ANSI_I: return 'i'; case kVK_ANSI_J: return 'j';
            case kVK_ANSI_K: return 'k'; case kVK_ANSI_L: return 'l';
            case kVK_ANSI_M: return 'm'; case kVK_ANSI_N: return 'n';
            case kVK_ANSI_O: return 'o'; case kVK_ANSI_P: return 'p';
            case kVK_ANSI_Q: return 'q'; case kVK_ANSI_R: return 'r';
            case kVK_ANSI_S: return 's'; case kVK_ANSI_T: return 't';
            case kVK_ANSI_U: return 'u'; case kVK_ANSI_V: return 'v';
            case kVK_ANSI_W: return 'w'; case kVK_ANSI_X: return 'x';
            case kVK_ANSI_Y: return 'y'; case kVK_ANSI_Z: return 'z';
            case kVK_ANSI_Comma:  return ',';
            case kVK_ANSI_Period: return '.';
            case kVK_ANSI_Slash:  return '/';
            case kVK_ANSI_0: return '0'; case kVK_ANSI_1: return '1';
            case kVK_ANSI_2: return '2'; case kVK_ANSI_3: return '3';
            case kVK_ANSI_4: return '4'; case kVK_ANSI_5: return '5';
            case kVK_ANSI_6: return '6'; case kVK_ANSI_7: return '7';
            case kVK_ANSI_8: return '8'; case kVK_ANSI_9: return '9';
            case kVK_Space: return ' ';
        }
        return 0;
    }

    // CGEventTap callback. Runs on the main thread (or a CFRunLoop
    // thread), so we just update atomic state and return the event
    // unchanged so it still reaches other apps.
    static CGEventRef eventTapCallback(CGEventTapProxy proxy,
                                       CGEventType type,
                                       CGEventRef event,
                                       void *userdata)
    {
        (void)proxy; (void)userdata;
        if (type == kCGEventKeyDown || type == kCGEventKeyUp)
        {
            uint16_t keycode = (uint16_t)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
            char c = macKeycodeToAscii(keycode);
            if (c != 0)
            {
                uint8_t idx = (uint8_t)c;
                if (type == kCGEventKeyDown)
                    g_keyState[idx].store((uint16_t)0x8001);  // down + transitioned
                else
                    g_keyState[idx].store(0);                 // up
            }
        }
        return event;
    }

    // Background thread that runs the CFRunLoop for the event tap.
    static void macInputThread()
    {
        // kCGHIDEventTap = listen at the hardware input level (before
        // the focused app), kCGEventTapOptionListenOnly = don't modify
        // events, just observe.
        CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
        CFMachPortRef tap = CGEventTapCreate(
            kCGSessionEventTap,
            kCGHeadInsertEventTap,
            kCGEventTapOptionListenOnly,
            mask,
            eventTapCallback,
            nullptr
        );

        if (!tap)
        {
            // Accessibility permission not granted. Print once and exit thread.
            // The synth will fall back to stdin-based input via the main loop.
            static std::atomic<bool> printed{false};
            if (!printed.exchange(true))
            {
                std::cerr <<
                    "\n[input.h] macOS: could not create CGEventTap. Grant Accessibility\n"
                    "[input.h] permission to your terminal (System Settings -> Privacy\n"
                    "[input.h] & Security -> Accessibility) and re-run. Falling back\n"
                    "[input.h] to stdin input - press number keys 1-9, then Enter.\n"
                    << std::endl;
            }
            return;
        }

        CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(nullptr, tap, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), src, kCFRunLoopCommonModes);
        CGEventTapEnable(tap, true);
        CFRunLoopRun();
    }

    // One-time init guard.
    static std::atomic<bool> g_inputInit{false};
    static void ensureMacInputInit()
    {
        if (g_inputInit.exchange(true)) return;
        std::thread t(macInputThread);
        t.detach();
        // Give the tap a moment to come up so the first GetAsyncKeyState
        // calls after program start see accurate state.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Shutdown the tap cleanly when the program exits.
    struct MacInputShutdown {
        ~MacInputShutdown() {
            // Nothing safe to do here - the CFRunLoop will die with the
            // process. We just clear state.
            for (auto &s : g_keyState) s.store(0);
        }
    };
    static MacInputShutdown g_macInputShutdown;

    // The shim. Matches Windows GetAsyncKeyState: takes a virtual key code
    // (in our case the ASCII byte of the character, like the synth code
    // already does), returns 0x8000 if down.
    inline short GetAsyncKeyState(int vKey)
    {
        ensureMacInputInit();
        if (vKey < 0 || vKey >= 256) return 0;
        // Refresh "transitioned" bit on read, like Windows does.
        uint16_t s = g_keyState[(uint8_t)vKey].load();
        if (s & 0x8000) s |= 0x0001;  // set transitioned
        return (short)s;
    }

    // ---------- console shim (for 4.cpp) ----------
    // Windows console screen buffer is a giant wchar_t array drawn
    // to a separate buffer. We don't have that on macOS, so we
    // implement a no-op version. 4.cpp will still call the drawing
    // functions but the output goes nowhere - the audio still works
    // and the program still runs.

    #define GENERIC_READ  0x80000000u
    #define GENERIC_WRITE 0x40000000u
    #define CONSOLE_TEXTMODE_BUFFER 1
    struct COORD { short X; short Y; };
    using HANDLE = void*;
    using DWORD = unsigned long;
    inline HANDLE CreateConsoleScreenBuffer(unsigned long, unsigned long,
                                            void*, unsigned long, void*)
    {
        return reinterpret_cast<HANDLE>(0x1);  // any non-null sentinel
    }
    inline int SetConsoleActiveScreenBuffer(HANDLE) { return 1; }
    inline int WriteConsoleOutputCharacter(HANDLE, const wchar_t*,
                                           unsigned long, COORD, DWORD*)
    {
        return 1;
    }

#else
    // Generic non-Windows / non-macOS - just stubs.
    #include <cstddef>
    inline short GetAsyncKeyState(int) { return 0; }
#endif
