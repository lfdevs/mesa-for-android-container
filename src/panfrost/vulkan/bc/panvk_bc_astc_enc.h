/*
 * Copyright 2026 PanVK-kbase contributors
 * SPDX-License-Identifier: MIT
 *
 * ASTC 4x4 HDR block encoder for the BC6H UF16 lowmem shadow (csf-v11/200).
 * Included by panvk_bc_astc.comp (GLSL) and by the host test (C): the code
 * is in the common GLSL/C subset (I()/U()/F() casts, CARR tables, globals
 * instead of out parameters, no vectors).
 *
 * Input: g_h[3 * p + c], the fp16 bit patterns (0..0x7bff) of texel p and
 * channel c of the exact BC6H decode. Output: g_blk[0..3], one 128-bit ASTC
 * block (little-endian words):
 *
 *   one partition, colour endpoint mode 11 (HDR RGB, direct), 6 endpoint
 *   values at QUANT_256 (bits 17..64), a 4x4 weight grid at QUANT_8
 *   (3 bits per weight, from bit 127 down), block mode 0x053.
 *
 * The encoder works in the LNS domain the ASTC HDR decoder interpolates in
 * (16-bit values, 5-bit exponent, 11-bit pseudo-log mantissa): principal
 * axis endpoints, all 8 sub-modes and both endpoint orders of mode 11 plus
 * the direct (majcomp 3) layout are tried for the endpoints, then one
 * least-squares refinement. Weights are picked on the decoded fp16 values,
 * i.e. on the exact objective.
 */

#ifndef ASTC_REFINE
#define ASTC_REFINE 2
#endif
/* Weights at QUANT_12 (12 levels, trit ISE, 58 bits) instead of QUANT_8
 * (8 levels, 48 bits); the endpoints stay at QUANT_256 either way. */
#ifndef ASTC_WQ12
#define ASTC_WQ12 1
#endif

/* Decoder side: 16-bit LNS value -> fp16 bits (ASTC lns_to_sf16). */
int
astc_lns_to_h(int c)
{
   int e = c >> 11;
   int m = c & 2047;
   int mt = m < 512 ? 3 * m : (m < 1536 ? 4 * m - 512 : 5 * m - 2048);
   return min((e << 10) | (mt >> 3), 0x7bff);
}

/* fp16 bits -> LNS value whose decode is h (centre of its interval). */
float
astc_h_to_lns(int h)
{
   int e = h >> 10;
   float mt = F((h & 1023) * 8 + 4);
   float m;
   if (mt < 1536.0)
      m = mt / 3.0;
   else if (mt < 5632.0)
      m = (mt + 512.0) / 4.0;
   else
      m = (mt + 2048.0) / 5.0;
   return F(e * 2048) + m;
}

GLOBAL int g_h[48];     /* input, fp16 bits */
GLOBAL float g_L[48];   /* input in the LNS domain */
GLOBAL int g_wi[16];    /* chosen weight index (0..7) per texel */
GLOBAL uint g_blk[4];   /* output block */

/* Endpoint encoding state: the 6 mode-11 values and the decoded 12-bit
 * endpoints (ep0 rgb, ep1 rgb). */
GLOBAL int g_v[6];
GLOBAL int g_e[6];

/* Weight levels, sorted, and (QUANT_12) the ISE value of each level. */
#if ASTC_WQ12
#define ASTC_NW 12
CARR(int, ASTC_WT, 12) 0, 5, 11, 17, 23, 28, 36, 41, 47, 53, 59, 64 CEND
CARR(int, ASTC_WISE, 12) 0, 4, 8, 2, 6, 10, 11, 7, 3, 9, 5, 1 CEND
/* ISE trit block T of trits (t0..t4), index t4 * 81 + t3 * 27 + t2 * 9 +
 * t1 * 3 + t0 (the integer_of_trits table of the ASTC specification). */
