/*
 * audio.c  -  sound input from WLED's AudioReactive "Audio Sync" (V2)
 *
 * PACKET (44 bytes, little-endian; checked against live packets)
 *    0 char[6] "00002\0"     6 pressure, 2 bytes (unused)
 *    8 float sampleRaw      12 float sampleSmth (smoothed volume)
 *   16 uint8 samplePeak     17 uint8 frame counter
 *   18 uint8 fftResult[16], bass to treble
 *   34 uint16 zero crossings (unused)
 *   36 float FFT_Magnitude  40 float FFT_MajorPeak (Hz)
 * Floats are copied straight out: the Pi and PCs are little-endian too.
 * samplePeak is ignored: on real music it fires far too often to be a beat.
 * If two devices send at once, the first one heard is followed until it
 * has been quiet for a second, so the two streams are never mixed.
 *
 * WHAT THE MUSIC LOOKS LIKE (one minute of Billie Jean, tools/audio)
 *   The bass bands are lit almost all the time, so a loud bass is not a
 *   beat: bass is only a smooth level. The clean beat is a sharp jump in
 *   bands 13-15 together (snare and hi-hat). Bands 7-12 are nearly always
 *   empty, so each group is scaled to its own recent loudest, with a floor
 *   so quiet noise is never stretched up to full.
 */
#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET au_sock;
#define AU_NONE INVALID_SOCKET
#define au_close closesocket
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
typedef int au_sock;
#define AU_NONE (-1)
#define au_close close
#endif

#define PKT_LEN      44
#define FLOOR        40.0f    /* the smallest "loudest level", out of 255   */
#define PEAK_HALF    8.0f     /* seconds for the loudest level to halve     */
#define DROPOUT      1.0f     /* seconds without packets before easing to 0 */
#define QUIET        1.5f     /* seconds of silence before 'live' falls     */
#define ATTACK       0.04f    /* easing time constants, seconds             */
#define RELEASE_BASS 0.30f
#define RELEASE      0.15f
#define BEAT_DECAY   0.25f
#define BEAT_RISE    0.25f    /* beat: air jumps by this much of its loudest */
#define BEAT_MIN     0.30f    /* ...and is at least this loud               */
#define BEAT_HOLD    0.30f    /* seconds before another beat may count      */
#define TEMPO_GAPS   12
#define TEMPO_LOST   4.0f     /* seconds without a beat before bpm fades    */

/* the four groups: first band and count */
static const int GROUP_LO[4] = { 0, 3, 7, 13 };
static const int GROUP_N[4]  = { 3, 4, 6, 3 };

static struct {
    au_sock sock;
    int     open;
    char    err[96];
    unsigned char bands[AUDIO_BANDS];  /* newest packet */
    float   smth;
    int     counter;                   /* last frame counter, -1 = none */
    struct sockaddr_in src;            /* the sender being followed */
    float   last_rx, last_sound;       /* when the last packet / sound came */
    float   peak_mean[4], peak_max[4], peak_level;
    float   air_fast;                  /* fast-smoothed air, per packet */
    int     beat_now;
    float   last_beat;
    float   gaps[TEMPO_GAPS];
    int     ngaps, gap_i;
    float   phase_fix;                 /* phase correction still to apply */
    int     rx_count;
    float   pps_t;
} S = { AU_NONE, 0, "not opened", { 0 }, 0, -1, { 0 }, -100, -100,
        { FLOOR, FLOOR, FLOOR, FLOOR }, { FLOOR, FLOOR, FLOOR, FLOOR }, FLOOR,
        0, 0, -100, { 0 }, 0, 0, 0, 0, 0 };

const char *audio_error(void) { return S.err; }

int audio_open(int port, const char *group)
{
    if (S.open) return 0;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        snprintf(S.err, sizeof S.err, "Winsock would not start");
        return -1;
    }
