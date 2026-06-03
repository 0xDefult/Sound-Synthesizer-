// olcNoiseMaker.h - cross-platform noise maker
// Originally by OneLoneCoder (Windows winmm), modified to work on macOS too
// On Windows it uses winmm, on macOS it uses CoreAudio via AudioToolbox
// On other platforms it falls back to a no-op stub so things at least compile

#pragma once

#ifndef PI
#define PI 3.14159265358979323846
#endif

#ifndef FTYPE
#define FTYPE double
#endif

#ifdef _WIN32
    // ==== WINDOWS BUILD ====
    #pragma comment(lib, "winmm.lib")

    #include <Windows.h>
    #include <vector>
    #include <string>
    #include <thread>
    #include <atomic>
    #include <condition_variable>
    #include <mutex>
    #include <cmath>
    #include <iostream>
    #include <fstream>
    #include <algorithm>

    template<class T>
    class olcNoiseMaker
    {
    public:
        olcNoiseMaker(std::wstring sOutputDevice, unsigned int nSampleRate = 44100, unsigned int nChannels = 1, unsigned int nBlocks = 8, unsigned int nBlockSamples = 512)
        {
            Create(sOutputDevice, nSampleRate, nChannels, nBlocks, nBlockSamples);
        }

        ~olcNoiseMaker()
        {
            Destroy();
        }

        bool Create(std::wstring sOutputDevice, unsigned int nSampleRate = 44100, unsigned int nChannels = 1, unsigned int nBlocks = 8, unsigned int nBlockSamples = 512)
        {
            m_bReady = false;
            m_nSampleRate = nSampleRate;
            m_nChannels = nChannels;
            m_nBlockCount = nBlocks;
            m_nBlockSamples = nBlockSamples;
            m_nBlockFree = m_nBlockCount;
            m_nBlockCurrent = 0;
            m_pBlockMemory = nullptr;
            m_pWaveHeaders = nullptr;
            m_userFunction = nullptr;

            std::vector<std::wstring> devices = Enumerate();
            auto d = std::find(devices.begin(), devices.end(), sOutputDevice);
            if (d != devices.end())
            {
                int nDeviceID = (int)std::distance(devices.begin(), d);
                WAVEFORMATEX waveFormat;
                waveFormat.wFormatTag = WAVE_FORMAT_PCM;
                waveFormat.nSamplesPerSec = m_nSampleRate;
                waveFormat.wBitsPerSample = sizeof(T) * 8;
                waveFormat.nChannels = m_nChannels;
                waveFormat.nBlockAlign = (waveFormat.wBitsPerSample / 8) * waveFormat.nChannels;
                waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
                waveFormat.cbSize = 0;

                if (waveOutOpen(&m_hwDevice, (UINT)nDeviceID, &waveFormat, (DWORD_PTR)waveOutProcWrap, (DWORD_PTR)this, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
                    return Destroy();
            }

            m_pBlockMemory = new T[m_nBlockCount * m_nBlockSamples];
            if (!m_pBlockMemory) return Destroy();
            ZeroMemory(m_pBlockMemory, sizeof(T) * m_nBlockCount * m_nBlockSamples);

            m_pWaveHeaders = new WAVEHDR[m_nBlockCount];
            if (!m_pWaveHeaders) return Destroy();
            ZeroMemory(m_pWaveHeaders, sizeof(WAVEHDR) * m_nBlockCount);

            for (unsigned int n = 0; n < m_nBlockCount; n++)
            {
                m_pWaveHeaders[n].dwBufferLength = m_nBlockSamples * sizeof(T);
                m_pWaveHeaders[n].lpData = (LPSTR)(m_pBlockMemory + (n * m_nBlockSamples));
            }

            m_bReady = true;
            m_thread = std::thread(&olcNoiseMaker::MainThread, this);
            m_cvBlockNotZero.notify_one();
            return true;
        }

        bool Destroy()
        {
            m_bReady = false;
            if (m_thread.joinable()) m_thread.join();
            if (m_hwDevice) waveOutClose(m_hwDevice);
            if (m_pBlockMemory) { delete[] m_pBlockMemory; m_pBlockMemory = nullptr; }
            if (m_pWaveHeaders) { delete[] m_pWaveHeaders; m_pWaveHeaders = nullptr; }
            return true;
        }

        void Stop() { Destroy(); }

        virtual FTYPE UserProcess(int nChannel, FTYPE dTime) { (void)nChannel; (void)dTime; return 0.0; }

        FTYPE GetTime() { return m_dGlobalTime; }

        static std::vector<std::wstring> Enumerate()
        {
            int nDeviceCount = waveOutGetNumDevs();
            std::vector<std::wstring> sDevices;
            WAVEOUTCAPS woc;
            for (int n = 0; n < nDeviceCount; n++)
                if (waveOutGetDevCaps((UINT)n, &woc, sizeof(WAVEOUTCAPS)) == MMSYSERR_NOERROR)
                    sDevices.push_back(woc.szPname);
            return sDevices;
        }

        void SetUserFunction(FTYPE(*func)(int, FTYPE)) { m_userFunction = func; }

        FTYPE clip(FTYPE dSample, FTYPE dMax)
        {
            if (dSample >= 0.0) return fmin(dSample, dMax);
            else return fmax(dSample, -dMax);
        }

    private:
        FTYPE(*m_userFunction)(int, FTYPE);

        unsigned int m_nSampleRate;
        unsigned int m_nChannels;
        unsigned int m_nBlockCount;
        unsigned int m_nBlockSamples;
        unsigned int m_nBlockCurrent;

        T* m_pBlockMemory;
        WAVEHDR *m_pWaveHeaders;
        HWAVEOUT m_hwDevice;

        std::thread m_thread;
        std::atomic<bool> m_bReady;
        std::atomic<unsigned int> m_nBlockFree;
        std::condition_variable m_cvBlockNotZero;
        std::mutex m_muxBlockNotZero;

        std::atomic<FTYPE> m_dGlobalTime;

        void waveOutProc(HWAVEOUT hWaveOut, UINT uMsg, DWORD_PTR, DWORD_PTR)
        {
            (void)hWaveOut;
            if (uMsg != WOM_DONE) return;
            m_nBlockFree++;
            m_cvBlockNotZero.notify_one();
        }

        static void CALLBACK waveOutProcWrap(HWAVEOUT hWaveOut, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR, DWORD_PTR)
        {
            ((olcNoiseMaker*)dwInstance)->waveOutProc(hWaveOut, uMsg, 0, 0);
        }

        void MainThread()
        {
            m_dGlobalTime = 0.0;
            FTYPE dTimeStep = 1.0 / (FTYPE)m_nSampleRate;
            T nMaxSample = (T)pow(2, (sizeof(T) * 8) - 1) - 1;
            FTYPE dMaxSample = (FTYPE)nMaxSample;

            while (m_bReady)
            {
                if (m_nBlockFree == 0)
                {
                    std::unique_lock<std::mutex> lm(m_muxBlockNotZero);
                    while (m_nBlockFree == 0) m_cvBlockNotZero.wait(lm);
                }

                m_nBlockFree--;
                if (m_pWaveHeaders[m_nBlockCurrent].dwFlags & WHDR_PREPARED)
                    waveOutUnprepareHeader(m_hwDevice, &m_pWaveHeaders[m_nBlockCurrent], sizeof(WAVEHDR));

                T nNewSample = 0;
                int nCurrentBlock = m_nBlockCurrent * m_nBlockSamples;

                for (unsigned int n = 0; n < m_nBlockSamples; n += m_nChannels)
                {
                    for (unsigned int c = 0; c < m_nChannels; c++)
                    {
                        if (m_userFunction == nullptr)
                            nNewSample = (T)(clip(UserProcess((int)c, m_dGlobalTime), 1.0) * dMaxSample);
                        else
                            nNewSample = (T)(clip(m_userFunction((int)c, m_dGlobalTime), 1.0) * dMaxSample);
                        m_pBlockMemory[nCurrentBlock + n + c] = nNewSample;
                    }
                    m_dGlobalTime = m_dGlobalTime + dTimeStep;
                }

                waveOutPrepareHeader(m_hwDevice, &m_pWaveHeaders[m_nBlockCurrent], sizeof(WAVEHDR));
                waveOutWrite(m_hwDevice, &m_pWaveHeaders[m_nBlockCurrent], sizeof(WAVEHDR));
                m_nBlockCurrent = (m_nBlockCurrent + 1) % m_nBlockCount;
            }
        }
    };

#elif defined(__APPLE__)
    // ==== macOS BUILD (CoreAudio) ====
    #include <AudioToolbox/AudioToolbox.h>
    #include <CoreAudio/CoreAudio.h>
    #include <vector>
    #include <string>
    #include <thread>
    #include <atomic>
    #include <mutex>
    #include <cmath>
    #include <iostream>
    #include <algorithm>
    #include <condition_variable>

    // on macOS wcout works but std::wstring is fine. we keep the same surface area
    // so the rest of the code does not need to change.

    template<class T>
    class olcNoiseMaker
    {
    public:
        olcNoiseMaker(std::wstring sOutputDevice, unsigned int nSampleRate = 44100, unsigned int nChannels = 1, unsigned int nBlocks = 8, unsigned int nBlockSamples = 512)
        {
            // device name is mostly ignored on macOS, we use the default output
            (void)sOutputDevice;
            Create(nSampleRate, nChannels, nBlocks, nBlockSamples);
        }

        ~olcNoiseMaker()
        {
            Destroy();
        }

        bool Create(unsigned int nSampleRate = 44100, unsigned int nChannels = 1, unsigned int nBlocks = 8, unsigned int nBlockSamples = 512)
        {
            m_bReady = false;
            m_nSampleRate = nSampleRate;
            m_nChannels = nChannels;
            m_nBlockCount = nBlocks;
            m_nBlockSamples = nBlockSamples;
            m_nBlockFree = m_nBlockCount;
            m_nBlockCurrent = 0;
            m_userFunction = nullptr;
            m_userFunctionSimple = nullptr;

            // Set up an AudioUnit that pulls samples from us (kAudioUnitSubType_HALOutput is for input only,
            // so use kAudioUnitSubType_DefaultOutput which is the default output device).
            AudioComponentDescription desc = {0};
            desc.componentType = kAudioUnitType_Output;
            desc.componentSubType = kAudioUnitSubType_DefaultOutput;
            desc.componentManufacturer = kAudioUnitManufacturer_Apple;
            desc.componentFlags = 0;
            desc.componentFlagsMask = 0;

            AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
            if (!comp) {
                std::cerr << "olcNoiseMaker: could not find default output audio component" << std::endl;
                return false;
            }
            OSStatus status = AudioComponentInstanceNew(comp, &m_audioUnit);
            if (status != noErr) {
                std::cerr << "olcNoiseMaker: AudioComponentInstanceNew failed (" << status << ")" << std::endl;
                return false;
            }

            // we want PCM float output
            AudioStreamBasicDescription fmt = {0};
            fmt.mSampleRate = (Float64)m_nSampleRate;
            fmt.mFormatID = kAudioFormatLinearPCM;
            fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
            fmt.mBitsPerChannel = sizeof(FTYPE) * 8;
            fmt.mChannelsPerFrame = m_nChannels;
            fmt.mBytesPerFrame = sizeof(FTYPE) * m_nChannels;
            fmt.mFramesPerPacket = 1;
            fmt.mBytesPerPacket = sizeof(FTYPE) * m_nChannels;

            status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
                kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
            if (status != noErr) {
                std::cerr << "olcNoiseMaker: could not set stream format (" << status << ")" << std::endl;
                return false;
            }

            // allocate a render callback buffer
            m_renderBuffer.assign(m_nBlockSamples, 0.0f);

            AURenderCallbackStruct cb = {0};
            cb.inputProc = &olcNoiseMaker::RenderCallback;
            cb.inputProcRefCon = this;
            status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_SetRenderCallback,
                kAudioUnitScope_Input, 0, &cb, sizeof(cb));
            if (status != noErr) {
                std::cerr << "olcNoiseMaker: could not set render callback (" << status << ")" << std::endl;
                return false;
            }

            status = AudioUnitInitialize(m_audioUnit);
            if (status != noErr) {
                std::cerr << "olcNoiseMaker: AudioUnitInitialize failed (" << status << ")" << std::endl;
                return false;
            }

            m_bReady = true;
            m_thread = std::thread(&olcNoiseMaker::MainThread, this);

            status = AudioOutputUnitStart(m_audioUnit);
            if (status != noErr) {
                std::cerr << "olcNoiseMaker: AudioOutputUnitStart failed (" << status << ")" << std::endl;
                return false;
            }

            return true;
        }

        bool Destroy()
        {
            m_bReady = false;
            if (m_thread.joinable()) m_thread.join();
            if (m_audioUnit) {
                AudioOutputUnitStop(m_audioUnit);
                AudioUnitUninitialize(m_audioUnit);
                AudioComponentInstanceDispose(m_audioUnit);
                m_audioUnit = nullptr;
            }
            return true;
        }

        void Stop() { Destroy(); }

        virtual FTYPE UserProcess(int nChannel, FTYPE dTime) { (void)nChannel; (void)dTime; return 0.0; }

        FTYPE GetTime() { return m_dGlobalTime.load(); }

        // we cant really enumerate devices the same way on macOS, return a default
        static std::vector<std::wstring> Enumerate()
        {
            std::vector<std::wstring> sDevices;
            sDevices.push_back(L"Default Output");
            return sDevices;
        }

        // support both single-arg and double-arg user functions, like the original
        void SetUserFunction(FTYPE(*func)(int, FTYPE)) { m_userFunction = func; m_userFunctionSimple = nullptr; }
        void SetUserFunction(FTYPE(*func)(FTYPE))      { m_userFunctionSimple = func; m_userFunction = nullptr; }

        FTYPE clip(FTYPE dSample, FTYPE dMax)
        {
            if (dSample >= 0.0) return fmin(dSample, dMax);
            else return fmax(dSample, -dMax);
        }

    private:
        FTYPE(*m_userFunction)(int, FTYPE);
        FTYPE(*m_userFunctionSimple)(FTYPE);

        unsigned int m_nSampleRate;
        unsigned int m_nChannels;
        unsigned int m_nBlockCount;
        unsigned int m_nBlockSamples;
        unsigned int m_nBlockCurrent;

        AudioUnit m_audioUnit;
        std::vector<float> m_renderBuffer;

        std::thread m_thread;
        std::atomic<bool> m_bReady;
        std::atomic<unsigned int> m_nBlockFree;
        std::condition_variable m_cvBlockNotZero;
        std::mutex m_muxBlockNotZero;

        std::atomic<FTYPE> m_dGlobalTime;

        // The CoreAudio render callback runs on its own high-priority thread.
        // We just need to fill the requested buffer with samples from the user function.
        static OSStatus RenderCallback(void *inRefCon,
            AudioUnitRenderActionFlags *ioActionFlags,
            const AudioTimeStamp *inTimeStamp,
            UInt32 inBusNumber,
            UInt32 inNumberFrames,
            AudioBufferList *ioData)
        {
            (void)ioActionFlags; (void)inTimeStamp; (void)inBusNumber;
            olcNoiseMaker *self = (olcNoiseMaker*)inRefCon;
            if (!self) return noErr;

            FTYPE dTime = self->m_dGlobalTime.load();
            FTYPE dTimeStep = 1.0 / (FTYPE)self->m_nSampleRate;

            for (UInt32 n = 0; n < inNumberFrames; n++)
            {
                FTYPE sample = 0.0;
                if (self->m_userFunction) {
                    for (unsigned int c = 0; c < self->m_nChannels; c++) {
                        FTYPE s = self->m_userFunction((int)c, dTime);
                        s = self->clip(s, 1.0);
                        ((float*)ioData->mBuffers[0].mData)[n * self->m_nChannels + c] = (float)s;
                    }
                } else if (self->m_userFunctionSimple) {
                    FTYPE s = self->clip(self->m_userFunctionSimple(dTime), 1.0);
                    for (unsigned int c = 0; c < self->m_nChannels; c++) {
                        ((float*)ioData->mBuffers[0].mData)[n * self->m_nChannels + c] = (float)s;
                    }
                } else {
                    for (unsigned int c = 0; c < self->m_nChannels; c++) {
                        ((float*)ioData->mBuffers[0].mData)[n * self->m_nChannels + c] = 0.0f;
                    }
                }
                dTime += dTimeStep;
            }
            self->m_dGlobalTime.store(dTime);
            return noErr;
        }

        // Background thread: just keeps m_dGlobalTime ticking in case nothing is connected
        void MainThread()
        {
            // Most of the work happens in the audio render callback on macOS.
            // This thread is mostly here to match the surface API of the Windows version.
            while (m_bReady)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    };

#else
    // ==== GENERIC / LINUX STUB ====
    // Just a stub that compiles. No actual audio output.
    #include <vector>
    #include <string>
    #include <thread>
    #include <atomic>
    #include <mutex>
    #include <cmath>
    #include <iostream>
    #include <algorithm>
    #include <condition_variable>

    template<class T>
    class olcNoiseMaker
    {
    public:
        olcNoiseMaker(std::wstring sOutputDevice, unsigned int nSampleRate = 44100, unsigned int nChannels = 1, unsigned int nBlocks = 8, unsigned int nBlockSamples = 512)
        {
            (void)sOutputDevice; (void)nSampleRate; (void)nChannels; (void)nBlocks; (void)nBlockSamples;
        }
        ~olcNoiseMaker() {}
        bool Create(...) { return true; }
        bool Destroy() { return true; }
        void Stop() {}
        virtual FTYPE UserProcess(int, FTYPE) { return 0.0; }
        FTYPE GetTime() { return 0.0; }
        static std::vector<std::wstring> Enumerate() { return {L"Default"}; }
        void SetUserFunction(FTYPE(*func)(int, FTYPE)) { (void)func; }
        void SetUserFunction(FTYPE(*func)(FTYPE)) { (void)func; }
        FTYPE clip(FTYPE dSample, FTYPE dMax) { return dSample >= 0.0 ? fmin(dSample, dMax) : fmax(dSample, -dMax); }
    private:
        std::atomic<FTYPE> m_dGlobalTime{0.0};
    };
#endif
