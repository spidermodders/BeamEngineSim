// Replaces BeamNG's BeamNGBlendLayerDSP.dll, which BeamNG loads as an FMOD plugin at startup.
// The stock pass-through description is still exported. On top of that, FMOD's createSound,
// playSound, createDSP, setChannelGroup and addDSP are located inside the statically linked exe
// through the API-name strings FMOD keeps for its error callback (string, LEA xref, unwind entry)
// and hooked. When one of the mod's silent marker samples plays into an event's channel group, a
// DSP that outputs engine-sim's audio (UDP 4571, "BESF" + float32 mono 44.1 kHz) goes at its head.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <share.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// FMOD 2.x plugin ABI (pluginsdkversion 110), layout checked against the stock BeamNG plugins

struct FmodDspState;
struct FmodDspStateFunctions {
    void *alloc, *realloc, *free;
    int(__stdcall *getsamplerate)(FmodDspState *, int *);
};
struct FmodDspState {
    void *instance;
    void *plugindata;
    unsigned int channelmask;
    int source_speakermode;
    float *sidechaindata;
    int sidechainchannels;
    FmodDspStateFunctions *functions;
    int systemobject;
};
struct FmodDspBufferArray {
    int numbuffers;
    int *buffernumchannels;
    unsigned int *bufferchannelmask;
    float **buffers;
    int speakermode;
};
typedef int(__stdcall *StateCb)(FmodDspState *);
typedef int(__stdcall *ProcessCb)(FmodDspState *, unsigned int, const FmodDspBufferArray *, FmodDspBufferArray *, int, int);
struct FmodDspDescription {
    unsigned int pluginsdkversion;
    char name[32];
    unsigned int version;
    int numinputbuffers;
    int numoutputbuffers;
    StateCb create, release, reset;
    void *read;
    ProcessCb process;
    void *setposition;
    int numparameters;
    void **paramdesc;
    void *setters[4];
    void *getters[4];
    void *shouldiprocess, *userdata, *sys_register, *sys_deregister, *sys_mix;
};
static_assert(sizeof(FmodDspDescription) == 216, "FMOD_DSP_DESCRIPTION layout");

enum {
    FMOD_OK = 0,
    PROCESS_PERFORM = 0,
    FMOD_OPENUSER = 0x400,
    FMOD_OPENMEMORY = 0x800,
    FMOD_OPENMEMORY_POINT = 0x10000000,
    DSP_INDEX_HEAD = -1,
};

// FMOD C++ API, x64: `this` is the first argument.
typedef int (*CreateSoundFn)(void *system, const char *name, unsigned int mode, void *exinfo, void **sound);
typedef int (*PlaySoundFn)(void *system, void *sound, void *group, bool paused, void **channel);
typedef int (*SetChannelGroupFn)(void *channel, void *group);
typedef int (*CreateDspFn)(void *system, const FmodDspDescription *description, void **dsp);
typedef int (*AddDspFn)(void *channelControl, int index, void *dsp);


static FILE *s_log = nullptr;
static void logf(const char *fmt, ...) {
    if (s_log == nullptr) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(s_log, fmt, args);
    va_end(args);
    fflush(s_log);
}


static const int RingSize = 1 << 16;
static float s_ring[RingSize];
static std::atomic<long long> s_written{0};
static std::atomic<unsigned long long> s_audioTime{0};

static void receiverThread() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(4571);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(s, (sockaddr *)&addr, sizeof(addr)) != 0) {
        logf("bind 4571 failed: %d\n", WSAGetLastError());
        return;
    }

    char buffer[8192];
    while (true) {
        const int n = recv(s, buffer, sizeof(buffer), 0);
        if (n < 4 || std::memcmp(buffer, "BESF", 4) != 0) continue;
        const int count = (n - 4) / 4;
        const long long w = s_written.load();
        for (int i = 0; i < count; ++i) std::memcpy(&s_ring[(w + i) & (RingSize - 1)], buffer + 4 + 4 * i, 4);
        s_written.store(w + count);
        s_audioTime.store(GetTickCount64());
    }
}

// Kaiser-windowed sinc resampler, 44.1 kHz to the mixer rate

static const int SincTaps = 32, SincPhases = 1024;
static float s_sinc[SincPhases + 1][SincTaps];

static double besselI0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 32; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; }
    return sum;
}

