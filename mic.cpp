#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <vector>

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <process.h>

#pragma comment(lib, "winmm.lib")

//#define USE_FLOAT_SND

#ifdef USE_FLOAT_SND
    typedef float t_snd;
#else
    typedef short t_snd;
#endif

inline float bound(float value) {
    if (value >= 1.0f)
        return 1.0f;
    if (value <= -1.0f)
        return -1.0f;
    return value;
}

inline t_snd float2snd(float value) {
#ifdef USE_FLOAT_SND
    return bound(value);
#else
    if (value >= 1.0f)
        return (SHORT)MAXSHORT;
    if (value <= -1.0f)
        return (SHORT)MINSHORT;
    return (t_snd)(value * 0x7FFF);
#endif
}

inline float snd2float(t_snd value) {
#ifdef USE_FLOAT_SND
    return bound(value);
#else
    if (value >= (SHORT)MAXSHORT)
        return 1.0f;
    if (value <= (SHORT)MINSHORT)
        return -1.0f;
    return float(value) / 0x7FFF;
#endif
}

typedef void (*SoundFillFunc)(void *dest, void *src, int samples);

enum {
    Bits = (sizeof(t_snd) * 8),
    Channel = (2),
    Freq = (44100),
    Align = ((Channel * Bits) / 8),
    BytePerSec = (Freq * Align),
    BufNum = 3,
    Samples = 1024,
};

struct SoundCallback {
    SoundFillFunc inf;
    SoundFillFunc outf;
};

static VOID SoundThreadProc(void *func) {
    enum {
        SoundIn = 0,
        SoundOut,
        SoundMax,
    };
    SoundCallback *pcb = (SoundCallback *)func;
    HWAVEIN hwi = NULL;
    HWAVEOUT hwo = NULL;
    DWORD countin = 0, countout = 0;
    SoundFillFunc infill = pcb->inf, outfill = pcb->outf;
    HANDLE ahEvents[SoundMax] = {
        CreateEvent(NULL, FALSE, FALSE, NULL),
        CreateEvent(NULL, FALSE, FALSE, NULL),
    };

    WAVEFORMATEX wfx = {
#ifdef USE_FLOAT_SND
                        WAVE_FORMAT_IEEE_FLOAT,
#else
                        WAVE_FORMAT_PCM,
#endif
                        Channel,
                        Freq,
                        BytePerSec,
                        Align,
                        Bits,
                        0};
    WAVEHDR whdrin[BufNum], whdrout[BufNum];

    waveInOpen(&hwi, WAVE_MAPPER, &wfx, (DWORD_PTR)ahEvents[SoundIn], 0,
               CALLBACK_EVENT);
    waveOutOpen(&hwo, WAVE_MAPPER, &wfx, (DWORD_PTR)ahEvents[SoundOut], 0,
                CALLBACK_EVENT);
    std::vector<std::vector<char> > soundbuffer;
    std::vector<std::vector<char> > soundbufferout;
    soundbuffer.resize(BufNum);
    soundbufferout.resize(BufNum);
    for (int i = 0; i < BufNum; i++) {
        soundbuffer[i].resize(Samples * wfx.nBlockAlign);
        soundbufferout[i].resize(Samples * wfx.nBlockAlign);
        WAVEHDR tempin = {&soundbuffer[i][0],
                          (DWORD)(Samples * wfx.nBlockAlign),
                          0,
                          0,
                          0,
                          0,
                          NULL,
                          0};
        WAVEHDR tempout = {&soundbufferout[i][0],
                           (DWORD)(Samples * wfx.nBlockAlign),
                           0,
                           0,
                           0,
                           0,
                           NULL,
                           0};
        whdrin[i] = tempin;
        whdrout[i] = tempout;
        waveInPrepareHeader(hwi, &whdrin[i], sizeof(WAVEHDR));
        waveInAddBuffer(hwi, &whdrin[i], sizeof(WAVEHDR));
    }

    // Record Start
    waveInStart(hwi);

    // Start MSG
    MSG msg;
    for (;;) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
        }

        DWORD dwSignal = WaitForMultipleObjects(SoundMax, ahEvents, FALSE, 1000);

        // IN
        if (dwSignal == WAIT_OBJECT_0 + 0) {
            if (whdrin[countin].dwFlags & WHDR_DONE) {
                infill(whdrout[countin].lpData, whdrin[countin].lpData,
                       whdrin[countin].dwBufferLength);
                waveOutPrepareHeader(hwo, &whdrout[countout], sizeof(WAVEHDR));
                waveOutWrite(hwo, &whdrout[countout], sizeof(WAVEHDR));
                waveInPrepareHeader(hwi, &whdrin[countin], sizeof(WAVEHDR));
                waveInAddBuffer(hwi, &whdrin[countin], sizeof(WAVEHDR));
                countout = (countout + 1) % BufNum;
                countin = (countin + 1) % BufNum;
            }
        }

        // OUT
        if (dwSignal == WAIT_OBJECT_0 + 1) {
            ;
        }
    }

    // in
    do {
        countin = 0;
        for (int i = 0; i < BufNum; i++)
            countin += !(whdrin[i].dwFlags & WHDR_DONE);
        if (countin) Sleep(50);
    } while (countin);

    // out
    do {
        countout = 0;
        for (int i = 0; i < BufNum; i++)
            countout += !(whdrout[i].dwFlags & WHDR_DONE);
        if (countout) Sleep(50);
    } while (countout);

    for (int i = 0; i < BufNum; i++) {
        waveInUnprepareHeader(hwi, &whdrin[i], sizeof(WAVEHDR));
        waveOutUnprepareHeader(hwo, &whdrout[i], sizeof(WAVEHDR));
    }

    waveInReset(hwi);
    waveOutReset(hwo);
    waveInClose(hwi);
    waveOutClose(hwo);

    for (int i = 0; i < SoundMax; i++) {
        if (ahEvents[i]) CloseHandle(ahEvents[i]);
    }

    delete pcb;
}

