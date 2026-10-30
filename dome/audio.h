/*
 * audio.h  -  sound input from WLED's AudioReactive "Audio Sync" (V2)
 *
 * One WLED device listens to the room and sends 44-byte packets to the
 * multicast group 239.0.0.1, port 11988, about 86 a second: a volume, and
 * 16 FFT bands from bass to treble. This listens for them (no IP addresses
 * needed: it joins the group on every interface) and turns them into a few
 * smooth values that are easy to draw with.
 *
 * Self-contained: sockets and libc only, no SDL, GL or dome.h, so it can be
 * lifted into another project. Builds on Linux and on Windows (link ws2_32).
 *
 *     audio_open(11988, "239.0.0.1");
 *     each frame:  audio_update(&a, now, dt);   then draw with a.bass etc.
 *
 * Every value eases; nothing jumps except 'beat', which leaps to 1 on a beat
 * and eases back. If packets stop for about a second, everything eases back
 * to 0: it never freezes on the last value.
 *
 * For speed that follows the music, keep your own clock and add to it,
 *     t += dt * speed * (1 + k * a.bass);
 * rather than multiplying the time by a varying speed, which jumps.
 */
#ifndef AUDIO_H
#define AUDIO_H

#define AUDIO_BANDS 16

typedef struct {
    float bass, body, mid, air;   /* 0..1, smoothed, scaled to the loudest
                                     recent level: bands 0-2, 3-6, 7-12, 13-15 */
    float level;                  /* 0..1, overall volume (sampleSmth)          */
    float beat;                   /* 1 on a beat (a snare or hi-hat hit),
                                     easing back to 0 over about 0.3 s          */
    float bpm;                    /* estimated tempo, 0 while unknown           */
    float phase;                  /* 0..1 through the current beat, at bpm      */
    float live;                   /* eases 0..1: is there sound arriving?       */
    float band[AUDIO_BANDS];      /* each band 0..1, smoothed and scaled        */
    int   pps;                    /* packets received in the last second        */
} Audio;

/* Open the socket and join the group. Returns 0 if listening, or -1, with
 * audio_error() saying why. Without it audio_update() just eases to 0. */
int  audio_open(int port, const char *group);
const char *audio_error(void);

/* Read every waiting packet and move the values on by dt seconds. Call once
 * a frame; it never blocks. now is any clock in seconds. */
void audio_update(Audio *a, float now, float dt);

void audio_close(void);

#endif