static void buildSinc() {
    const double pi = 3.14159265358979323846, cutoff = 0.96, beta = 9.0;
    for (int p = 0; p <= SincPhases; ++p) {
        double sum = 0;
        for (int k = 0; k < SincTaps; ++k) {
            const double x = (k - SincTaps / 2 + 1) - (double)p / SincPhases;
            const double r = x / (SincTaps / 2);
            const double window = (std::fabs(r) <= 1) ? besselI0(beta * std::sqrt(1 - r * r)) / besselI0(beta) : 0;
            const double sinc = (x == 0) ? 1 : std::sin(pi * cutoff * x) / (pi * cutoff * x);
            s_sinc[p][k] = (float)(cutoff * sinc * window);
            sum += s_sinc[p][k];
        }
        for (int k = 0; k < SincTaps; ++k) s_sinc[p][k] = (float)(s_sinc[p][k] / sum);
    }
}

static float sincSample(double pos) {
    const long long i0 = (long long)pos;
    const double phase = (pos - i0) * SincPhases;
    const int p = (int)phase;
    const float t = (float)(phase - p);
    const float *a = s_sinc[p], *b = s_sinc[p + 1];
    float out = 0;
    for (int k = 0; k < SincTaps; ++k) {
        out += s_ring[(i0 - SincTaps / 2 + 1 + k) & (RingSize - 1)] * (a[k] + (b[k] - a[k]) * t);
    }
    return out;
}


struct Voice {
    double readPos;
    double lagAvg;
    int generation;
};
static std::atomic<int> s_resyncs{0};
static std::atomic<unsigned long long> s_lastStats{0};
// Each marker re-init is a new generation. Only the newest plays engine-sim, older DSPs pass
// their input through, since BeamNG may keep or recycle those groups.
static std::atomic<int> s_generation{0};
static std::atomic<int> s_oldVoiceCalls{0};

static int __stdcall esCreate(FmodDspState *state) {
    state->plugindata = new Voice{ -1, 0, s_generation.load() };
    return FMOD_OK;
}

static int __stdcall esRelease(FmodDspState *state) {
    delete (Voice *)state->plugindata;
    return FMOD_OK;
}

static int __stdcall esProcess(FmodDspState *state, unsigned int length,
    const FmodDspBufferArray *in, FmodDspBufferArray *out, int inputsIdle, int op)
{
    Voice *v = (Voice *)state->plugindata;
    if (op != PROCESS_PERFORM) {
        out->buffernumchannels[0] = in->buffernumchannels[0];
        out->bufferchannelmask[0] = in->bufferchannelmask[0];
        out->speakermode = in->speakermode;
        return FMOD_OK;
    }

    const int channels = out->buffernumchannels[0];
    float *dst = out->buffers[0];

    if (v->generation != s_generation.load()) s_oldVoiceCalls++;
    if (v->generation != s_generation.load() || GetTickCount64() - s_audioTime.load() > 300) {
        if (inputsIdle) std::memset(dst, 0, sizeof(float) * length * channels);
        else std::memcpy(dst, in->buffers[0], sizeof(float) * length * channels);
        v->readPos = -1;
        return FMOD_OK;
    }

    int rate = 48000;
    if (state->functions && state->functions->getsamplerate) state->functions->getsamplerate(state, &rate);

    // Trail the writer by ~50 ms, steering the rate by at most 0.2% on a ~1 s lag average
    const double target = 2048;
    const long long written = s_written.load();
    const double lag = written - v->readPos;
    if (v->readPos < 0 || lag < length * 0.95 + SincTaps || lag > 16384) {
        if (v->readPos >= 0) s_resyncs++;
        v->readPos = written - target;
        v->lagAvg = target;
    }
    v->lagAvg += 0.02 * ((written - v->readPos) - v->lagAvg);
    const double step = 44100.0 / rate * (1.0 + 0.002 * std::fmax(-1.0, std::fmin(1.0, (v->lagAvg - target) / target)));

    float peak = 0;
    for (unsigned int i = 0; i < length; ++i) {
        const float sample = sincSample(v->readPos);
        for (int c = 0; c < channels; ++c) dst[i * channels + c] = sample;
        v->readPos += step;
        peak = std::fmax(peak, std::fabs(sample));
    }

    const unsigned long long now = GetTickCount64();
    if (now - s_lastStats > 10000) {
        s_lastStats = now;
        logf("stats: rate %d ch %d len %u peak %.2f resyncs %d lag %.0f gen %d oldVoiceCalls %d\n", rate, channels, length, peak, s_resyncs.load(), v->lagAvg, s_generation.load(), s_oldVoiceCalls.exchange(0));
    }
    return FMOD_OK;
}

