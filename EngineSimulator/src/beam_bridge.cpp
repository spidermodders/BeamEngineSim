#include <winsock2.h>
#include <ws2tcpip.h>

#include "../include/beam_bridge.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include <timeapi.h>

// Wire format, little-endian:
//   BeamNG -> engine-sim: "BES1" + 5 floats (BeamBridgeInput order), "BESH" heartbeat while paused
//   engine-sim -> BeamNG: "BES1" + float torqueNm + float rpm ("BESN": keep BeamNG torque)
//                         "BESP" + engine profile floats, about twice a second

static SOCKET s_socket = INVALID_SOCKET;
static std::mutex s_inMutex;
static sockaddr_in s_peer;
static bool s_hasPeer = false;
static BeamBridgeInput s_latest;
static double s_latestTime = -1, s_packetGap = 1 / 60.0;
static const int HistorySize = 64;
static struct { double time; float rpm; } s_history[HistorySize];
static int s_historyCount = 0;

static double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void receiverThread() {
    char buffer[64];
    while (true) {
        sockaddr_in from;
        int fromLength = sizeof(from);
        const int n = recvfrom(s_socket, buffer, sizeof(buffer), 0, (sockaddr *)&from, &fromLength);
        if (n == 4 && std::memcmp(buffer, "BESH", 4) == 0) {
            // Paused game: keep a live link alive, never start one
            std::lock_guard<std::mutex> lock(s_inMutex);
            const double now = nowSeconds();
            if (s_latestTime >= 0 && now - s_latestTime < 0.5) s_latestTime = now;
            continue;
        }
        if (n != 4 + (int)sizeof(BeamBridgeInput) || std::memcmp(buffer, "BES1", 4) != 0) continue; // junk, or WSAECONNRESET

        BeamBridgeInput in;
        std::memcpy(&in, buffer + 4, sizeof(in));
        const double now = nowSeconds();

        std::lock_guard<std::mutex> lock(s_inMutex);
        const double gap = now - s_latestTime;
        if (s_latestTime >= 0 && gap < 0.25) s_packetGap += 0.05 * (gap - s_packetGap);
        s_history[s_historyCount++ % HistorySize] = { now, in.rpm };
        s_latest = in;
        s_latestTime = now;
        s_peer = from;
        s_hasPeer = true;
    }
}

bool beamBridgeInit(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_socket == INVALID_SOCKET) return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(s_socket, (sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(s_socket);
        s_socket = INVALID_SOCKET;
        return false;
    }

    std::thread(receiverThread).detach();

    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    timeBeginPeriod(1);
    return true;
}

bool beamBridgeLatest(BeamBridgeInput *in, double *ageSeconds, float *smoothRpm) {
    std::lock_guard<std::mutex> lock(s_inMutex);
    if (s_latestTime < 0) return false;
    *in = s_latest;
    const double now = nowSeconds();
    *ageSeconds = now - s_latestTime;

    // Extrapolate from the last two packets, at most one packet interval ahead
    *smoothRpm = s_latest.rpm;
    if (s_historyCount >= 2) {
        const auto &b = s_history[(s_historyCount - 1) % HistorySize];
        const auto &a = s_history[(s_historyCount - 2) % HistorySize];
        const double span = b.time - a.time;
        if (span > 0.001 && span < 0.1) {
            const double f = (std::min)((now - a.time) / span, 2.0);
            *smoothRpm = (float)(a.rpm + (b.rpm - a.rpm) * f);
        }
    }
    return true;
}

double beamBridgeRpmRate(double windowSeconds) {
    std::lock_guard<std::mutex> lock(s_inMutex);
    const double from = nowSeconds() - windowSeconds;
    double n = 0, st = 0, sr = 0, stt = 0, str = 0;
    for (int i = 1; i <= (std::min)(s_historyCount, HistorySize); ++i) {
        const auto &p = s_history[(s_historyCount - i) % HistorySize];
        if (p.time < from) break;
        const double t = p.time - from;
        n += 1; st += t; sr += p.rpm; stt += t * t; str += t * p.rpm;
    }
    const double d = n * stt - st * st;
    return (n >= 3 && d > 0) ? (n * str - st * sr) / d : 0;
}

static bool peer(sockaddr_in *out) {
    std::lock_guard<std::mutex> lock(s_inMutex);
    *out = s_peer;
    return s_hasPeer;
}

void beamBridgeSend(float torqueNm, float rpm) {
    sockaddr_in to;
    if (s_socket == INVALID_SOCKET || !peer(&to)) return;

    char buffer[12];
    std::memcpy(buffer, torqueNm == torqueNm ? "BES1" : "BESN", 4);
    std::memcpy(buffer + 4, &torqueNm, 4);
    std::memcpy(buffer + 8, &rpm, 4);
    sendto(s_socket, buffer, sizeof(buffer), 0, (sockaddr *)&to, sizeof(to));
}

void beamBridgeSendProfile(const float *values, int count) {
    sockaddr_in to;
    if (s_socket == INVALID_SOCKET || !peer(&to) || count > 256) return;

    char buffer[4 + 4 * 256];
    std::memcpy(buffer, "BESP", 4);
    std::memcpy(buffer + 4, values, 4 * count);
    sendto(s_socket, buffer, 4 + 4 * count, 0, (sockaddr *)&to, sizeof(to));
}

static std::mutex s_audioMutex;
static std::deque<float> s_audioQueue;

static void audioSenderThread() {
    timeBeginPeriod(1);
    sockaddr_in plugin = {};
    plugin.sin_family = AF_INET;
    plugin.sin_port = htons(4571);
    inet_pton(AF_INET, "127.0.0.1", &plugin.sin_addr);

    const int QueueTarget = 1500;
    char buffer[4 + 4 * 600];
    std::memcpy(buffer, "BESF", 4);
    auto last = std::chrono::steady_clock::now();
    double due = 0;
    while (true) {
        Sleep(2);
        const auto now = std::chrono::steady_clock::now();
        due += std::chrono::duration<double>(now - last).count() * 44100.0;
        last = now;

        int n;
        {
            std::lock_guard<std::mutex> lock(s_audioMutex);
            const int queued = (int)s_audioQueue.size();
            due += (queued > 2 * QueueTarget) ? (queued - 2 * QueueTarget) * 0.05 : 0;
            if (queued < QueueTarget / 2 && due > queued) due = 0;
            n = (int)(std::min)(due, (double)(std::min)(queued, 600));
            for (int i = 0; i < n; ++i) {
                std::memcpy(buffer + 4 + 4 * i, &s_audioQueue.front(), 4);
                s_audioQueue.pop_front();
            }
        }
        due -= n;
        if (due > 4410) due = 4410;
        if (n > 0) sendto(s_socket, buffer, 4 + 4 * n, 0, (sockaddr *)&plugin, sizeof(plugin));
    }
}

void beamBridgeSendAudio(const short *samples, int count, float gain) {
    if (s_socket == INVALID_SOCKET) return;
    static std::once_flag started;
    std::call_once(started, [] { std::thread(audioSenderThread).detach(); });

    std::lock_guard<std::mutex> lock(s_audioMutex);
    for (int i = 0; i < count; ++i) s_audioQueue.push_back(samples[i] * (gain / 32768.0f));
    while (s_audioQueue.size() > 44100) s_audioQueue.pop_front();
}