CARR(int, ASTC_TRITS, 243)
   0, 1, 2, 4, 5, 6, 8, 9, 10, 16, 17, 18, 20, 21, 22, 24,
   25, 26, 3, 7, 15, 19, 23, 27, 12, 13, 14, 32, 33, 34, 36, 37,
   38, 40, 41, 42, 48, 49, 50, 52, 53, 54, 56, 57, 58, 35, 39, 47,
   51, 55, 59, 44, 45, 46, 64, 65, 66, 68, 69, 70, 72, 73, 74, 80,
   81, 82, 84, 85, 86, 88, 89, 90, 67, 71, 79, 83, 87, 91, 76, 77,
   78, 128, 129, 130, 132, 133, 134, 136, 137, 138, 144, 145, 146, 148, 149, 150,
   152, 153, 154, 131, 135, 143, 147, 151, 155, 140, 141, 142, 160, 161, 162, 164,
   165, 166, 168, 169, 170, 176, 177, 178, 180, 181, 182, 184, 185, 186, 163, 167,
   175, 179, 183, 187, 172, 173, 174, 192, 193, 194, 196, 197, 198, 200, 201, 202,
   208, 209, 210, 212, 213, 214, 216, 217, 218, 195, 199, 207, 211, 215, 219, 204,
   205, 206, 96, 97, 98, 100, 101, 102, 104, 105, 106, 112, 113, 114, 116, 117,
   118, 120, 121, 122, 99, 103, 111, 115, 119, 123, 108, 109, 110, 224, 225, 226,
   228, 229, 230, 232, 233, 234, 240, 241, 242, 244, 245, 246, 248, 249, 250, 227,
   231, 239, 243, 247, 251, 236, 237, 238, 28, 29, 30, 60, 61, 62, 92, 93,
   94, 156, 157, 158, 188, 189, 190, 220, 221, 222, 31, 63, 127, 159, 191, 255,
   252, 253, 254
CEND
#else
#define ASTC_NW 8
CARR(int, ASTC_WT, 8) 0, 9, 18, 27, 37, 46, 55, 64 CEND
#endif
/* Mode 11 sub-mode field widths: a, b, c, d (d signed). */
CARR(int, M11_ABITS, 8) 9, 9, 10, 10, 11, 11, 12, 12 CEND
CARR(int, M11_BBITS, 8) 7, 8, 6, 7, 8, 6, 7, 6 CEND
CARR(int, M11_CBITS, 8) 6, 6, 7, 7, 6, 8, 7, 7 CEND
CARR(int, M11_DBITS, 8) 7, 6, 7, 6, 5, 6, 5, 6 CEND

int
bit_of(int v, int b)
{
   return (v >> b) & 1;
}

/* Decode the 6 values in g_v (mode 11) into g_e (12-bit, ep0 then ep1).
 * Same steps as the ASTC specification / astcenc hdr_rgb_unpack. */