static FmodDspDescription s_esDescription;


static const char *findString(const char *base, const IMAGE_SECTION_HEADER *sec, const char *s) {
    const size_t len = std::strlen(s) + 1;
    const char *p = base + sec->VirtualAddress;
    const char *end = p + sec->Misc.VirtualSize - len;
    for (; p < end; ++p) {
        p = (const char *)std::memchr(p, s[0], end - p);
        if (p == nullptr) return nullptr;
        if (p[-1] == 0 && std::memcmp(p, s, len) == 0) return p;
    }
    return nullptr;
}

static bool resolveFmod(const char *const *names, void **out, int count) {
    char *base = (char *)GetModuleHandleW(nullptr);
    auto *nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    const int sectionCount = nt->FileHeader.NumberOfSections;

    const char *strings[16] = {};
    int xrefs[16] = {};
    for (int n = 0; n < count; ++n) {
        out[n] = nullptr;
        for (int i = 0; i < sectionCount && !strings[n]; ++i) {
            if (!(sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) strings[n] = findString(base, &sections[i], names[n]);
        }
        if (!strings[n]) logf("resolve %s: string not found\n", names[n]);
    }

    for (int i = 0; i < sectionCount; ++i) {
        if (!(sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const unsigned char *code = (const unsigned char *)base + sections[i].VirtualAddress;
        const size_t size = sections[i].Misc.VirtualSize;
        for (size_t k = 0; k + 7 <= size; ++k) {
            if ((code[k] != 0x48 && code[k] != 0x4C) || code[k + 1] != 0x8D || (code[k + 2] & 0xC7) != 0x05) continue;
            int rel;
            std::memcpy(&rel, code + k + 3, 4);
            const char *target = (const char *)code + k + 7 + rel;
            for (int n = 0; n < count; ++n) {
                if (target != strings[n]) continue;
                ++xrefs[n];
                DWORD64 imageBase = 0;
                PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry((DWORD64)(code + k), &imageBase, nullptr);
                while (fn) {
                    const unsigned char *unwind = (const unsigned char *)imageBase + fn->UnwindData;
                    if (!((unwind[0] >> 3) & UNW_FLAG_CHAININFO)) break;
                    fn = (PRUNTIME_FUNCTION)(unwind + 4 + ((unwind[2] + 1) & ~1) * 2);
                }
                if (fn) out[n] = (void *)(imageBase + fn->BeginAddress);
            }
        }
    }

    bool ok = true;
    for (int n = 0; n < count; ++n) {
        logf("resolve %s: %d xref(s) -> %p (exe+0x%llx)\n", names[n], xrefs[n], out[n],
            (unsigned long long)((char *)out[n] - base));
        if (xrefs[n] != 1 || !out[n]) ok = false;
    }
    return ok;
}


static CreateSoundFn s_createSound;
static PlaySoundFn s_playSound;
static SetChannelGroupFn s_setChannelGroup;
static CreateDspFn s_createDsp;
static AddDspFn s_addDsp;

static std::mutex s_mutex;
// shortcut: entries are never removed; a freed group FMOD reuses keeps our old DSP, which
// generations turn into a pass-through. Hook ChannelGroup::release if leaks ever matter.
static std::unordered_set<void *> s_markerSounds, s_markerChannels;
static std::unordered_map<void *, int> s_bridgedGroups;
static unsigned long long s_lastBridgeTick = 0;
static void *s_system = nullptr;

static bool isMarker(const char *name, unsigned int mode, void *exinfo) {
    static const char tag[] = "ENGINESIMMARKER";
    if (mode & (FMOD_OPENMEMORY | FMOD_OPENMEMORY_POINT)) {
        if (!exinfo || !name) return false;
        const unsigned int length = *(const unsigned int *)((const char *)exinfo + 4); // FMOD_CREATESOUNDEXINFO.length
        const unsigned int scan = length < 4096 ? length : 4096;
        for (unsigned int i = 0; i + sizeof(tag) - 1 <= scan; ++i) {
            if (std::memcmp(name + i, tag, sizeof(tag) - 1) == 0) return true;
        }
        return false;
    }
    if (mode & FMOD_OPENUSER || !name) return false;
    return std::strstr(name, "engineSimMarker") != nullptr;
}

static void bridgeGroup(void *group) {
    if (!group || !s_system) return;
    auto it = s_bridgedGroups.find(group);
    if (it != s_bridgedGroups.end() && it->second == s_generation.load()) return;
    const unsigned long long now = GetTickCount64();
    if (now - s_lastBridgeTick > 500) s_generation++;
    s_lastBridgeTick = now;
    void *dsp = nullptr;
    const int created = s_createDsp(s_system, &s_esDescription, &dsp);
    const int added = created == FMOD_OK ? s_addDsp(group, DSP_INDEX_HEAD, dsp) : -1;
    logf("bridge group %p gen %d: createDSP %d addDSP %d\n", group, s_generation.load(), created, added);
    if (added == FMOD_OK) s_bridgedGroups[group] = s_generation.load();
}

static int hookCreateSound(void *system, const char *name, unsigned int mode, void *exinfo, void **sound) {
    const int result = s_createSound(system, name, mode, exinfo, sound);

    static std::atomic<int> logged{0};
    if (logged++ < 40) {
        if (mode & (FMOD_OPENMEMORY | FMOD_OPENMEMORY_POINT)) logf("createSound mode %08x (memory) -> %d\n", mode, result);
        else logf("createSound mode %08x %.120s -> %d\n", mode, (mode & FMOD_OPENUSER) || !name ? "(user)" : name, result);
    }

    if (result == FMOD_OK && sound && isMarker(name, mode, exinfo)) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_system = system;
        s_markerSounds.insert(*sound);
        logf("marker sound %p\n", *sound);
    }
    return result;
}

static int hookPlaySound(void *system, void *sound, void *group, bool paused, void **channel) {
    const int result = s_playSound(system, sound, group, paused, channel);
    if (result != FMOD_OK) return result;

    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_markerSounds.count(sound)) return result;
    static std::atomic<int> logged{0};
    if (logged++ < 20) logf("marker play: group %p channel %p\n", group, channel ? *channel : nullptr);
    if (group) bridgeGroup(group);
    else if (channel) s_markerChannels.insert(*channel);
    return result;
}

static int hookSetChannelGroup(void *channel, void *group) {
    const int result = s_setChannelGroup(channel, group);
    if (result != FMOD_OK) return result;

    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_markerChannels.erase(channel)) {
        logf("marker channel %p -> group %p\n", channel, group);
        bridgeGroup(group);
    }
    return result;
}

