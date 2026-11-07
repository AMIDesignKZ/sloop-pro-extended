/* SPDX-License-Identifier: GPL-3.0-only
 * Shared MIDI channel collision resolver (UI and focused host regression). */
UI_SMALL static void midi_chan_fix(void)
{
#if BOARD_SMK37
    static const uint8_t ID[6] = {G_CH1, G_CH2, G_CH3, G_CH4, G_CH5, G_DRCH};
    static int16_t was[6] = {-1, -1, -1, -1, -1, -1};
#else
    static const uint8_t ID[4] = {G_CH1, G_CH2, G_CH3, G_DRCH};
    static int16_t was[4] = {-1, -1, -1, -1};
#endif
    uint32_t k, j, count = (uint32_t)(sizeof ID / sizeof ID[0]);
    for (k = 0; k < count; k++) {
        int32_t v = song.g[ID[k]], dir, owner = -1;
        if (was[k] < 0 || v == was[k] || !v) {
            was[k] = (int16_t)v;
            continue;
        }
        dir = v > was[k] ? 1 : -1;
        for (;;) {
            int32_t o = -1;
            for (j = 0; j < count; j++)
                if (j != k && song.g[ID[j]] == v)
                    o = (int32_t)j;
            if (o < 0)
                break;
            if (owner < 0)
                owner = o;
            v += dir;
            if (v < 1) {
                v = 0;
                break;
            }
            if (v > 16) {
                v = was[k];
                break;
            }
        }
        if (owner >= 0) {
            char b[8];
            str_cpy(b, "CH ", sizeof b);
            fmt_int(b + 3, song.g[ID[k]]);
            ui_say(b, owner == count - 1u ? " = DRUMS" : owner == 0 ? " = TRACK 1" : owner == 1 ? " = TRACK 2" :
                        owner == 2 ? " = TRACK 3" : owner == 3 ? " = TRACK 4" : " = TRACK 5");
            song.g[ID[k]] = (int16_t)v;
        }
        was[k] = (int16_t)v;
    }
}