void
m11_decode()
{
   int v0 = g_v[0], v1 = g_v[1], v2 = g_v[2], v3 = g_v[3], v4 = g_v[4],
       v5 = g_v[5];
   int modeval = ((v1 >> 7) & 1) | (((v2 >> 7) & 1) << 1) | (((v3 >> 7) & 1) << 2);
   int majcomp = ((v4 >> 7) & 1) | (((v5 >> 7) & 1) << 1);

   if (majcomp == 3) {
      g_e[0] = v0 << 4; g_e[1] = v2 << 4; g_e[2] = (v4 & 0x7f) << 5;
      g_e[3] = v1 << 4; g_e[4] = v3 << 4; g_e[5] = (v5 & 0x7f) << 5;
      return;
   }

   int a = v0 | ((v1 & 0x40) << 2);
   int b0 = v2 & 0x3f, b1 = v3 & 0x3f, c = v1 & 0x3f;
   int d0 = v4 & 0x1f, d1 = v5 & 0x1f;
   int x0 = bit_of(v2, 6), x1 = bit_of(v3, 6), x2 = bit_of(v4, 6),
       x3 = bit_of(v5, 6), x4 = bit_of(v4, 5), x5 = bit_of(v5, 5);
   int ohm = 1 << modeval;

   if ((ohm & 0xa4) != 0) a |= x0 << 9;
   if ((ohm & 0x08) != 0) a |= x2 << 9;
   if ((ohm & 0x50) != 0) a |= x4 << 9;
   if ((ohm & 0x50) != 0) a |= x5 << 10;
   if ((ohm & 0xa0) != 0) a |= x1 << 10;
   if ((ohm & 0xc0) != 0) a |= x2 << 11;
   if ((ohm & 0x04) != 0) c |= x1 << 6;
   if ((ohm & 0xe8) != 0) c |= x3 << 6;
   if ((ohm & 0x20) != 0) c |= x2 << 7;
   if ((ohm & 0x5b) != 0) { b0 |= x0 << 6; b1 |= x1 << 6; }
   if ((ohm & 0x12) != 0) { b0 |= x2 << 7; b1 |= x3 << 7; }
   if ((ohm & 0xaf) != 0) { d0 |= x4 << 5; d1 |= x5 << 5; }
   if ((ohm & 0x05) != 0) { d0 |= x2 << 6; d1 |= x3 << 6; }

   int db = M11_DBITS[modeval];
   int sx = 32 - db;
   d0 = (d0 << sx) >> sx;
   d1 = (d1 << sx) >> sx;

   int s = (modeval >> 1) ^ 3;
   a <<= s; b0 <<= s; b1 <<= s; c <<= s;
   d0 = d0 * (1 << s); d1 = d1 * (1 << s);

   int r1 = a, g1 = a - b0, bl1 = a - b1;
   int r0 = a - c, g0 = a - b0 - c - d0, bl0 = a - b1 - c - d1;
   r0 = clamp(r0, 0, 4095); g0 = clamp(g0, 0, 4095); bl0 = clamp(bl0, 0, 4095);
   r1 = clamp(r1, 0, 4095); g1 = clamp(g1, 0, 4095); bl1 = clamp(bl1, 0, 4095);

   if (majcomp == 1) {
      int t0 = r0, t1 = r1;
      r0 = g0; r1 = g1; g0 = t0; g1 = t1;
   } else if (majcomp == 2) {
      int t0 = r0, t1 = r1;
      r0 = bl0; r1 = bl1; bl0 = t0; bl1 = t1;
   }
   g_e[0] = r0; g_e[1] = g0; g_e[2] = bl0;
   g_e[3] = r1; g_e[4] = g1; g_e[5] = bl1;
}

int
rnd_div(float v, int s)
{
   return I(floor(v / F(1 << s) + 0.5));
}

/* Pack sub-mode 'mode' (0..7) for 12-bit endpoints p (ep0) -> q (ep1),
 * with channel 'maj' as the major one, into g_v. */