#endif
    unsigned long g = inet_addr(group);
    if (g == INADDR_NONE || !IN_MULTICAST(ntohl(g))) {
        snprintf(S.err, sizeof S.err, "%s is not a multicast group", group);
        return -1;
    }
    au_sock s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == AU_NONE) {
        snprintf(S.err, sizeof S.err, "no socket");
        return -1;
    }
    /* let other listeners (another program, a test) share the port */
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) {
        snprintf(S.err, sizeof S.err, "port %d is busy", port);
        au_close(s);
        return -1;
    }
    struct ip_mreq m;
    memset(&m, 0, sizeof m);
    m.imr_multiaddr.s_addr = g;
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&m, sizeof m) != 0) {
        snprintf(S.err, sizeof S.err, "could not join %s (no network?)", group);
        au_close(s);
        return -1;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    BOOL off = FALSE;
    DWORD got = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &got, NULL, NULL);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    S.sock = s;
    S.open = 1;
    S.err[0] = 0;
    return 0;
}

void audio_close(void)
{
    if (!S.open) return;
    au_close(S.sock);
    S.sock = AU_NONE;
    S.open = 0;
#ifdef _WIN32
    WSACleanup();
#endif
}

static float group_mean(const unsigned char *b, int g)
{
    int sum = 0;
    for (int i = 0; i < GROUP_N[g]; i++) sum += b[GROUP_LO[g] + i];
    return (float)sum / (float)GROUP_N[g];
}

/* one packet: raise the loudest levels and look for a beat. Every packet
 * goes through here, so a hit inside a burst is never missed. */
static void take_packet(const unsigned char *p, float now)
{
    memcpy(S.bands, p + 18, AUDIO_BANDS);
    memcpy(&S.smth, p + 12, sizeof S.smth);
    if (!(S.smth >= 0.0f)) S.smth = 0.0f;        /* also catches NaN */
    if (S.smth > 255.0f) S.smth = 255.0f;
    if (S.smth > S.peak_level) S.peak_level = S.smth;

    int sound = S.smth > 0.0f;
    for (int g = 0; g < 4; g++) {
        float mean = group_mean(S.bands, g);
        if (mean > S.peak_mean[g]) S.peak_mean[g] = mean;
        for (int i = 0; i < GROUP_N[g]; i++) {
            float v = S.bands[GROUP_LO[g] + i];
            if (v > S.peak_max[g]) S.peak_max[g] = v;
            if (v > 0) sound = 1;
        }
    }
    if (sound) S.last_sound = now;

    float air = group_mean(S.bands, 3), pk = S.peak_mean[3];
    if (air - S.air_fast > BEAT_RISE * pk && air > BEAT_MIN * pk &&
        now - S.last_beat >= BEAT_HOLD) {
        float gap = now - S.last_beat;
        if (gap >= 0.33f && gap <= 1.5f) {
            S.gaps[S.gap_i] = gap;
            S.gap_i = (S.gap_i + 1) % TEMPO_GAPS;
            if (S.ngaps < TEMPO_GAPS) S.ngaps++;
        }
        S.last_beat = now;
        S.beat_now = 1;
    }
    S.air_fast = S.air_fast * 0.6f + air * 0.4f;
}

/* the typical gap between beats: the mean of the gaps near the median.
 * The median throws out missed and extra beats; the mean then smooths away
 * the jitter of beats only being seen once a frame. */
static float typical_gap(void)
{
    float g[TEMPO_GAPS];
    memcpy(g, S.gaps, sizeof g);
    for (int i = 1; i < S.ngaps; i++)            /* insertion sort, 12 at most */
        for (int j = i; j > 0 && g[j] < g[j - 1]; j--) {
            float t = g[j]; g[j] = g[j - 1]; g[j - 1] = t;
        }
    float med = g[S.ngaps / 2], sum = 0.0f;
    int n = 0;
    for (int i = 0; i < S.ngaps; i++)
        if (fabsf(g[i] - med) < 0.15f * med) { sum += g[i]; n++; }
    return sum / (float)n;                       /* n >= 1: med itself */
}

/* move v towards target: quickly when rising, at 'release' when falling */
static void ease(float *v, float target, float release, float dt)
{
    float tau = target > *v ? ATTACK : release;
    *v += (target - *v) * (1.0f - expf(-dt / tau));
}

static float scaled(float v, float peak)
{
    v /= peak;
    return v > 1.0f ? 1.0f : v;
}

