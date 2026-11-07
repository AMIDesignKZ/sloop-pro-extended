/* SPDX-License-Identifier: GPL-3.0-only */
/* Focused System 37 regression: five synth MIDI channels plus the drum channel. */
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#define BOARD_SMK37 1
#define UI_SMALL
#include "../firmware/src/core.h"


static char last_msg[64];
static void str_cpy(char *dst, const char *src, unsigned max)
{
    if (!max) return;
    strncpy(dst, src, max - 1u);
    dst[max - 1u] = 0;
}
static void fmt_int(char *dst, int32_t value)
{
    char tmp[16];
    snprintf(tmp, sizeof tmp, "%ld", (long)value);
    strcpy(dst, tmp);
}
static void ui_say(const char *a, const char *b)
{
    snprintf(last_msg, sizeof last_msg, "%s%s", a, b);
}
#include "../firmware/src/midi_channels.h"

int main(void)
{
    assert(NPART == 5u && NTRK == 6u && TRK_DRUM == 5u);
    assert(G_CH4 != G_CH5 && G_CH4 > G_CH3 && G_CH5 > G_CH4);

    song.g[G_CH1] = 1;
    song.g[G_CH2] = 2;
    song.g[G_CH3] = 5;
    song.g[G_CH4] = 6;
    song.g[G_CH5] = 7;
    song.g[G_DRCH] = 10;
    midi_chan_fix();

    song.g[G_CH4] = 5;
    midi_chan_fix();
    assert(song.g[G_CH4] == 4);
    assert(!strcmp(last_msg, "CH 5 = TRACK 3"));

    song.g[G_CH5] = 10;
    midi_chan_fix();
    assert(song.g[G_CH5] == 11);
    assert(!strcmp(last_msg, "CH 10 = DRUMS"));

    assert(song.g[G_CH1] != song.g[G_CH2] && song.g[G_CH1] != song.g[G_CH3] &&
           song.g[G_CH1] != song.g[G_CH4] && song.g[G_CH1] != song.g[G_CH5] &&
           song.g[G_CH1] != song.g[G_DRCH]);
    assert(song.g[G_CH2] != song.g[G_CH3] && song.g[G_CH2] != song.g[G_CH4] &&
           song.g[G_CH2] != song.g[G_CH5] && song.g[G_CH2] != song.g[G_DRCH]);
    assert(song.g[G_CH3] != song.g[G_CH4] && song.g[G_CH3] != song.g[G_CH5] &&
           song.g[G_CH3] != song.g[G_DRCH]);
    assert(song.g[G_CH4] != song.g[G_CH5] && song.g[G_CH4] != song.g[G_DRCH] &&
           song.g[G_CH5] != song.g[G_DRCH]);
    puts("System 37 MIDI: CH4/CH5 and six-way channel collision handling OK");
    return 0;
}