void
m11_pack(int mode, int maj, float p0, float p1, float p2, float q0, float q1,
         float q2)
{
   /* Major channel first (the decoder swaps it back). */
   float R0 = maj == 0 ? p0 : (maj == 1 ? p1 : p2);
   float G0 = maj == 1 ? p0 : p1;
   float B0 = maj == 2 ? p0 : p2;
   float R1 = maj == 0 ? q0 : (maj == 1 ? q1 : q2);
   float G1 = maj == 1 ? q0 : q1;
   float B1 = maj == 2 ? q0 : q2;

   int s = (mode >> 1) ^ 3;
   int amax = (1 << M11_ABITS[mode]) - 1;
   int bmax = (1 << M11_BBITS[mode]) - 1;
   int cmax = (1 << M11_CBITS[mode]) - 1;
   int dlim = 1 << (M11_DBITS[mode] - 1);

   int a = clamp(rnd_div(R1, s), 0, amax);
   float aa = F(a << s);
   int b0 = clamp(rnd_div(aa - G1, s), 0, bmax);
   int b1 = clamp(rnd_div(aa - B1, s), 0, bmax);
   int c = clamp(rnd_div(aa - R0, s), 0, cmax);
   int d0 = clamp(rnd_div(aa - F((b0 + c) << s) - G0, s), -dlim, dlim - 1);
   int d1 = clamp(rnd_div(aa - F((b1 + c) << s) - B0, s), -dlim, dlim - 1);

   bool m257 = mode == 2 || mode == 5 || mode == 7;
   bool m57 = mode == 5 || mode == 7;
   bool m14 = mode == 1 || mode == 4;
   bool m46 = mode == 4 || mode == 6;
   int x0 = m257 ? bit_of(a, 9) : bit_of(b0, 6);
   int x1 = m57 ? bit_of(a, 10) : (mode == 2 ? bit_of(c, 6) : bit_of(b1, 6));
   int x2 = mode == 3 ? bit_of(a, 9)
          : ((mode == 6 || mode == 7) ? bit_of(a, 11)
          : (mode == 5 ? bit_of(c, 7)
          : (m14 ? bit_of(b0, 7) : bit_of(d0, 6))));
   int x3 = (mode == 3 || mode >= 5) ? bit_of(c, 6)
          : (m14 ? bit_of(b1, 7) : bit_of(d1, 6));
   int x4 = m46 ? bit_of(a, 9) : bit_of(d0, 5);
   int x5 = m46 ? bit_of(a, 10) : bit_of(d1, 5);

   g_v[0] = a & 0xff;
   g_v[1] = (c & 0x3f) | (bit_of(a, 8) << 6) | ((mode & 1) << 7);
   g_v[2] = (b0 & 0x3f) | (x0 << 6) | (((mode >> 1) & 1) << 7);
   g_v[3] = (b1 & 0x3f) | (x1 << 6) | (((mode >> 2) & 1) << 7);
   g_v[4] = (d0 & 0x1f) | (x4 << 5) | (x2 << 6) | ((maj & 1) << 7);
   g_v[5] = (d1 & 0x1f) | (x5 << 5) | (x3 << 6) | (((maj >> 1) & 1) << 7);
}

/* Direct layout (majcomp 3): 8, 8 and 7 bits of the 12-bit endpoints. */
void
m11_pack_direct(float p0, float p1, float p2, float q0, float q1, float q2)
{
   g_v[0] = clamp(rnd_div(p0, 4), 0, 255);
   g_v[1] = clamp(rnd_div(q0, 4), 0, 255);
   g_v[2] = clamp(rnd_div(p1, 4), 0, 255);
   g_v[3] = clamp(rnd_div(q1, 4), 0, 255);
   g_v[4] = clamp(rnd_div(p2, 5), 0, 127) | 0x80;
   g_v[5] = clamp(rnd_div(q2, 5), 0, 127) | 0x80;
}

float
sqf(float x)
{
   return x * x;
}

/* Squared error of the decoded endpoints (g_e) against the targets. */
float
m11_ep_err(float p0, float p1, float p2, float q0, float q1, float q2)
{
   return sqf(F(g_e[0]) - p0) + sqf(F(g_e[1]) - p1) + sqf(F(g_e[2]) - p2) +
          sqf(F(g_e[3]) - q0) + sqf(F(g_e[4]) - q1) + sqf(F(g_e[5]) - q2);
}

/* Best mode-11 encoding of the 12-bit endpoint pair (p, q), either order.
 * Leaves the winner in g_v / g_e. */