void audio_update(Audio *a, float now, float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;                  /* after a stall, don't leap */
    S.beat_now = 0;

    if (S.open) {
        unsigned char p[128];
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n;
        while ((n = (int)recvfrom(S.sock, (char *)p, (int)sizeof p, 0,
                                  (struct sockaddr *)&from, &fl)) > 0) {
            fl = sizeof from;
            if (n < PKT_LEN || memcmp(p, "00002", 5) != 0) continue;
            int same = from.sin_addr.s_addr == S.src.sin_addr.s_addr &&
                       from.sin_port == S.src.sin_port;
            if (now - S.last_rx < DROPOUT) {
                if (!same) continue;                  /* another sender */
                if (p[17] == S.counter) continue;     /* a repeat */
            }
            S.src = from;
            S.counter = p[17];
            S.last_rx = now;
            S.rx_count++;
            take_packet(p, now);
        }
    }
    if (now - S.pps_t >= 1.0f) {
        a->pps = S.rx_count;
        S.rx_count = 0;
        S.pps_t = now;
    }

    /* the loudest levels sink slowly, never below the floor */
    float sink = exp2f(-dt / PEAK_HALF);
    for (int g = 0; g < 4; g++) {
        S.peak_mean[g] = fmaxf(FLOOR, S.peak_mean[g] * sink);
        S.peak_max[g]  = fmaxf(FLOOR, S.peak_max[g] * sink);
    }
    S.peak_level = fmaxf(FLOOR, S.peak_level * sink);

    /* targets from the newest packet; all 0 once the packets stop */
    int on = now - S.last_rx < DROPOUT;
    if (!on) S.air_fast = 0.0f;
    float *grp[4] = { &a->bass, &a->body, &a->mid, &a->air };
    for (int g = 0; g < 4; g++) {
        float rel = g == 0 ? RELEASE_BASS : RELEASE;
        ease(grp[g], on ? scaled(group_mean(S.bands, g), S.peak_mean[g]) : 0.0f, rel, dt);
        for (int i = 0; i < GROUP_N[g]; i++) {
            int b = GROUP_LO[g] + i;
            ease(&a->band[b], on ? scaled(S.bands[b], S.peak_max[g]) : 0.0f, rel, dt);
        }
    }
    ease(&a->level, on ? scaled(S.smth, S.peak_level) : 0.0f, RELEASE, dt);
    {   /* live rises over about a second and falls more slowly */
        float target = on && now - S.last_sound < QUIET ? 1.0f : 0.0f;
        a->live += (target - a->live) * (1.0f - expf(-dt / (target > a->live ? 0.4f : 0.8f)));
    }

    /* beat: leap to 1, ease back */
    a->beat *= expf(-dt / BEAT_DECAY);
    if (S.beat_now) a->beat = 1.0f;

    /* tempo: the median gap, folded into 70-180 BPM, eased */
    float bpm_target = 0.0f;
    if (now - S.last_beat > TEMPO_LOST) S.ngaps = 0;
    else if (on && S.ngaps >= 3) {
        bpm_target = 60.0f / typical_gap();
        while (bpm_target < 70.0f) bpm_target *= 2.0f;
        while (bpm_target > 180.0f) bpm_target *= 0.5f;
    }
    if (a->bpm < 1.0f && bpm_target > 0.0f) a->bpm = bpm_target;  /* first estimate */
    else a->bpm += (bpm_target - a->bpm) * (1.0f - expf(-dt / 2.0f));
    if (bpm_target == 0.0f && a->bpm < 40.0f) a->bpm = 0.0f;

    /* phase runs at bpm; a beat pulls it towards 0 over a few frames
     * rather than resetting it */
    if (S.beat_now && a->bpm > 0.0f) {
        float err = a->phase > 0.5f ? a->phase - 1.0f : a->phase;
        S.phase_fix = -err * 0.5f;
    }
    float fix = S.phase_fix * fminf(1.0f, dt / 0.15f);
    S.phase_fix -= fix;
    a->phase += dt * a->bpm / 60.0f + fix;
    a->phase -= floorf(a->phase);
}
