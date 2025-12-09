/*
 * SPDX-FileCopyrightText: Copyright 2020-2022, 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <math.h>
#include "mlw_common.h"
#include "mlw_encode.h"

#define DPRINTF(...)
//#define DPRINTF(...) printf(__VA_ARGS__)

#define ZERO_RUN_THRES  4

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

#define CHECKED_MALLOC(var, size) { if ( !(var = malloc(size)) ) break; }

typedef struct palette {
    int16_t lut[32];
    int16_t inv_lut[512];
    int palsize;    // number of palette entries
    int palbits;    // bit width of palette entries
    int use_zero_runs;    // zeros are coded separately
    int only_palette;   // no values outside the palette
    int direct_offset;  // added to the decoded weight index before direct conversion to sign/mag
    int only_zeros;     // special case that the section is all zeros
} palette_t;

static int is_power_of_two( int x ) {
    return ((x-1) & x)==0;
}

static int round_up_divide(int num, int den)
{
    return (num + den - 1) / den;
}

static int round_up(int num, int den)
{
    return round_up_divide(num, den) * den;
}

static int get_palette_index_bits( int size ) {
    int i;
    for(i=7; i>=0; i--)
        if (size > (1<<i) )
            return i+1;
    return 0;
}

// Search the stream for suitable palette restart positions
// Return the number of restarts
static int search_palette_sections( int16_t *buf, int size, int **palette_restart_positions ) {
    int i,j,got_palette,restart_i,palette_size=0, last_restart_idx, zero_cnt;
    int prev_idx[512];  // For each value, keep track of the index of the previous occurence
    int *restart_pos;
    int max_palettes = round_up_divide(size, 64);
    *palette_restart_positions = NULL;

    // Preliminary allocation of sufficient size
    restart_pos = (int*)malloc( max_palettes*sizeof(int) );
    if (!restart_pos) {
        return -1;
    }
    last_restart_idx=0;
    got_palette=0;
    restart_i=1;
    restart_pos[0] = 0;
    zero_cnt=0;
    memset(prev_idx, -1, sizeof(prev_idx));
    for(i=0; i<size; i++) {
        // Guess if zeros should be excluded from the palette
        int exclude_zero = zero_cnt > (i-last_restart_idx)/4;

        if (got_palette) {
            // Check if the next value is not covered by the current palette
            if ( prev_idx[ buf[i]+256 ] < last_restart_idx ) {
                // New value: increase the palette size
                palette_size++;
                DPRINTF("Note: at pos %d extend palette to size %d\n", i, palette_size);
                if ( is_power_of_two(palette_size-1-exclude_zero) ) {
                    if ( (i - last_restart_idx - zero_cnt) > 512 || (palette_size-exclude_zero)>32 ) {
                        // create a new palette because we extend a long lasting palette to require one more index bit
                        DPRINTF("Note: at pos %d create new palette because previous has to increase one more index bit. last_restart_idx %d n %d zero_cnt %d\n", i, last_restart_idx, i - last_restart_idx, zero_cnt );
                        if (restart_i == max_palettes) {
                            max_palettes = max_palettes*2;
                            restart_pos = (int*)realloc( restart_pos, max_palettes*sizeof(int) );
                            if (!restart_pos) {
                                return -1;
                            }
                        }
                        DPRINTF("restart %d pos %d\n", restart_i, i);
                        restart_pos[restart_i++] = i;
                        last_restart_idx = i;
                        got_palette=0;
                        zero_cnt=0;
                    }
                }
            }
        }

        prev_idx[ buf[i]+256 ] = i;
        if (buf[i]==0)
            zero_cnt++;

        static const int window_sizes[5][2] = {{32,1}, {64,1}, {128,1}, {256,1}, {512,1}};
        int k;
        // loop over window sizes
        for(k=0; k<5; k++) {
            // Every Nth non-zero value, count what would be the size of a palette covering the last N NZ.
            int N = window_sizes[k][0] * (got_palette?2:1);
            if ( (i - last_restart_idx - zero_cnt) > 0 && ((i - last_restart_idx - zero_cnt) % N)==0 ) {
                // Search backward to the position N nonzero values earlier
                int nzcnt=0;
                for( j=i; j>last_restart_idx; j--) {
                    if ( buf[j]!=0 ) {
                        if (nzcnt==N+1)
                            break;
                        nzcnt++;
                    }
                }
                int restart_idx = j;

                // Calculate the size of a new palette (starting at restart_idx)
                int new_palette_size=0;
                for(j=0; j<512; j++) {
                    if ( prev_idx[j] >= restart_idx ) {
                        new_palette_size++;
                    }
                }

                int create_new_palette=0;
                if (got_palette) {
                    int new_size_bits = get_palette_index_bits( new_palette_size - exclude_zero );
                    int old_size_bits = get_palette_index_bits( palette_size - exclude_zero );
                    int savings = N*(old_size_bits*15-new_size_bits*15)/16 - new_palette_size*8 - 20;
                    if ( savings>0 ) {
                        // Create new palette because it can be smaller than the existing palette
                        create_new_palette=1;
                        DPRINTF("Note: at pos %d restart smaller palette\n", restart_idx);
                    }
                } else {
                    if ( (new_palette_size-exclude_zero) <= 32) {
                        int new_size_bits = get_palette_index_bits( new_palette_size - exclude_zero );
                        // estimate if we will make savings by using palette mode
                        int savings = N*(90-new_size_bits*15)/16 - new_palette_size*8 - 20;
                        create_new_palette = savings>0;
                    }
                }
                if (create_new_palette) {
                    palette_size=new_palette_size;
                    got_palette=1;
                    last_restart_idx = restart_idx;
                    DPRINTF("Note: at pos %d create palette of size %d\n", last_restart_idx, new_palette_size);
                    if ( restart_pos[restart_i-1] != last_restart_idx) {
                        if (restart_i == max_palettes) {
                            max_palettes = max_palettes*2;
                            restart_pos = (int*)realloc( restart_pos, max_palettes*sizeof(int) );
                            if (!restart_pos) {
                                return -1;
                            }
                        }
                        restart_pos[restart_i++] = last_restart_idx;
                    }
                    zero_cnt=0;
                    for( j=last_restart_idx; j<=i; j++)
                        if (buf[j]==0)
                            zero_cnt++;
                }
            }
        }
    }
    // Reallocate to actual size
    *palette_restart_positions = (int*)realloc( restart_pos, restart_i*sizeof(int) );
    return *palette_restart_positions ? restart_i : -1;
}

// Calculate frequency table
static void calc_freq( const int16_t *buf, int size, int freq[512] ) {
    int i;
    memset(freq, 0, 512*sizeof(int));
    for(i=0; i<size; i++) {
        freq[buf[i]+256]++;
    }
}

static int cmp_uint64(const void * a, const void * b) {
   uint64_t aa = *(const uint64_t*)a;
   uint64_t bb = *(const uint64_t*)b;
   return  aa>bb ? -1 : aa<bb ? 1 : 0;
}

// Create palette from the given frequencies
// Freq index 0-511 correspond to weights -256..255
static void create_palette( int freq[512],
                           int use_zero_runs,
                           palette_t *p ) {
    uint64_t freq64[512];
    int i,all_cnt,all_max_val;

    // Pair the frequency with the value so that
    // the array can be sorted on frequency while keeping
    // track of the corresponding palette value
    memset(freq64, 0, sizeof(freq64));
    all_cnt=0;
    all_max_val=0;
    for(i=-255; i<256; i++) {
        if (i==0 && use_zero_runs)
            continue;
        int sign = i<0;
        int mag = abs(i);
        int palval = (mag<<1) | sign;

        // Store palette value in 16 LSB bits, which will not affect the sorting
        freq64[palval] = (((uint64_t)freq[i+256])<<16) | palval;
        all_cnt+=freq[i+256];

        if (freq[i+256]>0) {
            all_max_val = max(all_max_val, palval);
        }
    }

    // Count number of non-used weight values around zero (0, -1, +1, -2, +2 etc)
    for(i=0; i<31; i++) {
        if ((freq64[i]>>16)!=0)
            break;
    }
    p->direct_offset = i;

    // Sort in descending frequency order
    qsort(freq64, 512, sizeof(uint64_t), cmp_uint64);

    // Identify special case that there are no weights to code
    // in the weight index stream (i.e. all weights are zeros)
    p->only_zeros = (freq64[0]>>16)==0;
    if (p->only_zeros) {
        p->direct_offset=0;
    }

    // Check if all weights fit into the palette (and the palette is not empty)
    p->only_palette = (freq64[0]>>16)>0 && (freq64[32]>>16)==0;

    int max_palette_size;
    if (p->only_palette) {
        max_palette_size = 32;
    } else {
        // For direct-lut we must make sure that the encoded weight
        // index is not > 511. We do that by limiting the palette size
        // such that the greatest value can be reached after subtracting
        // the palette size.
        max_palette_size = min(32, 511-all_max_val);
        if (max_palette_size==1) {
            max_palette_size=0; // because palette of size 1 is not supported
        }
    }

    // Setup the 32 entry palette
    int16_t palette_max_val = 0, val;
    int cnt, pal_cnt=0;
    for(i=0; i<max_palette_size; i++) {
        cnt = (int)(freq64[i]>>16);
        val = freq64[i]&0xffff;
        if ( cnt==0 )
            break;
        p->lut[i] = val;
        palette_max_val = max(palette_max_val, val);
        pal_cnt+=cnt;
    }
    if (i==1)
        p->lut[i++] = 0; // palette size of 1 is not supported, make it 2

    // Heuristic for when to use the palette. If more than half of the
    // weights are in the palette then we use it. This ensures we don't
    // use palette for e.g. rectangular distributions.
    int palbits_val;
    if (pal_cnt > all_cnt/2) {
        p->palsize  =  i;
        palbits_val = palette_max_val;
    } else {
        // No palette
        p->palsize  =  0;
        // If no palette, then palbits is used to specify the
        // number of bits required for uncompressed mode, i.e.
        // the number of bits for the greatest weight value
        palbits_val = all_max_val;
    }

    // the palette entry bit width
    // minimum 2bits (because PALBITS is in range 2..9)
    int palbits=2;
    while( (1<<palbits) <= palbits_val )
        palbits++;
    assert(palbits<=9);
    p->palbits  = palbits;
    p->use_zero_runs  = use_zero_runs;
}

// Return 1 if zero runs should be used
// If palette_size is 512, then palette is not used (in that case the palette is setup
// with the standard alternating unsigned to signed mapping)
static int find_palette( const int16_t *inbuf, int inbuf_size, palette_t *p) {
    int freq[512], i;

    // Calculate frequencies of the given weight stream
    calc_freq( inbuf, inbuf_size, freq);

    // Find two most common values
    int most_common_freq[2]={0}, most_common_val[2]={0};
    for(i=0; i<512; i++) {
        if ( freq[i] > most_common_freq[0] ) {
            most_common_freq[1] = most_common_freq[0];
            most_common_val[1]  = most_common_val[0];
            most_common_freq[0] = freq[i];
            most_common_val[0]  = i-256;
        } else if ( freq[i] > most_common_freq[1] ) {
            most_common_freq[1] = freq[i];
            most_common_val[1]  = i-256;
        }
    }

    // Decide if zero-runs (alternating mode) should be used:
    // * zero should be the most common symbol
    // * zero should be sufficiently more common than the second most common symbol
    int use_zero_runs = most_common_val[0]==0 && most_common_freq[0] > ZERO_RUN_THRES*most_common_freq[1];

    // Create the palette
    create_palette( freq, use_zero_runs, p);

    return use_zero_runs;
}

static void create_inverse_palette( palette_t *p) {
    int i;
    memset( p->inv_lut, 0, sizeof(p->inv_lut));
    for(i=0; i<512; i++) {
        int val  = i;
        int sign = val&1;
        int mag  = val>>1;
        int weight = sign ? -mag : mag;
        int index = weight+256;
        if (index >= 0 && index < 512)
            p->inv_lut[ index ] = i + p->palsize - p->direct_offset;
    }
    for(i=0; i<p->palsize; i++) {
        int val = p->lut[i];
        int sign = val&1;
        int mag  = val>>1;
        int weight = sign ? -mag : mag;
        int index = weight+256;
        assert(index >= 0 && index < 512);
        if (index >= 0 && index < 512)
            p->inv_lut[ index ] = i;
    }
}

#define NWCFG 13
#define NZCFG 4 // restrict search to ZDIV=0..3
#define MAX_ZWCFG (max(NWCFG,NZCFG))

// search state
typedef struct search_state {
    int bitcnt;             // number of bits to reach this state
    uint8_t prev_cfg;       // previous grc parameter config
} search_state_t;

// (trunc<<4) | div, 0x20 means uncompressed
static const uint8_t w_grc_params[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x20 };
static const uint8_t z_grc_params[] = { 0x00, 0x01, 0x02, 0x03, 0x04 };



// An algorithm similar to the Viterbi algorithm is used to search for a
// good GRC parameter sequence for the given input value sequence.
// The inval buffer can contain weights, weight indices or runs.
// The return value is the resulting number of bitstream sections.
static int search_grc_params( const int *inval_buf,
                              int n_inval,
                              int zrun_mode,
                              int uncompressed_bits,
                              uint8_t *grc_param_cfg,
                              int *grc_param_pos,
                              int max_grc_param_cfg,
                              int *existing_grc_param_pos,
                              int n_existing_grc_param_pos,
                              int *bitcnt )
{
    int n_cfg = zrun_mode ? NZCFG : NWCFG;
    const uint8_t *grc_params = zrun_mode ? z_grc_params : w_grc_params;
    int i,j;

    search_state_t *state[MAX_ZWCFG];
    for(i=0; i<n_cfg; i++) {
        CHECKED_MALLOC(state[i], sizeof(search_state_t) * (n_inval + 1));
        state[i][0].bitcnt=0;
        state[i][0].prev_cfg=i;
    }

    if ( i < n_cfg ) {  // Memory allocation failed - clean up and exit
        while ( i ) {
            free(state[--i]);
        }
        return -1;
    }

    // Loop over inval_buf
    int existing_idx=0;
    for(i=0; i<n_inval; i++) {
        int value = inval_buf[i];

        // Best GRC parameter so far
        int best_bitcnt=0x7fffffff, best_cfg=0;
        for(j=0; j<n_cfg; j++) {
            if (state[j][i].bitcnt < best_bitcnt) {
                best_bitcnt = state[j][i].bitcnt;
                best_cfg = j;
            }
        }

        int cmd_cost = 40;
        if (existing_idx < n_existing_grc_param_pos && existing_grc_param_pos[existing_idx] == (i+1)) {
            // free transition, because the weight stream already inserted a command at this position
            cmd_cost = 0;
            existing_idx++;
        }

        // Loop over GRC parameters, calculate bits to code value, and then update the search state
        for(j=0; j<n_cfg; j++) {
            int div = grc_params[j]&15;
            int trunc = grc_params[j]>>4;
            int q = value>>div;
            int bits = trunc ? min(q+1,2) + div : q+1+div;
            if (!zrun_mode && ((trunc && q>2) || q>31))
                bits=10000;  // it's not possible to code the current value; give it a high cost
            if (trunc==2)
                bits=uncompressed_bits;

            if ( best_bitcnt + cmd_cost < state[j][i].bitcnt ) {
                // Change GRC parameters
                state[j][i+1].prev_cfg  = best_cfg;
                state[j][i+1].bitcnt    = best_bitcnt + cmd_cost + bits;
            } else {
                // Keep same GRC parameters
                state[j][i+1].prev_cfg  = j;
                state[j][i+1].bitcnt    = state[j][i].bitcnt + bits;
            }
        }
    }


    // Best GRC parameter
    int best_bitcnt=0x7fffffff, best_cfg=0;
    for(j=0; j<n_cfg; j++) {
        if (state[j][n_inval].bitcnt < best_bitcnt) {
            best_bitcnt = state[j][n_inval].bitcnt;
            best_cfg = j;
        }
    }

    int cfg = best_cfg;
    int n_cmds=0;
    for(i=n_inval; i>=0; i--) {
        if (state[cfg][i].prev_cfg != cfg || i==0) {
            n_cmds++;
            cfg = state[cfg][i].prev_cfg;
        }
    }

    (void)(max_grc_param_cfg);
    assert(n_cmds<=max_grc_param_cfg);

    cfg = best_cfg;
    j=n_cmds-1;
    int endpos=n_inval;
    for(i=n_inval; i>=0; i--) {
        if (state[cfg][i].prev_cfg != cfg || i==0) {
            grc_param_cfg[j] = cfg;
            grc_param_pos[j] = endpos;
            j--;
            cfg = state[cfg][i].prev_cfg;
            endpos = i-1;
        }
    }
    assert(j==-1);

    for(i=0; i<n_cfg; i++) {
        free(state[i]);
    }

    *bitcnt = best_bitcnt;

    return n_cmds;
}


/////////////////////////////// Write to bitstream

typedef struct bitbuf {
    uint8_t *buf;
    int buf_size;               // in bytes
    int pos;                    // bit pos of next bit
    int log_symbols;
} bitbuf_t;

// size in byte
static void bitbuf_init( bitbuf_t *bb, uint8_t *buf, int size, int log_symbols ) {
    bb->buf  = buf;
    bb->pos  = 0;
    bb->buf_size = size;
    bb->log_symbols = log_symbols;
}

static void bitbuf_putbit( bitbuf_t *bb, uint8_t bit) {
    int byte_pos = bb->pos>>3;
    uint8_t bit_pos = bb->pos&7;
    assert( byte_pos >= 0 );
    assert( byte_pos < bb->buf_size );
    bb->buf[ byte_pos ] = ((bb->buf[ byte_pos ] & ~(1U<<bit_pos)) | ((bit<<bit_pos) & 0xff)) & 0xff;
    bb->pos += 1;
}

static void bitbuf_put( bitbuf_t *bb, const char *name, int len, int data) {
    int i;
    if (len>0) {
        if (bb->log_symbols)
            printf("bitbuf: pos %3d %7s len %d data %x\n", bb->pos, name, len, data);
        for(i=0; i<len; i++) {
            bitbuf_putbit(bb, (uint8_t)((data>>i)&1));
        }
    }
}

// Return new bitpos
static int encode_slice( const int *w_value,
                         const int *z_value,
                         int nvalues,
                         palette_t *p,
                         int new_palette,
                         int uncompressed_bits,
                         int w_cfg,
                         int z_cfg,
                         uint8_t *bitbuf,
                         int bitbuf_size,
                         int bitpos,
                         int verbose )
{
    int i,j;
    bitbuf_t bitbuf_s, *bb=&bitbuf_s;
    bitbuf_init( bb, bitbuf, bitbuf_size, verbose&2?1:0 );
    bb->pos = bitpos;

    assert(nvalues<32768);
    if (w_cfg < 0 || z_cfg < 0)
        return bitpos;
    // GRC parameters for this slice
    int w_grc_div       = w_grc_params[w_cfg] & 15;
    int w_grc_trunc     = (w_grc_params[w_cfg] >> 4)==1;
    int w_uncompressed  = (w_grc_params[w_cfg] >> 4)==2;
    int z_grc_div       = z_grc_params[z_cfg] & 15;

    if (w_uncompressed) {
        w_grc_div = uncompressed_bits;
    }

    int zdiv = p->use_zero_runs ? z_grc_div : ZDIV_DISABLE;
    int wdiv = !w_uncompressed ? w_grc_div : WDIV_UNCOMPRESSED;

    if (verbose&1) {
        printf("slice: bitoffset %7d slicelen %5d zdiv %d wdiv %d wtrunc %d newpal %d palbits %d palsize %2d\n",
                bb->pos, nvalues, zdiv, wdiv, w_grc_trunc, new_palette, p->palbits, p->palsize);
    }

    // Write slice header
    bitbuf_put( bb, "ZDIV", 3, zdiv);
    bitbuf_put( bb, "SLICELEN", 15, nvalues-1 );
    bitbuf_put( bb, "WDIV", 3, wdiv);
    bitbuf_put( bb, "WTRUNC", 1, w_grc_trunc );
    bitbuf_put( bb, "NEWPAL", 1, new_palette );
    if (new_palette) {
        bitbuf_put( bb, "DIROFS", 5, p->direct_offset );
        bitbuf_put( bb, "PALSIZE", 5, max(0, p->palsize-1));
        bitbuf_put( bb, "PALBITS", 3, p->palbits-2 );
        for(i=0; i<p->palsize; i++) {
            bitbuf_put( bb, "PALETTE", p->palbits, p->lut[i] );
        }
    }

    int z_nvalues = nvalues + (new_palette?1:0);
    int w_pos=0, z_pos=0;
    int w_unary0=0, w_unary1=0, w_unary1_len=0, w_q=-1, w_r=0;
    int z_unary=0, z_q=-1, z_r=0;
    int w_nsymbols=0, w_remain[12]={0};
    int w_prev_enable=0, w_prev_nsymbols=0, w_prev_remain[12]={0};
    int z_nsymbols=0, z_remain[12]={0};
    int z_prev_enable=0, z_prev_nsymbols=0, z_prev_remain[12]={0};
    int z_unary_len = z_grc_div<3 ? 12 : 8;
    do {
        int balance = p->use_zero_runs ? w_pos - z_pos : 0;
        int w_enable = balance<8 && w_pos<nvalues;
        int z_enable = balance>=0 && p->use_zero_runs && z_pos<z_nvalues;
        if (w_enable) {
            // Encode chunk (weights)
            j=0;
            w_nsymbols=0;
            w_unary0=0;
            w_unary1=0;
            w_unary1_len=0;
            int max_symbols = w_uncompressed && w_grc_div>5 ? 8 : 12;
            while(j<max_symbols) {
                if (w_q<0) {
                    if (w_pos<nvalues) {
                        int value = w_value[w_pos];
                        assert(value<512);
                        w_q = value>>w_grc_div;
                        w_r = value&((1<<w_grc_div)-1);
                        assert( w_q<=31 && (!w_grc_trunc || w_q<=2));
                    } else {
                        w_q = 0;
                        w_r = -1;   // don't send remainder
                    }
                }
                while( w_q>=0 && j<max_symbols) {
                    w_unary0 |= w_q>0 ? (1<<j) : 0;
                    if (w_q>0) {
                        w_unary1 |= w_q>1 ? (1<<w_unary1_len) : 0;
                        w_unary1_len++;
                    }
                    j++;
                    w_q-=2;
                    if (w_grc_trunc)
                        w_q--;
                }
                if (w_q<0 && w_r>=0) {
                    w_remain[w_nsymbols] = w_r;
                    w_nsymbols++;
                    w_pos++;
                }
            }
        }

        if (z_enable) {
            // Encode chunk (zrun)
            j=0;
            z_nsymbols=0;
            z_unary=0;
            while(j<z_unary_len) {
                if (z_q<0) {
                    if (z_pos<z_nvalues) {
                        int value = z_value[z_pos];
                        z_q = value>>z_grc_div;
                        z_r = value&((1<<z_grc_div)-1);
                    } else {
                        z_q = 0;
                        z_r = -1;
                    }
                }
                while( z_q>=0 && j<z_unary_len) {
                    z_unary |= z_q>0 ? (1<<j) : 0;
                    j++;
                    z_q--;
                }
                if (z_q<0 && z_r>=0) {
                    z_remain[z_nsymbols] = z_r;
                    z_nsymbols++;
                    z_pos++;
                }
            }
        }

        // Write chunk to bitstream
        if (w_enable && !w_uncompressed) {
            bitbuf_put( bb, "WUNARY0", 12, w_unary0);
        }
        if (z_enable) {
            bitbuf_put( bb, "ZUNARY", z_unary_len, z_unary);
        }
        if (w_enable && !w_uncompressed) {
            bitbuf_put( bb, "WUNARY1", w_unary1_len, w_unary1);
        }
        if (w_prev_enable) {
            for(i=0; i<w_prev_nsymbols; i++) {
                bitbuf_put( bb, "WREMAIN", w_grc_div, w_prev_remain[i]);
            }
        }
        if (z_prev_enable) {
            for(i=0; i<z_prev_nsymbols; i++) {
                bitbuf_put( bb, "ZREMAIN", z_grc_div, z_prev_remain[i]);
            }
        }
        w_prev_enable = w_enable;
        w_prev_nsymbols = w_nsymbols;
        memcpy( w_prev_remain, w_remain, sizeof(w_prev_remain));
        z_prev_enable = z_enable;
        z_prev_nsymbols = z_nsymbols;
        memcpy( z_prev_remain, z_remain, sizeof(z_prev_remain));
    } while( w_prev_enable || z_prev_enable );

    return bb->pos;
}

// return new bitpos
static int encode_section( const int16_t *inbuf,
                           int size,
                           palette_t *p,
                           uint8_t *bitbuf,
                           int bitbuf_size,
                           int bitpos,
                           int verbose )
{
    int uncompressed_bits;

    // Uncompressed mode can only be used if either all weights
    // are in the palette OR if the palette is not used.
    if (p->only_palette) {
        // Uncompressed bits derived from palette size
        uncompressed_bits=0;
        while( (1<<uncompressed_bits) < p->palsize )
            uncompressed_bits++;
    } else if (p->palsize==0) {
        // Uncompressed bits is palbits (which is the bitdepth of the greatest weight)
        uncompressed_bits = p->palbits;
    } else {
        // Don't use uncompressed
        uncompressed_bits = 100;
    }

    uint8_t *w_slice_cfg=0;
    uint8_t *z_slice_cfg=0;
    int *w_slice_pos=0;
    int *z_slice_pos=0;
    int *weight_values =0;
    int *zrun_values = 0;
    do {
        CHECKED_MALLOC( weight_values, size*sizeof(int) );
        CHECKED_MALLOC( zrun_values, size*sizeof(int) );

        // Get weights (or weight indicies) AND zero-runs from the input weight stream.
        int i=0, n_weights = 0, zcnt;
        while(1) {
            if (p->use_zero_runs) {
                zcnt=0;
                // Count zero run
                // Special case: if all weights in the section are zero, we must
                // still ensure we have one coded weight so the the slice length
                // doesn't become 0. Therefore we skip the first zero run and code
                // the zero explicitly as a weight value instead
                if (!p->only_zeros || i>0) {
                    while( i<size && inbuf[i]==0) {
                        zcnt++;
                        i++;
                    }
                }
                zrun_values[n_weights] = zcnt;
            }
            if (i==size)
                break;
            int value = p->inv_lut[inbuf[i]+256];
            weight_values[n_weights] = value;
            n_weights++;
            i++;
        }

        // Search for good GRC parameters for the weight stream
        int n_w_slice, w_bitcnt;
        CHECKED_MALLOC( w_slice_cfg, size );
        CHECKED_MALLOC( w_slice_pos, size*sizeof(int) );
        n_w_slice = search_grc_params( weight_values, n_weights, 0, uncompressed_bits, w_slice_cfg, w_slice_pos, size, 0, 0, &w_bitcnt);
        if ( n_w_slice < 0 ) {  // Memory allocation failed
            bitpos = -1;
            break;
        }
        if (n_weights==0)
            n_w_slice = 0;

        // Search for good GRC parameters for the zrun stream
        int n_z_slice=0, z_bitcnt=0;
        if (p->use_zero_runs) {
            CHECKED_MALLOC( z_slice_cfg, size );
            CHECKED_MALLOC( z_slice_pos, size*sizeof(int) );
            n_z_slice = search_grc_params( zrun_values, n_weights+1, 1, 0, z_slice_cfg, z_slice_pos, size, w_slice_pos, n_w_slice, &z_bitcnt);
            if ( n_z_slice < 0 ) {  // Memory allocation failed
                bitpos = -1;
                break;
            }
        }

        // Encode bitstream slice
        int pos=0, i_w_slice=0, i_z_slice=0, new_palette=1;
        while(pos<n_weights || new_palette) {
            int endpos=pos+32767;   // max slice length

            if (i_w_slice<n_w_slice && w_slice_pos[i_w_slice]<endpos) {
                endpos = w_slice_pos[i_w_slice];
            }

            if (i_z_slice<n_z_slice && z_slice_pos[i_z_slice]<endpos) {
                endpos = z_slice_pos[i_z_slice];
            }

            if (n_weights < endpos) {
                endpos = n_weights;
            }

            // The first slice (when new_palette is 1) encodes zero runs both at the
            // beginning and end (i.e. number of zero runs are len+1).
            // The following slices only encode zero runs at the end (there cannot be
            // any zeros in the beginning since they are encoded by the previous slice)
            int len = endpos - pos;
            int *zrun_buf = p->use_zero_runs ? zrun_values+pos+(!new_palette) : 0;
            bitpos = encode_slice( weight_values+pos, zrun_buf, len,
                                p, new_palette, uncompressed_bits,
                                w_slice_cfg[i_w_slice], p->use_zero_runs ? z_slice_cfg[i_z_slice] : 0,
                                bitbuf, bitbuf_size, bitpos, verbose );
            new_palette = 0;

            if (i_w_slice<n_w_slice && w_slice_pos[i_w_slice]==endpos) {
                i_w_slice++;
            }
            if (i_z_slice<n_z_slice && z_slice_pos[i_z_slice]==endpos) {
                i_z_slice++;
            }
            pos = endpos;
        }
    } while(false);

    // Free temporary buffers
    free(w_slice_cfg);
    free(w_slice_pos);
    if (p->use_zero_runs) {
        free(z_slice_cfg);
        free(z_slice_pos);
    }
    free(weight_values);
    free(zrun_values);

    return bitpos;
}

// Encode the given weight stream
//      inbuf       uncompressed 9bit signed weights
//      inbuf_size  number of weights
//      outbuf      compressed bitstream, buffer is malloced within this function
//      verbose     if non-zero, printf log
// Return value is the size in bytes of the compressed output
// Return -1 if error
int mlw_encode( int16_t *inbuf, int inbuf_size, uint8_t **outbuf, int verbose) {
    int i;
    // Range check
    for(i=0; i<inbuf_size; i++) {
        //printf("index %d, weight value is %d\n", i, inbuf[i] + 130);

        if (inbuf[i]<-255 || inbuf[i]>255) {
            printf("ERROR: weight out of range at index %d, weight value is %d (valid range is -255..255)\n", i, inbuf[i]);
            return -1;
        }
    }

    int bitbuf_size = inbuf_size*2+1024;
    assert(*outbuf == NULL);
    *outbuf = malloc( bitbuf_size );
    if (!*outbuf)
    {  // Failed to allocate buffer
        return -1;
    }

    // Analyse input data to find palette re-programming points
    int *palette_restart_pos = NULL;
    int n_restarts = search_palette_sections( inbuf, inbuf_size, &palette_restart_pos);

    // Compress each section (using a single palette) separately
    int bitpos = 0;
    for ( i = 0; i < n_restarts && bitpos >= 0; i++ ) {
        palette_t palette;
        int pos, size;
        pos = palette_restart_pos[i];
        size = (i<n_restarts-1 ? palette_restart_pos[i+1] : inbuf_size) - pos;
        find_palette( inbuf+pos, size, &palette);
        create_inverse_palette( &palette);
        bitpos = encode_section( inbuf+pos, size, &palette,
                                 *outbuf, bitbuf_size, bitpos, verbose );
    }

    int ret = -1;
    if ( bitpos >= 0 && n_restarts >= 0 ) {  // If allocation fails bitpos or n_restarts < 0
        // Add end of stream marker and align to 128bit
        bitbuf_t bitbuf_s, *bb=&bitbuf_s;
        bitbuf_init( bb, *outbuf, bitbuf_size, verbose&2?1:0 );
        bb->pos = bitpos;
        bitbuf_put( bb, "ZDIV", 3, ZDIV_EOS);
        bitbuf_put( bb, "BYTEALIGN", (8-(bb->pos&7))&7, 0xff );

        // Pad with 0xff until 64bit aligned
        while( bb->pos & 127 ) {
          bitbuf_put( bb, "PAD", 8, 0xff );
        }
        bitpos = bb->pos;

        assert((bitpos&127)==0);
        int outbuf_size = bitpos/8;
        *outbuf = realloc(*outbuf, outbuf_size);
        if ( *outbuf ) {
            ret = outbuf_size;
        }
    }

    free(palette_restart_pos);

    return ret;
}

void mlw_free_outbuf( uint8_t *outbuf ) {
    if (outbuf)
        free(outbuf);
}

struct brick_buf_s
{
    int16_t* buf;
    int* strides;
};
typedef struct brick_buf_s brick_buf_t;

static int16_t get_brick_weight(brick_buf_t* buf, int ofm_z, int wy, int wx, int ifm_z)
{
    int16_t* p = buf->buf;

    p += ofm_z * buf->strides[0];
    p += wy * buf->strides[1];
    p += wx * buf->strides[2];
    p += ifm_z * buf->strides[3];

    return *p;
}

static void reorder_free(int16_t* buf)
{
    if (buf)
    {
        free(buf);
    }
}

static int16_t* reorder(
    int ifm_ublock_depth,
    int ofm_ublock_depth,
    int ofm_depth,
    int kernel_height,
    int kernel_width,
    int ifm_depth,
    int* strides,
    int16_t* inbuf,
    int ofm_block_depth,
    int is_depthwise,
    int is_partkernel,
    int ifm_bitdepth,
    int decomp_h,
    int decomp_w,
    int64_t* padded_length)
{
    *padded_length = -1;
    /* Size unknown. Start with one page at least */
    int64_t length = round_up(max(1, sizeof(int16_t)*
        ofm_depth*
        kernel_height*
        kernel_width*
        ifm_depth),
    4*1024) / sizeof(int16_t);
    int16_t* weights = (int16_t*)malloc(length * sizeof(int16_t));
    if (!weights)
    { // Alloc failed, so exit
        return NULL;
    }

    brick_buf_t brick_buf;
    brick_buf.buf = inbuf;
    brick_buf.strides = strides;

    int ifm_block_depth = is_partkernel || ifm_bitdepth == 16 ? 16 : 32;
    int64_t weight_cnt = 0;
    for (int ofm_block_z = 0; ofm_block_z < ofm_depth; ofm_block_z += ofm_block_depth)
    {
        int clipped_ofm_block_depth = min(ofm_block_depth, ofm_depth - ofm_block_z);
        // IFM blocks required for the brick
        for (int ifm_block_z = 0; ifm_block_z < (is_depthwise ? 1 : ifm_depth); ifm_block_z += ifm_block_depth)
        {
            int clipped_ifm_block_depth;
            if (is_depthwise)
            {
                clipped_ifm_block_depth = ifm_ublock_depth;
            }
            else
            {
                clipped_ifm_block_depth = is_partkernel ?
                    min(ifm_block_depth, ifm_depth - ifm_block_z) : ifm_block_depth;
            }
            // Weight decomposition
            // Subkernel Splitting  (H)
            for (int subkernel_y = 0; subkernel_y < kernel_height; subkernel_y += decomp_h)
            {
                int sub_height = min(kernel_height - subkernel_y, decomp_h);
                // Subkernel splitting (W)
                for (int subkernel_x = 0; subkernel_x < kernel_width; subkernel_x += decomp_w)
                {
                    int sub_width = min(kernel_width - subkernel_x, decomp_w);
                    int subkernel_elements = sub_width * sub_height;
                    // Part kernel first works across the kernel H/W and needs padding
                    if (is_partkernel)
                    {
                        if (ifm_bitdepth == 16 && subkernel_elements % 2 != 0)
                        {
                            subkernel_elements = round_up(subkernel_elements, 2);
                        }
                        else if (ifm_bitdepth == 8 && subkernel_elements % 4 != 0)
                        {
                            subkernel_elements = round_up(subkernel_elements, 4);
                        }
                    }
                    else if (is_depthwise)
                    {
                        subkernel_elements = round_up(subkernel_elements, 4);
                    }
                    int ifm_block_depth_outer = is_partkernel ? clipped_ifm_block_depth : 1;
                    int ifm_block_depth_inner = is_partkernel ? 1 : clipped_ifm_block_depth;
                    for (int ifm_ublk_outer = 0; ifm_ublk_outer < ifm_block_depth_outer; ifm_ublk_outer += ifm_ublock_depth)
                    {
                        // OFM Ublocks in OFM-block over depth
                        for (int ofm_ublk = 0; ofm_ublk < clipped_ofm_block_depth; ofm_ublk += ofm_ublock_depth)
                        {
                            // HW Kernel element traversal - cannot be a H/W loop due to element
                            // padding requirement on depthwise/part-kernel configurations
                            for (int element = 0; element < subkernel_elements; element++)
                            {
                                int kx = element % sub_width;
                                int ky = element / sub_width;
                                // IFM Ublocks in IFM-block over depth (only 1 ublock if depthwise)
                                // In case of part-kernel-first IFM Ublock traversal have already been handled
                                // and this loop is ignored.
                                for (int ifm_ublk_inner = 0; ifm_ublk_inner < ifm_block_depth_inner; ifm_ublk_inner += ifm_ublock_depth)
                                {
                                    // Feed OFM ublock elements
                                    for (int ofm_ublock_z = 0; ofm_ublock_z < ofm_ublock_depth; ofm_ublock_z++)
                                    {
                                        // Source IFM ublock elements (only 1 element deep if depthwise)
                                        for (int ifm_ublock_z = 0; ifm_ublock_z < (is_depthwise ? 1 : ifm_ublock_depth); ifm_ublock_z++)
                                        {
                                            // Source position within the current subkernel
                                            int wx = subkernel_x + kx;
                                            int wy = subkernel_y + ky;
                                            // Source IFM/OFM slices
                                            int ifm_ublk = ifm_ublk_inner + ifm_ublk_outer;
                                            int ifm_z = ifm_block_z + ifm_ublk + ifm_ublock_z;
                                            int ofm_z = ofm_block_z + ofm_ublk + ofm_ublock_z;
                                            if ((ifm_z < ifm_depth) && (ofm_z < ofm_depth) && (ky < sub_height))
                                            {
                                                weights[weight_cnt] = get_brick_weight(&brick_buf, ofm_z, wy, wx, ifm_z);
                                                //fprintf(stderr, "weights[%ld] %d ofm_z %d wy %d wx %d ifm_z %d\n", weight_cnt, weights[weight_cnt], ofm_z, wy, wx, ifm_z);
                                            }
                                            else
                                            {
                                                weights[weight_cnt] = 0;
                                            }
                                            weight_cnt++;
                                            if (weight_cnt == length)
                                            {
                                                // Reallocate by doubling the buffer size as needed
                                                length *= 2;
                                                weights = (int16_t*)realloc(weights, length * sizeof(int16_t));
                                                if (!weights)
                                                { // Realloc failed, so exit
                                                    return NULL;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }


    weights = (int16_t*)realloc(weights, weight_cnt * sizeof(int16_t));
    if ( weights ) {
        *padded_length = weight_cnt;
    }

    return weights;
}

// Reorder and encode the given weight stream
// Return value is the size in bytes of the compressed output
// Return -1 if error
int mlw_reorder_encode(
    int ifm_ublock_depth,
    int ofm_ublock_depth,
    int ofm_depth,
    int kernel_height,
    int kernel_width,
    int ifm_depth,
    int* brick_strides,
    int16_t* inbuf,
    int ofm_block_depth,
    int is_depthwise,
    int is_partkernel,
    int ifm_bitdepth,
    int decomp_h,
    int decomp_w,
    uint8_t **outbuf, // *outbuf must be freed by caller
    int64_t* padded_length,
    int verbose)
{
   if (verbose) {
      fprintf(stderr, "mlw_reorder_encode: %d %d %d %d %d %d (%d %d %d %d) %d %d %d %d %d %d\n", ifm_ublock_depth,
                                                                  ofm_ublock_depth,
                                                                  ofm_depth,
                                                                  kernel_height,
                                                                  kernel_width,
                                                                  ifm_depth,
                                                                  brick_strides[0],
                                                                  brick_strides[1],
                                                                  brick_strides[2],
                                                                  brick_strides[3],
                                                                  ofm_block_depth,
                                                                  is_depthwise,
                                                                  is_partkernel,
                                                                  ifm_bitdepth,
                                                                  decomp_h,
                                                                  decomp_w);
   }
    /* Reorder weights */
    int16_t* weights = reorder(
        ifm_ublock_depth,
        ofm_ublock_depth,
        ofm_depth,
        kernel_height,
        kernel_width,
        ifm_depth,
        brick_strides,
        inbuf,
        ofm_block_depth,
        is_depthwise,
        is_partkernel,
        ifm_bitdepth,
        decomp_h,
        decomp_w,
        padded_length);

    /* Then encode */
    int output_length = -1;
    if (*padded_length > 0 && *padded_length <= INT32_MAX)
    {
        output_length = mlw_encode(weights, (int)*padded_length, outbuf, verbose);
    }
    reorder_free(weights);

    return output_length;
}