void
m11_encode(float p0, float p1, float p2, float q0, float q1, float q2)
{
   float best = 1e30;
   int bv0 = 0, bv1 = 0, bv2 = 0, bv3 = 0, bv4 = 0, bv5 = 0;

   for (int order = 0; order < 2; order++) {
      /* ep0 = P, ep1 = Q (order 0) or the reverse. */
      float P0 = order == 0 ? p0 : q0, P1 = order == 0 ? p1 : q1,
            P2 = order == 0 ? p2 : q2;
      float Q0 = order == 0 ? q0 : p0, Q1 = order == 0 ? q1 : p1,
            Q2 = order == 0 ? q2 : p2;
      int maj = (Q0 >= Q1 && Q0 >= Q2) ? 0 : (Q1 >= Q2 ? 1 : 2);
      float Pm = maj == 0 ? P0 : (maj == 1 ? P1 : P2);
      float Qm = maj == 0 ? Q0 : (maj == 1 ? Q1 : Q2);

      /* c >= 0: ep0's major channel can not exceed ep1's. */
      if (Pm <= Qm + 8.0) {
         for (int mode = 7; mode >= 0; mode--) {
            m11_pack(mode, maj, P0, P1, P2, Q0, Q1, Q2);
            m11_decode();
            float e = m11_ep_err(P0, P1, P2, Q0, Q1, Q2);
            if (e < best) {
               best = e;
               bv0 = g_v[0]; bv1 = g_v[1]; bv2 = g_v[2];
               bv3 = g_v[3]; bv4 = g_v[4]; bv5 = g_v[5];
            }
         }
      }
      if (order == 0) {
         m11_pack_direct(P0, P1, P2, Q0, Q1, Q2);
         m11_decode();
         float e = m11_ep_err(P0, P1, P2, Q0, Q1, Q2);
         if (e < best) {
            best = e;
            bv0 = g_v[0]; bv1 = g_v[1]; bv2 = g_v[2];
            bv3 = g_v[3]; bv4 = g_v[4]; bv5 = g_v[5];
         }
      }
   }
   g_v[0] = bv0; g_v[1] = bv1; g_v[2] = bv2;
   g_v[3] = bv3; g_v[4] = bv4; g_v[5] = bv5;
   m11_decode();
}

/* Pick the weight of every texel for the endpoints in g_e, on the decoded
 * fp16 values. Returns the squared error in fp16-bit units. */
int
astc_pick_weights()
{
   int err = 0;
   int c00 = g_e[0] << 4, c01 = g_e[1] << 4, c02 = g_e[2] << 4;
   int c10 = g_e[3] << 4, c11 = g_e[4] << 4, c12 = g_e[5] << 4;

   for (int p = 0; p < 16; p++) {
      int be = 0x7fffffff, bi = 0;
      for (int i = 0; i < ASTC_NW; i++) {
         int w = ASTC_WT[i];
         int r = astc_lns_to_h((c00 * (64 - w) + c10 * w + 32) >> 6);
         int g = astc_lns_to_h((c01 * (64 - w) + c11 * w + 32) >> 6);
         int b = astc_lns_to_h((c02 * (64 - w) + c12 * w + 32) >> 6);
         int e = (r - g_h[3 * p]) * (r - g_h[3 * p]) +
                 (g - g_h[3 * p + 1]) * (g - g_h[3 * p + 1]) +
                 (b - g_h[3 * p + 2]) * (b - g_h[3 * p + 2]);
         if (e < be) { be = e; bi = i; }
      }
      g_wi[p] = bi;
      err += be;
   }
   return err;
}

/* Least-squares endpoints (12-bit domain) for the weights in g_wi, written
 * to g_ls[0..5]. Returns false when all weights are equal. */
GLOBAL float g_ls[6];

bool
astc_ls_endpoints()
{
   float a = 0.0, b = 0.0, c = 0.0;
   float s0r = 0.0, s0g = 0.0, s0b = 0.0, s1r = 0.0, s1g = 0.0, s1b = 0.0;

   for (int p = 0; p < 16; p++) {
      float t = F(ASTC_WT[g_wi[p]]) / 64.0;
      float u = 1.0 - t;
      a += u * u; b += u * t; c += t * t;
      s0r += u * g_L[3 * p]; s0g += u * g_L[3 * p + 1]; s0b += u * g_L[3 * p + 2];
      s1r += t * g_L[3 * p]; s1g += t * g_L[3 * p + 1]; s1b += t * g_L[3 * p + 2];
   }
   float det = a * c - b * b;
   if (det < 1e-3)
      return false;
   /* LNS (16-bit) -> 12-bit endpoint domain: / 16. */
   float k = 1.0 / (16.0 * det);
   g_ls[0] = clamp((c * s0r - b * s1r) * k, 0.0, 4095.0);
   g_ls[1] = clamp((c * s0g - b * s1g) * k, 0.0, 4095.0);
   g_ls[2] = clamp((c * s0b - b * s1b) * k, 0.0, 4095.0);
   g_ls[3] = clamp((a * s1r - b * s0r) * k, 0.0, 4095.0);
   g_ls[4] = clamp((a * s1g - b * s0g) * k, 0.0, 4095.0);
   g_ls[5] = clamp((a * s1b - b * s0b) * k, 0.0, 4095.0);
   return true;
}