static HANDLE SoundInit(SoundFillFunc inFunc, SoundFillFunc outFunc) {
    SoundCallback *pcb = new SoundCallback{inFunc, outFunc};
    return (HANDLE)_beginthread(SoundThreadProc, 0, pcb);
}

static void SoundTerm(HANDLE hThread) {
    if (!hThread) return;
    PostThreadMessage(GetThreadId(hThread), WM_QUIT, 0, 0);
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
}

struct DelayBuffer {
    enum {
        Max = 100000,
    };
    float Buf[Max];
    unsigned long Rate;
    unsigned int Index;
    void Init(unsigned long m) { Rate = m; }
    void Update(float a) { Buf[Index++ % Rate] = a; }
    float Sample(unsigned long n = 0) { return Buf[(Index + n) % Rate]; }
};

// http://www.ari-web.com/service/soft/reverb-2.htm
struct Reverb {
    enum {
        CombMax = 8,
        AllMax = 2,
    };
    DelayBuffer comb[CombMax], all[AllMax];

    float Sample(float a, int index = 0, int character = 0, int lpfnum = 4) {
        const int tau[][4][4] = {
                                 {
                                     {2063, 1847, 1523, 1277},
                                     {3089, 2927, 2801, 2111},
                                     {5479, 5077, 4987, 4057},
                                     {9929, 7411, 4951, 1063},
                                 },
                                 {
                                     {2053, 1867, 1531, 1259},
                                     {3109, 2939, 2803, 2113},
                                     {5477, 5059, 4993, 4051},
                                     {9949, 7393, 4957, 1097},
                                 }};

        const float gain[] = {
            -0.8733f,
            -0.8223f,
            -0.8513f,
            -0.8503f,
        };
        float D = a * 0.5f;
        float E = 0;

        // Comb
        for (int i = 0; i < CombMax; i++) {
            DelayBuffer *reb = &comb[i];
            reb->Init(tau[character % 2][index % 4][i]);
            float k = 0;
            float c = 0;
            int LerpMax = lpfnum + 1;
            for (int h = 0; h < LerpMax; h++) k += reb->Sample(h * 2);
            k /= float(LerpMax);
            c = a + k;
            reb->Update(c * gain[i] * 1.1);
            E += c;
        }
        D = (D + E) * 0.3;
        return D;
    }
};

enum STATE {
    STATE_NO_SOUND = 0,
    STATE_NORMAL,
    STATE_REVERB,
    STATE_DELAY,
    STATE_REVERB_AND_DELAY,
};

static Reverb rebL;
static Reverb rebR;
static DelayBuffer delayL;
static DelayBuffer delayR;
static STATE s_state = STATE_NORMAL;
static float s_volume = 1.0f;