static void installHooks() {
    static const char *const names[] = {
        "System::createSound", "System::playSound", "Channel::setChannelGroup", "System::createDSP", "ChannelControl::addDSP",
    };
    void *fns[5];
    if (!resolveFmod(names, fns, 5)) {
        logf("FMOD functions not resolved, bridge disabled (game update?)\n");
        return;
    }
    s_createDsp = (CreateDspFn)fns[3];
    s_addDsp = (AddDspFn)fns[4];

    MH_Initialize();
    logf("hook createSound: %s\n", MH_StatusToString(MH_CreateHook(fns[0], (void *)hookCreateSound, (void **)&s_createSound)));
    logf("hook playSound: %s\n", MH_StatusToString(MH_CreateHook(fns[1], (void *)hookPlaySound, (void **)&s_playSound)));
    logf("hook setChannelGroup: %s\n", MH_StatusToString(MH_CreateHook(fns[2], (void *)hookSetChannelGroup, (void **)&s_setChannelGroup)));
    logf("enable: %s\n", MH_StatusToString(MH_EnableHook(MH_ALL_HOOKS)));
}


static FmodDspDescription s_stubDescription;

extern "C" __declspec(dllexport) FmodDspDescription *__stdcall FMODGetDSPDescription() {
    if (s_stubDescription.pluginsdkversion == 0) {
        char path[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&FMODGetDSPDescription, &self);
        GetModuleFileNameA(self, path, MAX_PATH);
        strcpy_s(std::strrchr(path, '\\') + 1, 32, "engineSimDSP.log");
        s_log = _fsopen(path, "w", _SH_DENYNO);
        logf("FMODGetDSPDescription\n");

        s_stubDescription.pluginsdkversion = 110;
        strcpy_s(s_stubDescription.name, "BeamNGBlendLayer");
        s_stubDescription.version = 0x00010000;
        s_stubDescription.numinputbuffers = 1;
        s_stubDescription.numoutputbuffers = 1;

        s_esDescription = s_stubDescription;
        strcpy_s(s_esDescription.name, "EngineSimBridge");
        s_esDescription.create = esCreate;
        s_esDescription.release = esRelease;
        s_esDescription.process = esProcess;
        buildSinc();

        std::thread(receiverThread).detach();
        installHooks();
    }
    return &s_stubDescription;
}