/* Assemble the block from g_v and g_wi. */
void
astc_emit()
{
   /* Block mode 0x251 (4x4 grid, QUANT_12, one plane) or 0x053 (QUANT_8),
    * 1 partition, CEM 11 at bits 13..16, endpoints from bit 17. */
   uint w0 = (ASTC_WQ12 != 0 ? 0x251u : 0x053u) | (11u << 13);
   w0 |= U(g_v[0]) << 17;
   w0 |= U(g_v[1] & 0x7f) << 25;
   uint w1 = U(g_v[1]) >> 7;
   w1 |= U(g_v[2]) << 1;
   w1 |= U(g_v[3]) << 9;
   w1 |= U(g_v[4]) << 17;
   w1 |= U(g_v[5] & 0x7f) << 25;
   uint w2 = U(g_v[5]) >> 7;
   uint w3 = 0u;

   /* The weight ISE stream, bit k at block bit 127 - k, written in pieces
    * of up to 4 bits (k < 64, so only w2 bits >= 1 and w3 are hit). */
#if ASTC_WQ12
   int k = 0;
   for (int grp = 0; grp < 4; grp++) {
      int t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
      int m0 = 0, m1 = 0, m2 = 0, m3 = 0, m4 = 0;
      for (int j = 0; j < 5; j++) {
         int p = grp * 5 + j;
         int v = p < 16 ? ASTC_WISE[g_wi[min(p, 15)]] : 0;
         int t = v >> 2, m = v & 3;
         if (j == 0) { t0 = t; m0 = m; }
         if (j == 1) { t1 = t; m1 = m; }
         if (j == 2) { t2 = t; m2 = m; }
         if (j == 3) { t3 = t; m3 = m; }
         if (j == 4) { t4 = t; m4 = m; }
      }
      int T = ASTC_TRITS[t4 * 81 + t3 * 27 + t2 * 9 + t1 * 3 + t0];
      /* m0 T[1:0] m1 T[3:2] m2 T[4] m3 T[6:5] m4 T[7]; 16 values end after
       * the first 4 bits of the last group (58 bits). */
      int nfield = grp < 3 ? 10 : 2;
      for (int f = 0; f < nfield; f++) {
         int val = 0, n = 2;
         if (f == 0) val = m0;
         if (f == 1) val = T & 3;
         if (f == 2) val = m1;
         if (f == 3) val = (T >> 2) & 3;
         if (f == 4) val = m2;
         if (f == 5) { val = (T >> 4) & 1; n = 1; }
         if (f == 6) val = m3;
         if (f == 7) val = (T >> 5) & 3;
         if (f == 8) val = m4;
         if (f == 9) { val = (T >> 7) & 1; n = 1; }
         for (int i = 0; i < n; i++) {
            uint bit = U((val >> i) & 1);
            int pos = 127 - k;
            if (pos >= 96)
               w3 |= bit << U(pos - 96);
            else
               w2 |= bit << U(pos - 64);
            k++;
         }
      }
   }
#else
   for (int p = 0; p < 16; p++) {
      for (int k = 0; k < 3; k++) {
         uint bit = (U(g_wi[p]) >> U(k)) & 1u;
         int pos = 127 - (3 * p + k);
         if (pos >= 96)
            w3 |= bit << U(pos - 96);
         else
            w2 |= bit << U(pos - 64);
      }
   }
#endif
   g_blk[0] = w0;
   g_blk[1] = w1;
   g_blk[2] = w2;
   g_blk[3] = w3;
}