// fill callback
static void inFunc(void *dest, void *src, int samples) {
    t_snd *s = (t_snd *)src;
    t_snd *d = (t_snd *)dest;
    samples /= sizeof(t_snd);
    samples /= Channel;

    switch (s_state) {
    case STATE_NO_SOUND: // none
        for (int i = 0; i < samples; i++) {
            d[i * 2 + 0] = 0;
            d[i * 2 + 1] = 0;
        }
        break;
    case STATE_NORMAL: // normal
        for (int i = 0; i < samples; i++) {
            float v = bound(snd2float(s[i * 2 + 0]) * s_volume);
            d[i * 2 + 0] = float2snd(v);
            d[i * 2 + 1] = float2snd(v);
        }
        break;
    case STATE_REVERB: // reverb
        for (int i = 0; i < samples; i++) {
            float v = bound(snd2float(s[i * 2 + 0] * 0.5f) * s_volume);
            d[i * 2 + 0] = float2snd(0.5f * v + (rebL.Sample(v, 0, 0, 4)));
            d[i * 2 + 1] = float2snd(0.5f * v + (rebR.Sample(v, 0, 1, 4)));
        }
        break;
    case STATE_DELAY: // delay
        delayL.Init(15000);
        delayR.Init(15000);
        for (int i = 0; i < samples; i++) {
            float v = bound(snd2float(s[i * 2 + 0]) * s_volume);
            d[i * 2 + 0] = float2snd(delayL.Sample() + v);
            d[i * 2 + 1] = float2snd(delayR.Sample() + v);
            delayL.Update(snd2float(d[i * 2 + 0]) * 0.75f);
            delayR.Update(snd2float(d[i * 2 + 1]) * 0.75f);
        }
        break;
    case STATE_REVERB_AND_DELAY: // reverb and delay
        delayL.Init(20000);
        delayR.Init(20000);
        for (int i = 0; i < samples; i++) {
            float v0 = bound(snd2float(s[i * 2 + 0] * 0.5f) * s_volume);
            float v1 = bound(snd2float(s[i * 2 + 0]) * s_volume);
            float L = delayL.Sample() + v1;
            float R = delayR.Sample() + v1;
            d[i * 2 + 0] = float2snd(0.25f * v0 + L + rebL.Sample(v0, 0, 0, 4));
            d[i * 2 + 1] = float2snd(0.25f * v0 + R + rebR.Sample(v0, 0, 0, 4));
            delayL.Update(L * 0.5f);
            delayR.Update(R * 0.5f);
        }
        break;
    }

    //printf("%3.4f %3.4f\r", abs(d[0]), abs(d[1]));
}

static void outFunc(void *dest, void *src, int samples) {}

static BOOL s_bMic = FALSE;
static BOOL s_bEcho = FALSE;

void micEchoOn(void) {
    s_bEcho = TRUE;
    if (s_bMic)
        s_state = STATE_REVERB_AND_DELAY;
    else
        s_state = STATE_NO_SOUND;
}

void micEchoOff(void) {
    s_bEcho = FALSE;
    if (s_bMic)
        s_state = STATE_REVERB;
    else
        s_state = STATE_NO_SOUND;
}

void micOn(void) {
    s_bMic = TRUE;
    if (s_bEcho)
        s_state = STATE_REVERB_AND_DELAY;
    else
        s_state = STATE_REVERB;
}

void micOff(void) {
    s_bMic = FALSE;
    s_state = STATE_NO_SOUND;
}

void micVolume(float volume) {
    s_volume = volume;
}

HANDLE micStart(BOOL bMicOn) {
    s_bEcho = FALSE;
    if (s_bMic) {
        micOn();
    } else {
        micOff();
    }
    return SoundInit(inFunc, outFunc);
}

void micEnd(HANDLE token) {
    SoundTerm(token);
    s_bMic = FALSE;
}

#ifdef UNITTEST
int main(int argc, char **argv) {
    printf("Key1:Reverb, Key2:Delay, Key3:Normal, Key4:Reverb+Delay, Key5:NoSound\n");
    printf("Press [Esc] to quit\n");
    HANDLE h = micStart(TRUE);
    micVolume(2.0f);
    while (GetAsyncKeyState(VK_ESCAPE) >= 0) {
        if (GetAsyncKeyState('1') < 0) {
            s_state = STATE_REVERB;
        } else if (GetAsyncKeyState('2') < 0) {
            s_state = STATE_DELAY;
        } else if (GetAsyncKeyState('3') < 0) {
            s_state = STATE_NORMAL;
        } else if (GetAsyncKeyState('4') < 0) {
            s_state = STATE_REVERB_AND_DELAY;
        } else if (GetAsyncKeyState('5') < 0) {
            s_state = STATE_NO_SOUND;
        }
        Sleep(16);
    }
    micEnd(h);
    return 0;
}
#endif
