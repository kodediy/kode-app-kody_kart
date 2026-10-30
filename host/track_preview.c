/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file track_preview.c
 * @brief The circuit seen from above, written to a BMP for eyes.
 *
 * The circuit seen from above, for eyes: a 24-bit BMP of the whole world,
 * the centre line's control points marked, and the progress of each printed.
 *     cc ... host/track_preview.c -o tp && ./tp out.bmp
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "palette.h"
#include "track.h"

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    track_t t = { 0 };
    t.tex = malloc(TK_SIZE * TK_SIZE);
    t.prog = malloc(TK_SIZE * TK_SIZE * 2);
    t.off = malloc(TK_SIZE * TK_SIZE);
    t.line = malloc(sizeof(tk_line_t) * TK_MAX_LINE);
    int circuit = 0;
    if ( 2 < argc )
    {
        circuit = atoi(argv[2]) % TK_CIRCUITS;
    }
    if ( !track_build(&t, TK_ALL[circuit]) )
    {
        puts("too long");
        return 1;
    }
    printf("%s: lap %d texels\n", t.def->name, t.length);
    for ( int i = 0; i < t.def->n_pts; i++ )
    {
        const tk_pt_t *p = &t.def->pts[i];
        printf("  P%-2d (%4d,%4d) s=%u\n", i, p->x, p->y, track_prog(&t, p->x, p->y));
    }
    uint32_t *img = malloc(TK_SIZE * TK_SIZE * 4);
    for ( int i = 0; TK_SIZE * TK_SIZE > i; i++ )
    {
        img[i] = PAL_GROUND[t.tex[i]];
    }
    /* Every hundredth texel of the lap: a black dot; control points: a blue cross. */
    for ( int s = 0; s < t.length; s += 100 )
    {
        const tk_line_t *l = track_at(&t, s);
        img[(int)l->y * TK_SIZE + (int)l->x] = 0;
    }
    for ( int i = 0; i < t.def->n_pts; i++ )
    {
        for ( int k = -4; 4 >= k; k++ )
        {
            const tk_pt_t *p = &t.def->pts[i];
            img[p->y * TK_SIZE + p->x + k] = PAL_KODY_BLUE;
            img[(p->y + k) * TK_SIZE + p->x] = PAL_KODY_BLUE;
        }
    }
    const char *out = "track.bmp";
    if ( 1 < argc )
    {
        out = argv[1];
    }
    FILE *f = fopen(out, "wb");
    const uint32_t row = TK_SIZE * 3, size = 54 + row * TK_SIZE;
    fputc('B', f); fputc('M', f); put32(f, size); put32(f, 0); put32(f, 54);
    put32(f, 40); put32(f, TK_SIZE); put32(f, TK_SIZE); put16(f, 1); put16(f, 24); put32(f, 0); put32(f, row * TK_SIZE);
    put32(f, 2835); put32(f, 2835); put32(f, 0); put32(f, 0);
    for ( int y = TK_SIZE - 1; 0 <= y; y-- )
    {
        for ( int x = 0; TK_SIZE > x; x++ )
        {
            const uint32_t c = img[y * TK_SIZE + x];
            fputc(c & 255, f);
            fputc(c >> 8 & 255, f);
            fputc(c >> 16 & 255, f);
        }
    }
    fclose(f);
    return 0;
}
