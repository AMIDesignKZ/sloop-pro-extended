/* SPDX-License-Identifier: GPL-3.0-only */
/* System 37 hardware UI: the fifth synth channel page and collision handling across
 * five synth parts plus the dedicated drum channel. */
#define BOARD_SMK37 1
#define UI_PAGES_HARNESS_ONLY 1
#define FM1_NBTN 18u
#define FM1_NENC 8u
#define SMK_ENC_MASTER 0u
static uint8_t smk_master_enc;
static int8_t smk_master_dir;
#include "ui_pages_test.c"

int main(void)
{
    const page_t *midi45 = 0;
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, "MIDI 4"))
            midi45 = &PAGES[i];
    assert(NPART == 5u && NTRK == 6u && TRK_DRUM == 5u);
    assert(midi45 && midi45->id[0] == G_CH4 && midi45->id[1] == G_CH5);
    assert(!strcmp(GP[G_CH4].label, "CH4") && !strcmp(GP[G_CH5].label, "CH5"));

    song.g[G_CH1] = 1;
    song.g[G_CH2] = 2;
    song.g[G_CH3] = 5;
    song.g[G_CH4] = 6;
    song.g[G_CH5] = 7;
    song.g[G_DRCH] = 10;
    midi_chan_fix();                 /* establish the initial, non-conflicting channels */

    song.g[G_CH4] = 5;               /* collide with synth track 3; search down to channel 4 */
    midi_chan_fix();
    assert(song.g[G_CH4] == 4);
    assert(!strcmp(ui.msg, "CH 5 = TRACK 3"));

    song.g[G_CH5] = 10;              /* collide with drums; search up to channel 11 */
    midi_chan_fix();
    assert(song.g[G_CH5] == 11);
    assert(!strcmp(ui.msg, "CH 10 = DRUMS"));

    assert(song.g[G_CH1] != song.g[G_CH2] && song.g[G_CH1] != song.g[G_CH3] &&
           song.g[G_CH1] != song.g[G_CH4] && song.g[G_CH1] != song.g[G_CH5] &&
           song.g[G_CH1] != song.g[G_DRCH]);
    assert(song.g[G_CH2] != song.g[G_CH3] && song.g[G_CH2] != song.g[G_CH4] &&
           song.g[G_CH2] != song.g[G_CH5] && song.g[G_CH2] != song.g[G_DRCH]);
    assert(song.g[G_CH3] != song.g[G_CH4] && song.g[G_CH3] != song.g[G_CH5] &&
           song.g[G_CH3] != song.g[G_DRCH]);
    assert(song.g[G_CH4] != song.g[G_CH5] && song.g[G_CH4] != song.g[G_DRCH] &&
           song.g[G_CH5] != song.g[G_DRCH]);
    puts("System 37 UI: MIDI 4/5 page and six-way MIDI channel collision handling OK");
    return 0;
}