/* Encode g_h into g_blk. */
void
astc_encode_block()
{
   float mr = 0.0, mg = 0.0, mb = 0.0;
   for (int p = 0; p < 16; p++) {
      g_L[3 * p] = astc_h_to_lns(g_h[3 * p]);
      g_L[3 * p + 1] = astc_h_to_lns(g_h[3 * p + 1]);
      g_L[3 * p + 2] = astc_h_to_lns(g_h[3 * p + 2]);
      mr += g_L[3 * p]; mg += g_L[3 * p + 1]; mb += g_L[3 * p + 2];
   }
   mr /= 16.0; mg /= 16.0; mb /= 16.0;

   /* Principal axis: power iteration on the covariance. */
   float cxx = 0.0, cxy = 0.0, cxz = 0.0, cyy = 0.0, cyz = 0.0, czz = 0.0;
   for (int p = 0; p < 16; p++) {
      float x = g_L[3 * p] - mr, y = g_L[3 * p + 1] - mg, z = g_L[3 * p + 2] - mb;
      cxx += x * x; cxy += x * y; cxz += x * z;
      cyy += y * y; cyz += y * z; czz += z * z;
   }
   float dx = 1.0, dy = 1.0, dz = 1.0;
   for (int it = 0; it < 6; it++) {
      float nx = cxx * dx + cxy * dy + cxz * dz;
      float ny = cxy * dx + cyy * dy + cyz * dz;
      float nz = cxz * dx + cyz * dy + czz * dz;
      float n = max(max(max(nx, -nx), max(ny, -ny)), max(nz, -nz));
      if (n < 1e-6)
         break;
      dx = nx / n; dy = ny / n; dz = nz / n;
   }
   float tmin = 0.0, tmax = 0.0;
   float dd = dx * dx + dy * dy + dz * dz;
   for (int p = 0; p < 16; p++) {
      float t = ((g_L[3 * p] - mr) * dx + (g_L[3 * p + 1] - mg) * dy +
                 (g_L[3 * p + 2] - mb) * dz) / dd;
      tmin = min(tmin, t);
      tmax = max(tmax, t);
   }

   float k = 1.0 / 16.0;
   m11_encode(clamp((mr + tmin * dx) * k, 0.0, 4095.0),
              clamp((mg + tmin * dy) * k, 0.0, 4095.0),
              clamp((mb + tmin * dz) * k, 0.0, 4095.0),
              clamp((mr + tmax * dx) * k, 0.0, 4095.0),
              clamp((mg + tmax * dy) * k, 0.0, 4095.0),
              clamp((mb + tmax * dz) * k, 0.0, 4095.0));
   int err = astc_pick_weights();

   /* Least-squares refinements, kept while they help. */
   for (int it = 0; it < ASTC_REFINE && err > 0; it++) {
      if (!astc_ls_endpoints())
         break;

      int sv0 = g_v[0], sv1 = g_v[1], sv2 = g_v[2], sv3 = g_v[3],
          sv4 = g_v[4], sv5 = g_v[5];
      int sw[16];
      for (int p = 0; p < 16; p++)
         sw[p] = g_wi[p];

      m11_encode(g_ls[0], g_ls[1], g_ls[2], g_ls[3], g_ls[4], g_ls[5]);
      int err2 = astc_pick_weights();
      if (err2 >= err) {
         g_v[0] = sv0; g_v[1] = sv1; g_v[2] = sv2;
         g_v[3] = sv3; g_v[4] = sv4; g_v[5] = sv5;
         for (int p = 0; p < 16; p++)
            g_wi[p] = sw[p];
         m11_decode();
         break;
      }
      err = err2;
   }
   astc_emit();
}
