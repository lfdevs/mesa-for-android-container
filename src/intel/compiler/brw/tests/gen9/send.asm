send(8)         nullUD          g123UD          0x8a080017
                            urb MsgDesc: 1 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         nullUD          g13UD           0x12080007
                            urb MsgDesc: 0 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g123UD          0x8a080027
                            urb MsgDesc: 2 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(16)        g9UD            g2UD            0x02280300
                            hdc:ro MsgDesc: (0, 3, 0, 0) mlen 1 rlen 2       { align1 WE_all 1H };
send(8)         nullUD          g119UD          0x92080017
                            urb MsgDesc: 1 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(16)        nullUD          g127UD          0x82000010
                            ts/btd MsgDesc: mlen 1 rlen 0           { align1 WE_all 1H EOT };
send(8)         g124UD          g13UD           0x0643a001
                            sampler MsgDesc: ld_lz SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(16)        g120UD          g23UD           0x0c85a001
                            sampler MsgDesc: ld_lz SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 8 { align1 1H };
send(8)         g10UD           g2UD            0x02480028
                            urb MsgDesc: 2 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         nullUD          g8UD            0x140a0017
                            urb MsgDesc: 1 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g118UD          0x940a0017
                            urb MsgDesc: 1 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q EOT };
send(8)         g2UD            g10UD           0x08427001
                            sampler MsgDesc: ld SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(16)        g2UD            g18UD           0x10847001
                            sampler MsgDesc: ld SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 8 { align1 1H };
send(8)         nullUD          g11UD           0x0c0a0037
                            urb MsgDesc: 3 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x0a080027
                            urb MsgDesc: 2 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         nullUD          g6UD            0x0c088017
                            urb MsgDesc: 1 SIMD8 write masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x0a088017
                            urb MsgDesc: 1 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x08088017
                            urb MsgDesc: 1 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g2UD            0x06088017
                            urb MsgDesc: 1 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x0c088007
                            urb MsgDesc: 0 SIMD8 write masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x0a088007
                            urb MsgDesc: 0 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g125UD          0x86088007
                            urb MsgDesc: 0 SIMD8 write masked mlen 3 rlen 0 { align1 1Q EOT };
send(8)         g7UD            g7UD            0x0443a000
                            sampler MsgDesc: ld_lz SIMD8 Surface = 0 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(8)         g10UD           g6UD            0x0222a001
                            sampler MsgDesc: resinfo SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 2 { align1 1Q };
send(8)         g2UD            g19UD           0x084a8001
                            sampler MsgDesc: gather4 SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(16)        g25UD           g16UD           0x0444a001
                            sampler MsgDesc: resinfo SIMD16 Surface = 1 Sampler = 0 mlen 2 rlen 4 { align1 1H };
send(16)        g14UD           g7UD            0x0e8c8001
                            sampler MsgDesc: gather4 SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 8 { align1 1H };
send(8)         nullUD          g11UD           0x12080017
                            urb MsgDesc: 1 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g20UD           0x12080037
                            urb MsgDesc: 3 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g123UD          0x8a080057
                            urb MsgDesc: 5 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         g9UD            g6UD            0x0613d001
                            sampler MsgDesc: ld_mcs SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 1 { align1 1Q };
send(16)        g12UD           g14UD           0x0c25d001
                            sampler MsgDesc: ld_mcs SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 2 { align1 1H };
send(8)         g2UD            g14UD           0x0643d001
                            sampler MsgDesc: ld_mcs SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         g8UD            g17UD           0x0a43e001
                            sampler MsgDesc: ld2dms SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(16)        g26UD           g10UD           0x0c85d001
                            sampler MsgDesc: ld_mcs SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 8 { align1 1H };
send(16)        g34UD           g16UD           0x1485e001
                            sampler MsgDesc: ld2dms SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 8 { align1 1H };
send(8)         g5UD            g2UD            0x04320001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 3 { align1 1Q };
send(16)        g7UD            g2UD            0x08640001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 6 { align1 1H };
send(8)         g12UD           g10UD           0x0a33e001
                            sampler MsgDesc: ld2dms SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 3 { align1 1Q };
send(16)        g2UD            g18UD           0x1465e001
                            sampler MsgDesc: ld2dms SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 6 { align1 1H };
send(8)         g5UD            g2UD            0x04420001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(16)        g7UD            g2UD            0x08840001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(8)         g11UD           g9UD            0x0222a000
                            sampler MsgDesc: resinfo SIMD8 Surface = 0 Sampler = 0 mlen 1 rlen 2 { align1 1Q };
send(8)         g124UD          g13UD           0x064a8000
                            sampler MsgDesc: gather4 SIMD8 Surface = 0 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         g12UD           g5UD            0x02427000
                            sampler MsgDesc: ld SIMD8 Surface = 0 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         nullUD          g123UD          0x8a080037
                            urb MsgDesc: 3 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         g6UD            g11UD           0x144a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 10 rlen 4 { align1 1Q };
(+f1.0) send(8) g125UD          g3UD            0x0210b501
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 1, SIMD8, inc) mlen 1 rlen 1 { align1 1Q };
(+f1.0) send(16) g122UD         g4UD            0x0420a501
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 1, SIMD16, inc) mlen 2 rlen 2 { align1 1H };
send(8)         g6UD            g12UD           0x084a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(8)         g98UD           g17UD           0x0c43c001
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
send(8)         g124UD          g8UD            0x064a8001
                            sampler MsgDesc: gather4 SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(16)        g120UD          g12UD           0x0a8c8001
                            sampler MsgDesc: gather4 SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 8 { align1 1H };
send(8)         g6UD            g7UD            0x0a1a6001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 1 { align1 1Q };
send(8)         g7UD            g12UD           0x0a1a6102
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 1 { align1 1Q };
send(16)        g10UD           g12UD           0x122c6001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 2 { align1 1H };
send(16)        g12UD           g21UD           0x122c6102
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 2 Sampler = 1 mlen 9 rlen 2 { align1 1H };
send(8)         g124UD          g3UD            0x0a43e000
                            sampler MsgDesc: ld2dms SIMD8 Surface = 0 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(8)         nullUD          g119UD          0x92080027
                            urb MsgDesc: 2 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         g2UD            g3UD            0x0643d000
                            sampler MsgDesc: ld_mcs SIMD8 Surface = 0 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         nullUD          g7UD            0x0a080037
                            urb MsgDesc: 3 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         nullUD          g8UD            0x0a080047
                            urb MsgDesc: 4 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         nullUD          g29UD           0x0c0a0017
                            urb MsgDesc: 1 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g122UD          0x8c0a0017
                            urb MsgDesc: 1 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         g13UD           g10UD           0x02320001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 3 { align1 1Q };
send(16)        g22UD           g18UD           0x04640001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 2 rlen 6 { align1 1H };
send(8)         g124UD          g2UD            0x0232a000
                            sampler MsgDesc: resinfo SIMD8 Surface = 0 Sampler = 0 mlen 1 rlen 3 { align1 1Q };
send(8)         g2UD            g13UD           0x0c4b1001
                            sampler MsgDesc: gather4_po SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
send(16)        g18UD           g7UD            0x168d1001
                            sampler MsgDesc: gather4_po SIMD16 Surface = 1 Sampler = 0 mlen 11 rlen 8 { align1 1H };
send(8)         nullUD          g6UD            0x0a088027
                            urb MsgDesc: 2 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g7UD            0x0a088037
                            urb MsgDesc: 3 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g8UD            0x0a088047
                            urb MsgDesc: 4 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g9UD            0x0a088057
                            urb MsgDesc: 5 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         g124UD          g3UD            0x06427000
                            sampler MsgDesc: ld SIMD8 Surface = 0 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x06427001
                            sampler MsgDesc: ld SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(16)        g2UD            g18UD           0x0c847001
                            sampler MsgDesc: ld SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 8 { align1 1H };
send(8)         g6UD            g10UD           0x0c424001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
send(8)         g2UD            g7UD            0x0c4b1000
                            sampler MsgDesc: gather4_po SIMD8 Surface = 0 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
send(8)         g2UD            g4UD            0x0242a000
                            sampler MsgDesc: resinfo SIMD8 Surface = 0 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x0242a101
                            sampler MsgDesc: resinfo SIMD8 Surface = 1 Sampler = 1 mlen 1 rlen 4 { align1 1Q };
send(8)         g10UD           g10UD           0x0242a202
                            sampler MsgDesc: resinfo SIMD8 Surface = 2 Sampler = 2 mlen 1 rlen 4 { align1 1Q };
send(8)         g14UD           g14UD           0x0242a303
                            sampler MsgDesc: resinfo SIMD8 Surface = 3 Sampler = 3 mlen 1 rlen 4 { align1 1Q };
send(8)         g18UD           g18UD           0x0242a404
                            sampler MsgDesc: resinfo SIMD8 Surface = 4 Sampler = 4 mlen 1 rlen 4 { align1 1Q };
send(8)         g22UD           g22UD           0x0242a505
                            sampler MsgDesc: resinfo SIMD8 Surface = 5 Sampler = 5 mlen 1 rlen 4 { align1 1Q };
send(8)         g26UD           g26UD           0x0242a606
                            sampler MsgDesc: resinfo SIMD8 Surface = 6 Sampler = 6 mlen 1 rlen 4 { align1 1Q };
send(8)         g6UD            g15UD           0x042a0318
                            urb MsgDesc: 49 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g8UD            g15UD           0x042a0518
                            urb MsgDesc: 81 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g10UD           g15UD           0x042a0718
                            urb MsgDesc: 113 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g12UD           g15UD           0x042a0918
                            urb MsgDesc: 145 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g14UD           g15UD           0x042a0128
                            urb MsgDesc: 18 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g16UD           g14UD           0x042a0218
                            urb MsgDesc: 33 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g18UD           g14UD           0x042a0418
                            urb MsgDesc: 65 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g20UD           g14UD           0x042a0618
                            urb MsgDesc: 97 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g22UD           g14UD           0x042a0818
                            urb MsgDesc: 129 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g13UD           g14UD           0x042a0028
                            urb MsgDesc: 2 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g30UD           0x02480208
                            urb MsgDesc: 32 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g14UD           g30UD           0x02480408
                            urb MsgDesc: 64 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g18UD           g30UD           0x02480608
                            urb MsgDesc: 96 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g30UD           0x02480808
                            urb MsgDesc: 128 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         nullUD          g6UD            0x0a0a8217
                            urb MsgDesc: 33 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g11UD           0x0a0a8227
                            urb MsgDesc: 34 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g12UD           0x0a0a8237
                            urb MsgDesc: 35 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x0a0a8247
                            urb MsgDesc: 36 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g14UD           0x0a0a8257
                            urb MsgDesc: 37 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x0a0a8267
                            urb MsgDesc: 38 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x0a0a8277
                            urb MsgDesc: 39 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x0a0a8287
                            urb MsgDesc: 40 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x0a0a8297
                            urb MsgDesc: 41 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x0a0a82a7
                            urb MsgDesc: 42 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x0a0a82b7
                            urb MsgDesc: 43 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x0a0a82c7
                            urb MsgDesc: 44 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x0a0a82d7
                            urb MsgDesc: 45 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x0a0a82e7
                            urb MsgDesc: 46 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x0a0a82f7
                            urb MsgDesc: 47 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x0a0a8307
                            urb MsgDesc: 48 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x0a0a8317
                            urb MsgDesc: 49 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x0a0a8327
                            urb MsgDesc: 50 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x0a0a8337
                            urb MsgDesc: 51 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x0a0a8347
                            urb MsgDesc: 52 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x0a0a8357
                            urb MsgDesc: 53 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x0a0a8367
                            urb MsgDesc: 54 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x0a0a8377
                            urb MsgDesc: 55 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x0a0a8387
                            urb MsgDesc: 56 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x0a0a8397
                            urb MsgDesc: 57 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x0a0a83a7
                            urb MsgDesc: 58 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x0a0a83b7
                            urb MsgDesc: 59 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x0a0a83c7
                            urb MsgDesc: 60 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x0a0a83d7
                            urb MsgDesc: 61 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x0a0a83e7
                            urb MsgDesc: 62 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g40UD           0x0a0a83f7
                            urb MsgDesc: 63 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g11UD           0x08088027
                            urb MsgDesc: 2 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g12UD           0x08088037
                            urb MsgDesc: 3 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x08088047
                            urb MsgDesc: 4 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g14UD           0x08088057
                            urb MsgDesc: 5 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x08088067
                            urb MsgDesc: 6 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x08088077
                            urb MsgDesc: 7 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x08088087
                            urb MsgDesc: 8 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x08088097
                            urb MsgDesc: 9 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x080880a7
                            urb MsgDesc: 10 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x080880b7
                            urb MsgDesc: 11 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x080880c7
                            urb MsgDesc: 12 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x080880d7
                            urb MsgDesc: 13 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x080880e7
                            urb MsgDesc: 14 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x080880f7
                            urb MsgDesc: 15 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x08088107
                            urb MsgDesc: 16 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x08088117
                            urb MsgDesc: 17 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x08088127
                            urb MsgDesc: 18 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x08088137
                            urb MsgDesc: 19 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x08088147
                            urb MsgDesc: 20 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x08088157
                            urb MsgDesc: 21 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x08088167
                            urb MsgDesc: 22 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x08088177
                            urb MsgDesc: 23 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x08088187
                            urb MsgDesc: 24 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x08088197
                            urb MsgDesc: 25 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x080881a7
                            urb MsgDesc: 26 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x080881b7
                            urb MsgDesc: 27 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x080881c7
                            urb MsgDesc: 28 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x080881d7
                            urb MsgDesc: 29 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x080881e7
                            urb MsgDesc: 30 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g40UD           0x080881f7
                            urb MsgDesc: 31 SIMD8 write masked mlen 4 rlen 0 { align1 1Q };
send(8)         g13UD           g1UD            0x02480018
                            urb MsgDesc: 1 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         nullUD          g11UD           0x0c0a0207
                            urb MsgDesc: 32 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g119UD          0x92080057
                            urb MsgDesc: 5 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         g10UD           g18UD           0x084a8000
                            sampler MsgDesc: gather4 SIMD8 Surface = 0 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(8)         g124UD          g2UD            0x04229001
                            sampler MsgDesc: lod SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 2 { align1 1Q };
send(16)        g120UD          g2UD            0x08449001
                            sampler MsgDesc: lod SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1H };
send(16)        g32UD           g44UD           0x0865a001
                            sampler MsgDesc: ld_lz SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 6 { align1 1H };
send(16)        nullUD          g5UD            0x04008502
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 2, SIMD16, inc) mlen 2 rlen 0 { align1 1H };
send(8)         g5UD            g3UD            0x02427001
                            sampler MsgDesc: ld SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(16)        g8UD            g5UD            0x04847001
                            sampler MsgDesc: ld SIMD16 Surface = 1 Sampler = 0 mlen 2 rlen 8 { align1 1H };
send(8)         nullUD          g119UD          0x92080007
                            urb MsgDesc: 0 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         nullUD          g126UD          0x84080017
                            urb MsgDesc: 1 SIMD8 write mlen 2 rlen 0        { align1 1Q EOT };
send(8)         g2UD            g13UD           0x0a4b1001
                            sampler MsgDesc: gather4_po SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(16)        g16UD           g7UD            0x128d1001
                            sampler MsgDesc: gather4_po SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 8 { align1 1H };
send(8)         g38UD           g1UD            0x02180028
                            urb MsgDesc: 2 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g40UD           g1UD            0x02180038
                            urb MsgDesc: 3 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g42UD           g1UD            0x02180048
                            urb MsgDesc: 4 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g44UD           g1UD            0x02180058
                            urb MsgDesc: 5 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g46UD           g1UD            0x02180068
                            urb MsgDesc: 6 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g48UD           g1UD            0x02180078
                            urb MsgDesc: 7 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g50UD           g1UD            0x02180088
                            urb MsgDesc: 8 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g52UD           g1UD            0x02180098
                            urb MsgDesc: 9 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g54UD           g1UD            0x021800a8
                            urb MsgDesc: 10 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g56UD           g1UD            0x021800b8
                            urb MsgDesc: 11 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g58UD           g1UD            0x021800c8
                            urb MsgDesc: 12 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g60UD           g1UD            0x021800d8
                            urb MsgDesc: 13 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g62UD           g1UD            0x021800e8
                            urb MsgDesc: 14 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g64UD           g1UD            0x021800f8
                            urb MsgDesc: 15 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g66UD           g1UD            0x02180108
                            urb MsgDesc: 16 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g68UD           g1UD            0x02180118
                            urb MsgDesc: 17 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g70UD           g1UD            0x02180128
                            urb MsgDesc: 18 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g72UD           g1UD            0x02180138
                            urb MsgDesc: 19 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g74UD           g1UD            0x02180148
                            urb MsgDesc: 20 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g76UD           g1UD            0x02180158
                            urb MsgDesc: 21 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g78UD           g1UD            0x02180168
                            urb MsgDesc: 22 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g80UD           g1UD            0x02180178
                            urb MsgDesc: 23 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g82UD           g1UD            0x02180188
                            urb MsgDesc: 24 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g84UD           g1UD            0x02180198
                            urb MsgDesc: 25 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g86UD           g1UD            0x021801a8
                            urb MsgDesc: 26 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g88UD           g1UD            0x021801b8
                            urb MsgDesc: 27 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g90UD           g1UD            0x021801c8
                            urb MsgDesc: 28 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g92UD           g1UD            0x021801d8
                            urb MsgDesc: 29 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g94UD           g1UD            0x021801e8
                            urb MsgDesc: 30 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g96UD           g1UD            0x021801f8
                            urb MsgDesc: 31 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g98UD           g1UD            0x02180208
                            urb MsgDesc: 32 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         nullUD          g12UD           0x0c0a0027
                            urb MsgDesc: 2 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g126UD          0x040a02fd
                            hdc0 MsgDesc: ( DC OWORD block write, 253, 2) mlen 2 rlen 0 { align1 1Q };
send(8)         g115UD          g115UD          0x021802fd
                            hdc0 MsgDesc: ( DC OWORD block read, 253, 2) mlen 1 rlen 1 { align1 WE_all 1Q };
send(8)         nullUD          g25UD           0x12080057
                            urb MsgDesc: 5 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g34UD           0x12080077
                            urb MsgDesc: 7 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g43UD           0x12080097
                            urb MsgDesc: 9 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g52UD           0x120800b7
                            urb MsgDesc: 11 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g61UD           0x120800d7
                            urb MsgDesc: 13 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g70UD           0x120800f7
                            urb MsgDesc: 15 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g2UD            0x12080117
                            urb MsgDesc: 17 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g2UD            0x12080137
                            urb MsgDesc: 19 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g2UD            0x12080157
                            urb MsgDesc: 21 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g79UD           0x12080177
                            urb MsgDesc: 23 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g88UD           0x12080197
                            urb MsgDesc: 25 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g97UD           0x120801b7
                            urb MsgDesc: 27 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g106UD          0x120801d7
                            urb MsgDesc: 29 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g117UD          0x920801f7
                            urb MsgDesc: 31 SIMD8 write mlen 9 rlen 0       { align1 1Q EOT };
send(8)         g124UD          g11UD           0x02229001
                            sampler MsgDesc: lod SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 2 { align1 1Q };
send(16)        g120UD          g11UD           0x04449001
                            sampler MsgDesc: lod SIMD16 Surface = 1 Sampler = 0 mlen 2 rlen 4 { align1 1H };
send(8)         g124UD          g3UD            0x08427000
                            sampler MsgDesc: ld SIMD8 Surface = 0 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(16)        nullUD          g40UD           0x04008501
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 1, SIMD16, inc) mlen 2 rlen 0 { align1 1H };
send(8)         nullUD          g127UD          0x82080007
                            urb MsgDesc: 0 SIMD8 write mlen 1 rlen 0        { align1 1Q EOT };
send(8)         g124UD          g9UD            0x0a4a8000
                            sampler MsgDesc: gather4 SIMD8 Surface = 0 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(8)         g2UD            g23UD           0x0633a001
                            sampler MsgDesc: ld_lz SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 3 { align1 1Q };
send(16)        g4UD            g12UD           0x0c65a001
                            sampler MsgDesc: ld_lz SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 6 { align1 1H };
send(8)         g2UD            g16UD           0x0e434001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 7 rlen 4 { align1 2Q };
(+f1.0) send(8) nullUD          g4UD            0x02009501
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 1, SIMD8, inc) mlen 1 rlen 0 { align1 1Q };
send(8)         g6UD            g9UD            0x08434001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(8)         nullUD          g102UD          0x120801f7
                            urb MsgDesc: 31 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g121UD          0x8a080217
                            urb MsgDesc: 33 SIMD8 write mlen 5 rlen 0       { align1 1Q EOT };
send(16)        nullUD          g3UD            0x02008004
                            gateway MsgDesc: (barrier msg) mlen 1 rlen 0    { align1 WE_all 1H };
send(16)        g3UD            g14UD           0x04205efe
                            hdc1 MsgDesc: ( untyped surface read, Surface = 254, SIMD16, Mask = 0xe) mlen 2 rlen 2 { align1 1H };
send(8)         nullUD          g30UD           0x140a0027
                            urb MsgDesc: 2 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g40UD           0x0c0a0047
                            urb MsgDesc: 4 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g126UD          0x84080007
                            urb MsgDesc: 0 SIMD8 write mlen 2 rlen 0        { align1 1Q EOT };
send(8)         g5UD            g11UD           0x04415001
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD16, Mask = 0x0) mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g3UD            0x04416001
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD8, Mask = 0x0) mlen 2 rlen 4 { align1 2Q };
send(8)         g13UD           g3UD            0x02480038
                            urb MsgDesc: 3 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         nullUD          g7UD            0x140a0037
                            urb MsgDesc: 3 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         g15UD           g2UD            0x02280038
                            urb MsgDesc: 3 SIMD8 read mlen 1 rlen 2         { align1 1Q };
send(8)         nullUD          g119UD          0x92080037
                            urb MsgDesc: 3 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         nullUD          g8UD            0x140a0007
                            urb MsgDesc: 0 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g118UD          0x940a0007
                            urb MsgDesc: 0 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q EOT };
send(8)         g124UD          g12UD           a0<0,1,0>UD     0x00000200
                            sampler MsgDesc: indirect                       { align1 1Q };
send(8)         g10UD           g2UD            0x02480048
                            urb MsgDesc: 4 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         g6UD            g2UD            0x02480088
                            urb MsgDesc: 8 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         g14UD           g2UD            0x02480058
                            urb MsgDesc: 5 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         g11UD           g2UD            0x024800a8
                            urb MsgDesc: 10 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g18UD           g2UD            0x02480068
                            urb MsgDesc: 6 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         g16UD           g2UD            0x023800c8
                            urb MsgDesc: 12 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g2UD            0x02480078
                            urb MsgDesc: 7 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         g12UD           g2UD            0x024800b8
                            urb MsgDesc: 11 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g7UD            g2UD            0x02480098
                            urb MsgDesc: 9 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         nullUD          g119UD          0x920800b7
                            urb MsgDesc: 11 SIMD8 write mlen 9 rlen 0       { align1 1Q EOT };
send(8)         g6UD            g8UD            0x084b0000
                            sampler MsgDesc: gather4_c SIMD8 Surface = 0 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(8)         g7UD            g0UD            0x02200008
                            pi MsgDesc: (persp, per_message_offset, 0x08) mlen 1 rlen 2 { align1 1Q };
send(16)        g9UD            g0UD            0x02410008
                            pi MsgDesc: (persp, per_message_offset, 0x08) mlen 1 rlen 4 { align1 1H };
send(8)         g2UD            g11UD           0x0443d001
                            sampler MsgDesc: ld_mcs SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g9UD            0x0843e001
                            sampler MsgDesc: ld2dms SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(16)        g2UD            g15UD           0x0885d001
                            sampler MsgDesc: ld_mcs SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(16)        g43UD           g11UD           0x1085e001
                            sampler MsgDesc: ld2dms SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 8 { align1 1H };
send(8)         g2UD            g6UD            0x0a4b1000
                            sampler MsgDesc: gather4_po SIMD8 Surface = 0 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(8)         g74UD           g2UD            0x02280028
                            urb MsgDesc: 2 SIMD8 read mlen 1 rlen 2         { align1 1Q };
send(8)         g7UD            g2UD            0x02380028
                            urb MsgDesc: 2 SIMD8 read mlen 1 rlen 3         { align1 1Q };
send(8)         g15UD           g2UD            0x02380038
                            urb MsgDesc: 3 SIMD8 read mlen 1 rlen 3         { align1 1Q };
send(8)         g124UD          g3UD            0x0843e000
                            sampler MsgDesc: ld2dms SIMD8 Surface = 0 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(8)         g2UD            g3UD            0x0443d000
                            sampler MsgDesc: ld_mcs SIMD8 Surface = 0 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g19UD           0x0a4a8001
                            sampler MsgDesc: gather4 SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(16)        g7UD            g16UD           0x128c8001
                            sampler MsgDesc: gather4 SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 8 { align1 1H };
send(8)         nullUD          g2UD            0x0c0a0057
                            urb MsgDesc: 5 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g9UD            0x04080027
                            urb MsgDesc: 2 SIMD8 write mlen 2 rlen 0        { align1 1Q };
send(8)         g6UD            g7UD            0x08134001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 1 { align1 1Q };
send(8)         g7UD            g11UD           0x08134102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 1 { align1 1Q };
send(8)         g13UD           g17UD           0x021ab000
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 0 Sampler = 0 mlen 1 rlen 1 { align1 1Q };
send(8)         nullUD          g50UD           0x140a0057
                            urb MsgDesc: 5 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g60UD           0x140a0077
                            urb MsgDesc: 7 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g70UD           0x0c0a0097
                            urb MsgDesc: 9 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g122UD          0x8c0a0097
                            urb MsgDesc: 9 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         g124UD          g6UD            0x0a4b0000
                            sampler MsgDesc: gather4_c SIMD8 Surface = 0 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(8)         g5UD            g6UD            0x061a3001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 1 { align1 1Q };
send(8)         g6UD            g9UD            0x061a3102
                            sampler MsgDesc: sample_c SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 1 { align1 1Q };
send(16)        g9UD            g11UD           0x0a2c3001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 2 { align1 1H };
send(16)        g11UD           g2UD            0x0a2c3102
                            sampler MsgDesc: sample_c SIMD16 Surface = 2 Sampler = 1 mlen 5 rlen 2 { align1 1H };
send(8)         nullUD          g123UD          0x8a080077
                            urb MsgDesc: 7 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         nullUD          g30UD           0x0c0a0067
                            urb MsgDesc: 6 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x0c0a0077
                            urb MsgDesc: 7 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g42UD           0x0c0a0087
                            urb MsgDesc: 8 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         g6UD            g6UD            0x06420102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 4 { align1 1Q };
send(16)        g10UD           g18UD           0x0c840102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 6 rlen 8 { align1 1H };
send(8)         g2UD            g2UD            0x04420102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 2 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x06420304
                            sampler MsgDesc: sample SIMD8 Surface = 4 Sampler = 3 mlen 3 rlen 4 { align1 1Q };
send(16)        g2UD            g10UD           0x08840102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 4 rlen 8 { align1 1H };
send(16)        g10UD           g18UD           0x0c840304
                            sampler MsgDesc: sample SIMD16 Surface = 4 Sampler = 3 mlen 6 rlen 8 { align1 1H };
send(8)         g2UD            g2UD            0x04420304
                            sampler MsgDesc: sample SIMD8 Surface = 4 Sampler = 3 mlen 2 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x06420708
                            sampler MsgDesc: sample SIMD8 Surface = 8 Sampler = 7 mlen 3 rlen 4 { align1 1Q };
send(16)        g2UD            g10UD           0x08840304
                            sampler MsgDesc: sample SIMD16 Surface = 4 Sampler = 3 mlen 4 rlen 8 { align1 1H };
send(16)        g10UD           g18UD           0x0c840708
                            sampler MsgDesc: sample SIMD16 Surface = 8 Sampler = 7 mlen 6 rlen 8 { align1 1H };
send(8)         g3UD            g11UD           0x0a43c001
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(16)        g16UD           g5UD            0x1485c001
                            sampler MsgDesc: ld2dms_w SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 8 { align1 1H };
send(16)        g4UD            g13UD           0x02280301
                            hdc:ro MsgDesc: (1, 3, 0, 0) mlen 1 rlen 2       { align1 WE_all 1H };
send(8)         g2UD            g2UD            0x0443a001
                            sampler MsgDesc: ld_lz SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(16)        g2UD            g10UD           0x0885a001
                            sampler MsgDesc: ld_lz SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(8)         g12UD           g12UD           0x06125001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 1 { align1 1Q };
send(8)         g13UD           g15UD           0x06125102
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 1 { align1 1Q };
send(16)        g20UD           g22UD           0x0c245001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 2 { align1 1H };
send(16)        g22UD           g28UD           0x0c245102
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 2 Sampler = 1 mlen 6 rlen 2 { align1 1H };
send(8)         g38UD           g2UD            0x024800c8
                            urb MsgDesc: 12 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g39UD           g2UD            0x024800d8
                            urb MsgDesc: 13 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g40UD           g2UD            0x024800e8
                            urb MsgDesc: 14 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g41UD           g2UD            0x024800f8
                            urb MsgDesc: 15 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g42UD           g2UD            0x02480108
                            urb MsgDesc: 16 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g43UD           g2UD            0x02480118
                            urb MsgDesc: 17 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g44UD           g2UD            0x02480128
                            urb MsgDesc: 18 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g45UD           g2UD            0x02480138
                            urb MsgDesc: 19 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g46UD           g2UD            0x02480148
                            urb MsgDesc: 20 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g47UD           g2UD            0x02480158
                            urb MsgDesc: 21 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g48UD           g2UD            0x02480168
                            urb MsgDesc: 22 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g49UD           g2UD            0x02480178
                            urb MsgDesc: 23 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g50UD           g2UD            0x02480188
                            urb MsgDesc: 24 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g51UD           g2UD            0x02480198
                            urb MsgDesc: 25 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g52UD           g2UD            0x024801a8
                            urb MsgDesc: 26 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g53UD           g2UD            0x024801b8
                            urb MsgDesc: 27 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g54UD           g2UD            0x024801c8
                            urb MsgDesc: 28 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g55UD           g2UD            0x024801d8
                            urb MsgDesc: 29 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g56UD           g2UD            0x024801e8
                            urb MsgDesc: 30 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g57UD           g2UD            0x024801f8
                            urb MsgDesc: 31 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         nullUD          g19UD           0x080a8027
                            urb MsgDesc: 2 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g8UD            0x0a0a8027
                            urb MsgDesc: 2 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         g6UD            g11UD           0x0e424001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 7 rlen 4 { align1 1Q };
send(8)         g124UD          g7UD            0x0212a000
                            sampler MsgDesc: resinfo SIMD8 Surface = 0 Sampler = 0 mlen 1 rlen 1 { align1 1Q };
send(8)         g8UD            g14UD           0x044a0128
                            urb MsgDesc: 18 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g22UD           g16UD           0x044a0028
                            urb MsgDesc: 2 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         nullUD          g6UD            0x0a080017
                            urb MsgDesc: 1 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         nullUD          g7UD            0x0a080057
                            urb MsgDesc: 5 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         g4UD            g2UD            0x02406001
                            hdc1 MsgDesc: ( untyped surface read, Surface = 1, SIMD8, Mask = 0x0) mlen 1 rlen 4 { align1 1Q };
send(16)        g5UD            g2UD            0x04805001
                            hdc1 MsgDesc: ( untyped surface read, Surface = 1, SIMD16, Mask = 0x0) mlen 2 rlen 8 { align1 1H };
send(8)         g124UD          g13UD           0x084b0001
                            sampler MsgDesc: gather4_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(16)        g120UD          g7UD            0x0e8d0001
                            sampler MsgDesc: gather4_c SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 8 { align1 1H };
send(8)         g10UD           g10UD           0x0e134001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 7 rlen 1 { align1 1Q };
send(8)         g11UD           g17UD           0x0e134102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 7 rlen 1 { align1 1Q };
send(8)         g14UD           g10UD           0x064a8202
                            sampler MsgDesc: gather4 SIMD8 Surface = 2 Sampler = 2 mlen 3 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x084a8101
                            sampler MsgDesc: gather4 SIMD8 Surface = 1 Sampler = 1 mlen 4 rlen 4 { align1 1Q };
send(8)         g5UD            g6UD            0x021ab001
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 1 { align1 1Q };
send(16)        g6UD            g3UD            0x022cb001
                            sampler MsgDesc: sampleinfo SIMD16 Surface = 1 Sampler = 0 mlen 1 rlen 2 { align1 1H };
send(8)         nullUD          g122UD          0x8c0a0037
                            urb MsgDesc: 3 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         nullUD          g10UD           0x12080027
                            urb MsgDesc: 2 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g123UD          0x8a080047
                            urb MsgDesc: 4 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         g14UD           g2UD            0x04438000
                            sampler MsgDesc: sample_lz SIMD8 Surface = 0 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(8)         g61UD           g107UD          0x02380048
                            urb MsgDesc: 4 SIMD8 read mlen 1 rlen 3         { align1 1Q };
send(8)         g64UD           g113UD          0x02380058
                            urb MsgDesc: 5 SIMD8 read mlen 1 rlen 3         { align1 1Q };
send(8)         nullUD          g119UD          0x92080047
                            urb MsgDesc: 4 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         g5UD            g4UD            0x06415001
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD16, Mask = 0x0) mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x06416001
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD8, Mask = 0x0) mlen 3 rlen 4 { align1 2Q };
send(8)         nullUD          g119UD          0x92080077
                            urb MsgDesc: 7 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         g12UD           g8UD            0x044a0038
                            urb MsgDesc: 3 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g21UD           g8UD            0x044a0048
                            urb MsgDesc: 4 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         nullUD          g22UD           0x0c0a00a7
                            urb MsgDesc: 10 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(16)        g1UD            g9UD            0x08858001
                            sampler MsgDesc: sample_lz SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(8)         nullUD          g56UD           0x140a0097
                            urb MsgDesc: 9 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g76UD           0x0c0a00b7
                            urb MsgDesc: 11 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g122UD          0x8c0a00b7
                            urb MsgDesc: 11 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         g4UD            g3UD            0x0232a001
                            sampler MsgDesc: resinfo SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 3 { align1 1Q };
send(16)        g8UD            g3UD            0x0464a001
                            sampler MsgDesc: resinfo SIMD16 Surface = 1 Sampler = 0 mlen 2 rlen 6 { align1 1H };
send(8)         nullUD          g6UD            0x0a080007
                            urb MsgDesc: 0 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         g126UD          g10UD           0x08123001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 1 { align1 1Q };
send(16)        g124UD          g8UD            0x10243001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 2 { align1 1H };
send(8)         g12UD           g12UD           0x06126001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 1 { align1 1Q };
send(8)         g13UD           g15UD           0x06126102
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 1 { align1 1Q };
send(16)        g20UD           g22UD           0x0c246001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 2 { align1 1H };
send(16)        g22UD           g28UD           0x0c246102
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 2 Sampler = 1 mlen 6 rlen 2 { align1 1H };
send(8)         g4UD            g0UD            0x02201000
                            pi MsgDesc: (persp, sample_position, 0x00) mlen 1 rlen 2 { align1 1Q };
send(16)        g6UD            g0UD            0x02411000
                            pi MsgDesc: (persp, sample_position, 0x00) mlen 1 rlen 4 { align1 1H };
send(8)         g124UD          g19UD           0x0a4b0001
                            sampler MsgDesc: gather4_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(16)        g120UD          g7UD            0x128d0001
                            sampler MsgDesc: gather4_c SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 8 { align1 1H };
send(8)         g2UD            g15UD           0x06422001
                            sampler MsgDesc: sample_l SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(16)        g14UD           g8UD            0x0c842001
                            sampler MsgDesc: sample_l SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 8 { align1 1H };
send(8)         nullUD          g118UD          0x940a0037
                            urb MsgDesc: 3 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q EOT };
send(8)         g4UD            g5UD            0x0212a001
                            sampler MsgDesc: resinfo SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 1 { align1 1Q };
send(16)        g4UD            g6UD            0x0424a001
                            sampler MsgDesc: resinfo SIMD16 Surface = 1 Sampler = 0 mlen 2 rlen 2 { align1 1H };
send(8)         g8UD            g15UD           0x042a0138
                            urb MsgDesc: 19 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g10UD           g15UD           0x042a0338
                            urb MsgDesc: 51 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g12UD           g15UD           0x042a0538
                            urb MsgDesc: 83 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g14UD           g15UD           0x042a0738
                            urb MsgDesc: 115 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g8UD            g15UD           0x042a0038
                            urb MsgDesc: 3 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g10UD           g15UD           0x042a0238
                            urb MsgDesc: 35 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g12UD           g15UD           0x042a0438
                            urb MsgDesc: 67 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g14UD           g15UD           0x042a0638
                            urb MsgDesc: 99 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g35UD           0x02480228
                            urb MsgDesc: 34 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g8UD            g35UD           0x02480428
                            urb MsgDesc: 66 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g35UD           0x02480628
                            urb MsgDesc: 98 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         nullUD          g6UD            0x0a0a8037
                            urb MsgDesc: 3 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g11UD           0x0a0a8047
                            urb MsgDesc: 4 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g12UD           0x0a0a8057
                            urb MsgDesc: 5 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x0a0a8067
                            urb MsgDesc: 6 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g14UD           0x0a0a8077
                            urb MsgDesc: 7 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x0a0a8087
                            urb MsgDesc: 8 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x0a0a8097
                            urb MsgDesc: 9 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x0a0a80a7
                            urb MsgDesc: 10 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x0a0a80b7
                            urb MsgDesc: 11 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x0a0a80c7
                            urb MsgDesc: 12 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x0a0a80d7
                            urb MsgDesc: 13 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x0a0a80e7
                            urb MsgDesc: 14 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x0a0a80f7
                            urb MsgDesc: 15 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x0a0a8107
                            urb MsgDesc: 16 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x0a0a8117
                            urb MsgDesc: 17 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x0a0a8127
                            urb MsgDesc: 18 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x0a0a8137
                            urb MsgDesc: 19 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x0a0a8147
                            urb MsgDesc: 20 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x0a0a8157
                            urb MsgDesc: 21 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x0a0a8167
                            urb MsgDesc: 22 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x0a0a8177
                            urb MsgDesc: 23 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x0a0a8187
                            urb MsgDesc: 24 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x0a0a8197
                            urb MsgDesc: 25 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x0a0a81a7
                            urb MsgDesc: 26 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x0a0a81b7
                            urb MsgDesc: 27 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x0a0a81c7
                            urb MsgDesc: 28 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x0a0a81d7
                            urb MsgDesc: 29 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x0a0a81e7
                            urb MsgDesc: 30 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x0a0a81f7
                            urb MsgDesc: 31 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x0a0a8207
                            urb MsgDesc: 32 SIMD8 write per-slot masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g122UD          0x8c0a0027
                            urb MsgDesc: 2 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         g124UD          g2UD            0x06424001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         g124UD          g2UD            0x06229001
                            sampler MsgDesc: lod SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 2 { align1 1Q };
send(16)        g120UD          g12UD           0x0c449001
                            sampler MsgDesc: lod SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1H };
send(8)         g5UD            g19UD           0x0443a102
                            sampler MsgDesc: ld_lz SIMD8 Surface = 2 Sampler = 1 mlen 2 rlen 4 { align1 1Q };
send(16)        g15UD           g11UD           0x0885a102
                            sampler MsgDesc: ld_lz SIMD16 Surface = 2 Sampler = 1 mlen 4 rlen 8 { align1 1H };
send(8)         g124UD          g12UD           0x0a43c000
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 0 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(8)         g4UD            g5UD            0x04120001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 1 { align1 1Q };
send(16)        g4UD            g7UD            0x08240001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 2 { align1 1H };
send(8)         nullUD          g118UD          0x940a0027
                            urb MsgDesc: 2 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q EOT };
send(8)         nullUD          g2UD            0x12080067
                            urb MsgDesc: 6 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g123UD          0x8a080087
                            urb MsgDesc: 8 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         g21UD           g2UD            0x02380068
                            urb MsgDesc: 6 SIMD8 read mlen 1 rlen 3         { align1 1Q };
send(8)         g35UD           g2UD            0x02380088
                            urb MsgDesc: 8 SIMD8 read mlen 1 rlen 3         { align1 1Q };
send(8)         nullUD          g5UD            0x140a0067
                            urb MsgDesc: 6 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g118UD          0x940a0067
                            urb MsgDesc: 6 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q EOT };
send(8)         g2UD            g8UD            0x04220001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 2 { align1 1Q };
send(16)        g2UD            g14UD           0x08440001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1H };
send(8)         nullUD          g123UD          0x8a0800d7
                            urb MsgDesc: 13 SIMD8 write mlen 5 rlen 0       { align1 1Q EOT };
send(8)         g22UD           g14UD           0x064a8405
                            sampler MsgDesc: gather4 SIMD8 Surface = 5 Sampler = 4 mlen 3 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x084a8102
                            sampler MsgDesc: gather4 SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 4 { align1 1Q };
send(8)         g14UD           g10UD           0x084a8203
                            sampler MsgDesc: gather4 SIMD8 Surface = 3 Sampler = 2 mlen 4 rlen 4 { align1 1Q };
send(8)         g18UD           g26UD           0x0a4a8304
                            sampler MsgDesc: gather4 SIMD8 Surface = 4 Sampler = 3 mlen 5 rlen 4 { align1 1Q };
send(16)        g18UD           g43UD           0x0a8c8405
                            sampler MsgDesc: gather4 SIMD16 Surface = 5 Sampler = 4 mlen 5 rlen 8 { align1 1H };
send(16)        g43UD           g7UD            0x0e8c8102
                            sampler MsgDesc: gather4 SIMD16 Surface = 2 Sampler = 1 mlen 7 rlen 8 { align1 1H };
send(16)        g2UD            g51UD           0x0e8c8203
                            sampler MsgDesc: gather4 SIMD16 Surface = 3 Sampler = 2 mlen 7 rlen 8 { align1 1H };
send(16)        g10UD           g26UD           0x128c8304
                            sampler MsgDesc: gather4 SIMD16 Surface = 4 Sampler = 3 mlen 9 rlen 8 { align1 1H };
send(8)         g6UD            g15UD           0x0e4a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 7 rlen 4 { align1 1Q };
send(16)        nullUD          g2UD            0x04008601
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 1, SIMD16, dec) mlen 2 rlen 0 { align1 1H };
send(8)         g124UD          g2UD            0x08422001
                            sampler MsgDesc: sample_l SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(16)        g120UD          g2UD            0x10842001
                            sampler MsgDesc: sample_l SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 8 { align1 1H };
send(8)         g6UD            g7UD            0x08126001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 1 { align1 1Q };
send(8)         g7UD            g11UD           0x08126102
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 1 { align1 1Q };
send(16)        g10UD           g12UD           0x10246001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 2 { align1 1H };
send(16)        g12UD           g20UD           0x10246102
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 2 Sampler = 1 mlen 8 rlen 2 { align1 1H };
send(8)         nullUD          g18UD           0x0e0a8047
                            urb MsgDesc: 4 SIMD8 write per-slot masked mlen 7 rlen 0 { align1 1Q };
send(8)         g9UD            g34UD           0x02480218
                            urb MsgDesc: 33 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g17UD           g34UD           0x02480238
                            urb MsgDesc: 35 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g2UD            g6UD            0x041a0128
                            urb MsgDesc: 18 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g22UD           g8UD            0x041a0028
                            urb MsgDesc: 2 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         nullUD          g2UD            0x06088027
                            urb MsgDesc: 2 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g12UD           0x06088037
                            urb MsgDesc: 3 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x06088047
                            urb MsgDesc: 4 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g14UD           0x06088057
                            urb MsgDesc: 5 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x06088067
                            urb MsgDesc: 6 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x06088077
                            urb MsgDesc: 7 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x06088087
                            urb MsgDesc: 8 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x06088097
                            urb MsgDesc: 9 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x060880a7
                            urb MsgDesc: 10 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x060880b7
                            urb MsgDesc: 11 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x060880c7
                            urb MsgDesc: 12 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x060880d7
                            urb MsgDesc: 13 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x060880e7
                            urb MsgDesc: 14 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x060880f7
                            urb MsgDesc: 15 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x06088107
                            urb MsgDesc: 16 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x06088117
                            urb MsgDesc: 17 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x06088127
                            urb MsgDesc: 18 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x06088137
                            urb MsgDesc: 19 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x06088147
                            urb MsgDesc: 20 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x06088157
                            urb MsgDesc: 21 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x06088167
                            urb MsgDesc: 22 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x06088177
                            urb MsgDesc: 23 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x06088187
                            urb MsgDesc: 24 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x06088197
                            urb MsgDesc: 25 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x060881a7
                            urb MsgDesc: 26 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x060881b7
                            urb MsgDesc: 27 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x060881c7
                            urb MsgDesc: 28 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x060881d7
                            urb MsgDesc: 29 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x060881e7
                            urb MsgDesc: 30 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         nullUD          g40UD           0x060881f7
                            urb MsgDesc: 31 SIMD8 write masked mlen 3 rlen 0 { align1 1Q };
send(8)         g3UD            g10UD           0x0242a001
                            sampler MsgDesc: resinfo SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(16)        g3UD            g11UD           0x0484a001
                            sampler MsgDesc: resinfo SIMD16 Surface = 1 Sampler = 0 mlen 2 rlen 8 { align1 1H };
send(8)         g124UD          g6UD            0x06320001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 3 { align1 1Q };
send(16)        g120UD          g8UD            0x0c640001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 6 { align1 1H };
send(8)         g124UD          g2UD            0x02406000
                            hdc1 MsgDesc: ( untyped surface read, Surface = 0, SIMD8, Mask = 0x0) mlen 1 rlen 4 { align1 1Q };
send(8)         g127UD          g6UD            0x06120001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 1 { align1 1Q };
send(16)        g126UD          g8UD            0x0c240001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 2 { align1 1H };
send(8)         g23UD           g2UD            0x04115e01
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD16, Mask = 0xe) mlen 2 rlen 1 { align1 1Q };
send(8)         g39UD           g45UD           0x04116e01
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD8, Mask = 0xe) mlen 2 rlen 1 { align1 2Q };
(+f1.0) send(8) nullUD          g2UD            0x04018501
                            hdc1 MsgDesc: ( DC typed atomic, Surface = 1, SIMD16, inc) mlen 2 rlen 0 { align1 1Q };
(+f1.0) send(8) nullUD          g42UD           0x04019501
                            hdc1 MsgDesc: ( DC typed atomic, Surface = 1, SIMD8, inc) mlen 2 rlen 0 { align1 2Q };
send(8)         g2UD            g6UD            0x04423001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(8)         g6UD            g8UD            0x04423102
                            sampler MsgDesc: sample_c SIMD8 Surface = 2 Sampler = 1 mlen 2 rlen 4 { align1 1Q };
send(16)        g2UD            g10UD           0x08843001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(16)        g10UD           g18UD           0x08843102
                            sampler MsgDesc: sample_c SIMD16 Surface = 2 Sampler = 1 mlen 4 rlen 8 { align1 1H };
send(8)         g6UD            g22UD           0x044a0318
                            urb MsgDesc: 49 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g10UD           g22UD           0x044a0518
                            urb MsgDesc: 81 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g14UD           g22UD           0x044a0718
                            urb MsgDesc: 113 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g18UD           g22UD           0x044a0918
                            urb MsgDesc: 145 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g13UD           g29UD           0x044a0218
                            urb MsgDesc: 33 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g17UD           g29UD           0x044a0418
                            urb MsgDesc: 65 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g21UD           g29UD           0x044a0618
                            urb MsgDesc: 97 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g25UD           g29UD           0x044a0818
                            urb MsgDesc: 129 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         nullUD          g6UD            0x0c0a0217
                            urb MsgDesc: 33 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g12UD           0x0c0a0227
                            urb MsgDesc: 34 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x0c0a0237
                            urb MsgDesc: 35 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g14UD           0x0c0a0247
                            urb MsgDesc: 36 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x0c0a0257
                            urb MsgDesc: 37 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x0c0a0267
                            urb MsgDesc: 38 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x0c0a0277
                            urb MsgDesc: 39 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x0c0a0287
                            urb MsgDesc: 40 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x0c0a0297
                            urb MsgDesc: 41 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x0c0a02a7
                            urb MsgDesc: 42 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x0c0a02b7
                            urb MsgDesc: 43 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x0c0a02c7
                            urb MsgDesc: 44 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x0c0a02d7
                            urb MsgDesc: 45 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x0c0a02e7
                            urb MsgDesc: 46 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x0c0a02f7
                            urb MsgDesc: 47 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x0c0a0307
                            urb MsgDesc: 48 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x0c0a0317
                            urb MsgDesc: 49 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x0c0a0327
                            urb MsgDesc: 50 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x0c0a0337
                            urb MsgDesc: 51 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x0c0a0347
                            urb MsgDesc: 52 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x0c0a0357
                            urb MsgDesc: 53 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x0c0a0367
                            urb MsgDesc: 54 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x0c0a0377
                            urb MsgDesc: 55 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x0c0a0387
                            urb MsgDesc: 56 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x0c0a0397
                            urb MsgDesc: 57 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x0c0a03a7
                            urb MsgDesc: 58 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x0c0a03b7
                            urb MsgDesc: 59 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x0c0a03c7
                            urb MsgDesc: 60 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x0c0a03d7
                            urb MsgDesc: 61 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g40UD           0x0c0a03e7
                            urb MsgDesc: 62 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g41UD           0x0c0a03f7
                            urb MsgDesc: 63 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x0a080067
                            urb MsgDesc: 6 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         nullUD          g17UD           0x0a080077
                            urb MsgDesc: 7 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         nullUD          g18UD           0x0a080087
                            urb MsgDesc: 8 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         nullUD          g19UD           0x0a080097
                            urb MsgDesc: 9 SIMD8 write mlen 5 rlen 0        { align1 1Q };
send(8)         nullUD          g20UD           0x0a0800a7
                            urb MsgDesc: 10 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g21UD           0x0a0800b7
                            urb MsgDesc: 11 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g22UD           0x0a0800c7
                            urb MsgDesc: 12 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g23UD           0x0a0800d7
                            urb MsgDesc: 13 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g24UD           0x0a0800e7
                            urb MsgDesc: 14 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g25UD           0x0a0800f7
                            urb MsgDesc: 15 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g26UD           0x0a080107
                            urb MsgDesc: 16 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g27UD           0x0a080117
                            urb MsgDesc: 17 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g28UD           0x0a080127
                            urb MsgDesc: 18 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g29UD           0x0a080137
                            urb MsgDesc: 19 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g30UD           0x0a080147
                            urb MsgDesc: 20 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g31UD           0x0a080157
                            urb MsgDesc: 21 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g32UD           0x0a080167
                            urb MsgDesc: 22 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g33UD           0x0a080177
                            urb MsgDesc: 23 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g34UD           0x0a080187
                            urb MsgDesc: 24 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g35UD           0x0a080197
                            urb MsgDesc: 25 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g36UD           0x0a0801a7
                            urb MsgDesc: 26 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g37UD           0x0a0801b7
                            urb MsgDesc: 27 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g38UD           0x0a0801c7
                            urb MsgDesc: 28 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g39UD           0x0a0801d7
                            urb MsgDesc: 29 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g40UD           0x0a0801e7
                            urb MsgDesc: 30 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         nullUD          g41UD           0x0a0801f7
                            urb MsgDesc: 31 SIMD8 write mlen 5 rlen 0       { align1 1Q };
send(8)         g13UD           g2UD            0x06123001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 1 { align1 1Q };
send(8)         g14UD           g5UD            0x06123102
                            sampler MsgDesc: sample_c SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 1 { align1 1Q };
send(16)        g22UD           g2UD            0x0c243001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 2 { align1 1H };
send(16)        g24UD           g16UD           0x0c243102
                            sampler MsgDesc: sample_c SIMD16 Surface = 2 Sampler = 1 mlen 6 rlen 2 { align1 1H };
send(8)         g5UD            g15UD           0x04420203
                            sampler MsgDesc: sample SIMD8 Surface = 3 Sampler = 2 mlen 2 rlen 4 { align1 1Q };
send(16)        g7UD            g27UD           0x08840203
                            sampler MsgDesc: sample SIMD16 Surface = 3 Sampler = 2 mlen 4 rlen 8 { align1 1H };
send(16)        g4UD            g17UD           0x0420a503
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 3, SIMD16, inc) mlen 2 rlen 2 { align1 1H };
send(16)        nullUD          g18UD           0x04008504
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 4, SIMD16, inc) mlen 2 rlen 0 { align1 1H };
send(16)        g11UD           g19UD           0x0420a602
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 2, SIMD16, dec) mlen 2 rlen 2 { align1 1H };
send(16)        nullUD          g20UD           0x04008505
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 5, SIMD16, inc) mlen 2 rlen 0 { align1 1H };
send(16)        g16UD           g21UD           0x04205e01
                            hdc1 MsgDesc: ( untyped surface read, Surface = 1, SIMD16, Mask = 0xe) mlen 2 rlen 2 { align1 1H };
send(16)        nullUD          g22UD           0x04008506
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 6, SIMD16, inc) mlen 2 rlen 0 { align1 1H };
send(8)         g26UD           g26UD           0x0242a203
                            sampler MsgDesc: resinfo SIMD8 Surface = 3 Sampler = 2 mlen 1 rlen 4 { align1 1Q };
send(8)         g30UD           g30UD           0x0242a304
                            sampler MsgDesc: resinfo SIMD8 Surface = 4 Sampler = 3 mlen 1 rlen 4 { align1 1Q };
send(8)         g34UD           g34UD           0x0242a405
                            sampler MsgDesc: resinfo SIMD8 Surface = 5 Sampler = 4 mlen 1 rlen 4 { align1 1Q };
send(8)         g38UD           g38UD           0x0242a506
                            sampler MsgDesc: resinfo SIMD8 Surface = 6 Sampler = 5 mlen 1 rlen 4 { align1 1Q };
send(8)         g22UD           g25UD           0x0242a102
                            sampler MsgDesc: resinfo SIMD8 Surface = 2 Sampler = 1 mlen 1 rlen 4 { align1 1Q };
send(8)         g42UD           g42UD           0x0242a607
                            sampler MsgDesc: resinfo SIMD8 Surface = 7 Sampler = 6 mlen 1 rlen 4 { align1 1Q };
send(8)         g46UD           g46UD           0x0242a708
                            sampler MsgDesc: resinfo SIMD8 Surface = 8 Sampler = 7 mlen 1 rlen 4 { align1 1Q };
send(8)         g50UD           g50UD           0x0242a809
                            sampler MsgDesc: resinfo SIMD8 Surface = 9 Sampler = 8 mlen 1 rlen 4 { align1 1Q };
send(8)         g2UD            g54UD           0x0242a90a
                            sampler MsgDesc: resinfo SIMD8 Surface = 10 Sampler = 9 mlen 1 rlen 4 { align1 1Q };
send(8)         g6UD            g55UD           0x0242aa0b
                            sampler MsgDesc: resinfo SIMD8 Surface = 11 Sampler = 10 mlen 1 rlen 4 { align1 1Q };
send(8)         g10UD           g56UD           0x0242ab0c
                            sampler MsgDesc: resinfo SIMD8 Surface = 12 Sampler = 11 mlen 1 rlen 4 { align1 1Q };
send(8)         g14UD           g57UD           0x0242ac0d
                            sampler MsgDesc: resinfo SIMD8 Surface = 13 Sampler = 12 mlen 1 rlen 4 { align1 1Q };
send(16)        g10UD           g18UD           0x0484a102
                            sampler MsgDesc: resinfo SIMD16 Surface = 2 Sampler = 1 mlen 2 rlen 8 { align1 1H };
send(16)        g82UD           g110UD          0x0484aa0b
                            sampler MsgDesc: resinfo SIMD16 Surface = 11 Sampler = 10 mlen 2 rlen 8 { align1 1H };
send(16)        g18UD           g26UD           0x0484a203
                            sampler MsgDesc: resinfo SIMD16 Surface = 3 Sampler = 2 mlen 2 rlen 8 { align1 1H };
send(16)        g90UD           g112UD          0x0484ab0c
                            sampler MsgDesc: resinfo SIMD16 Surface = 12 Sampler = 11 mlen 2 rlen 8 { align1 1H };
send(16)        g98UD           g106UD          0x0484ac0d
                            sampler MsgDesc: resinfo SIMD16 Surface = 13 Sampler = 12 mlen 2 rlen 8 { align1 1H };
send(16)        g26UD           g34UD           0x0484a304
                            sampler MsgDesc: resinfo SIMD16 Surface = 4 Sampler = 3 mlen 2 rlen 8 { align1 1H };
send(16)        g34UD           g42UD           0x0484a405
                            sampler MsgDesc: resinfo SIMD16 Surface = 5 Sampler = 4 mlen 2 rlen 8 { align1 1H };
send(16)        g42UD           g50UD           0x0484a506
                            sampler MsgDesc: resinfo SIMD16 Surface = 6 Sampler = 5 mlen 2 rlen 8 { align1 1H };
send(16)        g50UD           g58UD           0x0484a607
                            sampler MsgDesc: resinfo SIMD16 Surface = 7 Sampler = 6 mlen 2 rlen 8 { align1 1H };
send(16)        g58UD           g66UD           0x0484a708
                            sampler MsgDesc: resinfo SIMD16 Surface = 8 Sampler = 7 mlen 2 rlen 8 { align1 1H };
send(16)        g66UD           g74UD           0x0484a809
                            sampler MsgDesc: resinfo SIMD16 Surface = 9 Sampler = 8 mlen 2 rlen 8 { align1 1H };
send(16)        g74UD           g108UD          0x0484a90a
                            sampler MsgDesc: resinfo SIMD16 Surface = 10 Sampler = 9 mlen 2 rlen 8 { align1 1H };
send(16)        nullUD          g3UD            0x040085fe
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 254, SIMD16, inc) mlen 2 rlen 0 { align1 1H };
send(8)         nullUD          g119UD          0x92080067
                            urb MsgDesc: 6 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         g6UD            g20UD           0x12424001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 9 rlen 4 { align1 1Q };
send(8)         g17UD           g2UD            0x0413a001
                            sampler MsgDesc: ld_lz SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 1 { align1 1Q };
send(16)        g2UD            g7UD            0x0825a001
                            sampler MsgDesc: ld_lz SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 2 { align1 1H };
send(8)         g9UD            g17UD           0x06422000
                            sampler MsgDesc: sample_l SIMD8 Surface = 0 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(16)        nullUD          g123UD          0x060a03fd
                            hdc0 MsgDesc: ( DC OWORD block write, 253, 3) mlen 3 rlen 0 { align1 1H };
send(16)        g114UD          g114UD          0x022803fd
                            hdc0 MsgDesc: ( DC OWORD block read, 253, 3) mlen 1 rlen 2 { align1 WE_all 1H };
send(8)         nullUD          g12UD           0x0c0a0127
                            urb MsgDesc: 18 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         g2UD            g11UD           0x04420405
                            sampler MsgDesc: sample SIMD8 Surface = 5 Sampler = 4 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g12UD           0x04420506
                            sampler MsgDesc: sample SIMD8 Surface = 6 Sampler = 5 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x04420607
                            sampler MsgDesc: sample SIMD8 Surface = 7 Sampler = 6 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g14UD           0x04420708
                            sampler MsgDesc: sample SIMD8 Surface = 8 Sampler = 7 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g15UD           0x04420809
                            sampler MsgDesc: sample SIMD8 Surface = 9 Sampler = 8 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g16UD           0x0442090a
                            sampler MsgDesc: sample SIMD8 Surface = 10 Sampler = 9 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g17UD           0x04420a0b
                            sampler MsgDesc: sample SIMD8 Surface = 11 Sampler = 10 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g18UD           0x04420b0c
                            sampler MsgDesc: sample SIMD8 Surface = 12 Sampler = 11 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g19UD           0x04420c0d
                            sampler MsgDesc: sample SIMD8 Surface = 13 Sampler = 12 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g20UD           0x04420d0e
                            sampler MsgDesc: sample SIMD8 Surface = 14 Sampler = 13 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g21UD           0x04420e0f
                            sampler MsgDesc: sample SIMD8 Surface = 15 Sampler = 14 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g22UD           0x04420f10
                            sampler MsgDesc: sample SIMD8 Surface = 16 Sampler = 15 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x064a0011
                            sampler MsgDesc: sample SIMD8 Surface = 17 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x064a0112
                            sampler MsgDesc: sample SIMD8 Surface = 18 Sampler = 1 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x064a0213
                            sampler MsgDesc: sample SIMD8 Surface = 19 Sampler = 2 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x064a0314
                            sampler MsgDesc: sample SIMD8 Surface = 20 Sampler = 3 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x064a0415
                            sampler MsgDesc: sample SIMD8 Surface = 21 Sampler = 4 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x064a0516
                            sampler MsgDesc: sample SIMD8 Surface = 22 Sampler = 5 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x064a0617
                            sampler MsgDesc: sample SIMD8 Surface = 23 Sampler = 6 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x064a0718
                            sampler MsgDesc: sample SIMD8 Surface = 24 Sampler = 7 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x064a0819
                            sampler MsgDesc: sample SIMD8 Surface = 25 Sampler = 8 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x064a091a
                            sampler MsgDesc: sample SIMD8 Surface = 26 Sampler = 9 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x064a0a1b
                            sampler MsgDesc: sample SIMD8 Surface = 27 Sampler = 10 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x064a0b1c
                            sampler MsgDesc: sample SIMD8 Surface = 28 Sampler = 11 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x064a0c1d
                            sampler MsgDesc: sample SIMD8 Surface = 29 Sampler = 12 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x064a0d1e
                            sampler MsgDesc: sample SIMD8 Surface = 30 Sampler = 13 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g10UD           0x064a0e1f
                            sampler MsgDesc: sample SIMD8 Surface = 31 Sampler = 14 mlen 3 rlen 4 { align1 1Q };
send(8)         g2UD            g13UD           0x064a0f20
                            sampler MsgDesc: sample SIMD8 Surface = 32 Sampler = 15 mlen 3 rlen 4 { align1 1Q };
send(16)        g2UD            g28UD           0x08840405
                            sampler MsgDesc: sample SIMD16 Surface = 5 Sampler = 4 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g29UD           0x08840506
                            sampler MsgDesc: sample SIMD16 Surface = 6 Sampler = 5 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g30UD           0x08840607
                            sampler MsgDesc: sample SIMD16 Surface = 7 Sampler = 6 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g31UD           0x08840708
                            sampler MsgDesc: sample SIMD16 Surface = 8 Sampler = 7 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g32UD           0x08840809
                            sampler MsgDesc: sample SIMD16 Surface = 9 Sampler = 8 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g33UD           0x0884090a
                            sampler MsgDesc: sample SIMD16 Surface = 10 Sampler = 9 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g34UD           0x08840a0b
                            sampler MsgDesc: sample SIMD16 Surface = 11 Sampler = 10 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g35UD           0x08840b0c
                            sampler MsgDesc: sample SIMD16 Surface = 12 Sampler = 11 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g36UD           0x08840c0d
                            sampler MsgDesc: sample SIMD16 Surface = 13 Sampler = 12 mlen 4 rlen 8 { align1 1H };
send(16)        g2UD            g37UD           0x08840d0e
                            sampler MsgDesc: sample SIMD16 Surface = 14 Sampler = 13 mlen 4 rlen 8 { align1 1H };
send(16)        g7UD            g38UD           0x08840e0f
                            sampler MsgDesc: sample SIMD16 Surface = 15 Sampler = 14 mlen 4 rlen 8 { align1 1H };
send(16)        g23UD           g39UD           0x08840f10
                            sampler MsgDesc: sample SIMD16 Surface = 16 Sampler = 15 mlen 4 rlen 8 { align1 1H };
send(16)        g17UD           g2UD            0x0a8c0011
                            sampler MsgDesc: sample SIMD16 Surface = 17 Sampler = 0 mlen 5 rlen 8 { align1 1H };
send(16)        g29UD           g7UD            0x0a8c0112
                            sampler MsgDesc: sample SIMD16 Surface = 18 Sampler = 1 mlen 5 rlen 8 { align1 1H };
send(16)        g27UD           g12UD           0x0a8c0213
                            sampler MsgDesc: sample SIMD16 Surface = 19 Sampler = 2 mlen 5 rlen 8 { align1 1H };
send(16)        g32UD           g17UD           0x0a8c0314
                            sampler MsgDesc: sample SIMD16 Surface = 20 Sampler = 3 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g22UD           0x0a8c0415
                            sampler MsgDesc: sample SIMD16 Surface = 21 Sampler = 4 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g27UD           0x0a8c0516
                            sampler MsgDesc: sample SIMD16 Surface = 22 Sampler = 5 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g32UD           0x0a8c0617
                            sampler MsgDesc: sample SIMD16 Surface = 23 Sampler = 6 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g37UD           0x0a8c0718
                            sampler MsgDesc: sample SIMD16 Surface = 24 Sampler = 7 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g42UD           0x0a8c0819
                            sampler MsgDesc: sample SIMD16 Surface = 25 Sampler = 8 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g47UD           0x0a8c091a
                            sampler MsgDesc: sample SIMD16 Surface = 26 Sampler = 9 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g52UD           0x0a8c0a1b
                            sampler MsgDesc: sample SIMD16 Surface = 27 Sampler = 10 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g57UD           0x0a8c0b1c
                            sampler MsgDesc: sample SIMD16 Surface = 28 Sampler = 11 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g62UD           0x0a8c0c1d
                            sampler MsgDesc: sample SIMD16 Surface = 29 Sampler = 12 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g67UD           0x0a8c0d1e
                            sampler MsgDesc: sample SIMD16 Surface = 30 Sampler = 13 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g72UD           0x0a8c0e1f
                            sampler MsgDesc: sample SIMD16 Surface = 31 Sampler = 14 mlen 5 rlen 8 { align1 1H };
send(16)        g2UD            g77UD           0x0a8c0f20
                            sampler MsgDesc: sample SIMD16 Surface = 32 Sampler = 15 mlen 5 rlen 8 { align1 1H };
send(8)         g124UD          g2UD            0x02120102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 1 rlen 1 { align1 1Q };
send(8)         g6UD            g3UD            0x02220102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 1 rlen 2 { align1 1Q };
send(8)         g8UD            g4UD            0x02320102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 1 rlen 3 { align1 1Q };
send(16)        g120UD          g2UD            0x04240102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 2 rlen 2 { align1 1H };
send(16)        g10UD           g4UD            0x04440102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 2 rlen 4 { align1 1H };
send(16)        g14UD           g6UD            0x04640102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 2 rlen 6 { align1 1H };
send(8)         nullUD          g8UD            0x0c0a8027
                            urb MsgDesc: 2 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x12080047
                            urb MsgDesc: 4 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g119UD          0x92080087
                            urb MsgDesc: 8 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         g5UD            g10UD           0x06420001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(16)        g7UD            g19UD           0x0c840001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 8 { align1 1H };
send(8)         g1UD            g125UD          0x02106e02
                            hdc1 MsgDesc: ( untyped surface read, Surface = 2, SIMD8, Mask = 0xe) mlen 1 rlen 1 { align1 1Q };
send(8)         g8UD            g22UD           0x02106efe
                            hdc1 MsgDesc: ( untyped surface read, Surface = 254, SIMD8, Mask = 0xe) mlen 1 rlen 1 { align1 1Q };
send(8)         nullUD          g123UD          0x8a080097
                            urb MsgDesc: 9 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         g29UD           g5UD            0x0e4b2001
                            sampler MsgDesc: gather4_po_c SIMD8 Surface = 1 Sampler = 0 mlen 7 rlen 4 { align1 1Q };
send(8)         g68UD           g72UD           0x0212a102
                            sampler MsgDesc: resinfo SIMD8 Surface = 2 Sampler = 1 mlen 1 rlen 1 { align1 1Q };
send(8)         g67UD           g5UD            0x0a126001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 1 { align1 1Q };
send(8)         g69UD           g10UD           0x0a126102
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 1 { align1 1Q };
send(16)        g36UD           g40UD           0x0424a102
                            sampler MsgDesc: resinfo SIMD16 Surface = 2 Sampler = 1 mlen 2 rlen 2 { align1 1H };
send(16)        g2UD            g7UD            0x14246001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 2 { align1 1H };
send(16)        g37UD           g17UD           0x14246102
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 2 Sampler = 1 mlen 10 rlen 2 { align1 1H };
send(8)         g125UD          g5UD            0x04220102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 2 rlen 2 { align1 1Q };
send(16)        g122UD          g7UD            0x08440102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 4 rlen 4 { align1 1H };
send(8)         nullUD          g14UD           0x0c0a8037
                            urb MsgDesc: 3 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x0c0a8047
                            urb MsgDesc: 4 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x0c0a8057
                            urb MsgDesc: 5 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         g6UD            g7UD            0x081a5001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 1 { align1 1Q };
send(8)         g7UD            g11UD           0x081a5102
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 1 { align1 1Q };
send(16)        g10UD           g12UD           0x0e2c5001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 2 { align1 1H };
send(16)        g12UD           g19UD           0x0e2c5102
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 2 Sampler = 1 mlen 7 rlen 2 { align1 1H };
send(8)         g5UD            g6UD            0x081a3001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 1 { align1 1Q };
send(8)         g6UD            g10UD           0x081a3102
                            sampler MsgDesc: sample_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 1 { align1 1Q };
send(16)        g9UD            g11UD           0x0e2c3001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 2 { align1 1H };
send(16)        g11UD           g18UD           0x0e2c3102
                            sampler MsgDesc: sample_c SIMD16 Surface = 2 Sampler = 1 mlen 7 rlen 2 { align1 1H };
send(8)         g5UD            g7UD            0x04320102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 2 rlen 3 { align1 1Q };
send(16)        g8UD            g14UD           0x08640102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 4 rlen 6 { align1 1H };
send(8)         g19UD           g12UD           0x04320003
                            sampler MsgDesc: sample SIMD8 Surface = 3 Sampler = 0 mlen 2 rlen 3 { align1 1Q };
send(16)        g34UD           g41UD           0x08640003
                            sampler MsgDesc: sample SIMD16 Surface = 3 Sampler = 0 mlen 4 rlen 6 { align1 1H };
send(8)         g11UD           g2UD            0x0443a008
                            sampler MsgDesc: ld_lz SIMD8 Surface = 8 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(8)         g15UD           g2UD            0x0443a109
                            sampler MsgDesc: ld_lz SIMD8 Surface = 9 Sampler = 1 mlen 2 rlen 4 { align1 1Q };
send(8)         g19UD           g2UD            0x0443a20a
                            sampler MsgDesc: ld_lz SIMD8 Surface = 10 Sampler = 2 mlen 2 rlen 4 { align1 1Q };
send(8)         g23UD           g2UD            0x0443a30b
                            sampler MsgDesc: ld_lz SIMD8 Surface = 11 Sampler = 3 mlen 2 rlen 4 { align1 1Q };
send(8)         g27UD           g2UD            0x0443a40c
                            sampler MsgDesc: ld_lz SIMD8 Surface = 12 Sampler = 4 mlen 2 rlen 4 { align1 1Q };
send(8)         g31UD           g2UD            0x0443a50d
                            sampler MsgDesc: ld_lz SIMD8 Surface = 13 Sampler = 5 mlen 2 rlen 4 { align1 1Q };
send(8)         g35UD           g2UD            0x0443a60e
                            sampler MsgDesc: ld_lz SIMD8 Surface = 14 Sampler = 6 mlen 2 rlen 4 { align1 1Q };
send(8)         g39UD           g2UD            0x0443a70f
                            sampler MsgDesc: ld_lz SIMD8 Surface = 15 Sampler = 7 mlen 2 rlen 4 { align1 1Q };
send(16)        g93UD           g2UD            0x0885a008
                            sampler MsgDesc: ld_lz SIMD16 Surface = 8 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(16)        g27UD           g2UD            0x0885a109
                            sampler MsgDesc: ld_lz SIMD16 Surface = 9 Sampler = 1 mlen 4 rlen 8 { align1 1H };
send(16)        g37UD           g2UD            0x0885a20a
                            sampler MsgDesc: ld_lz SIMD16 Surface = 10 Sampler = 2 mlen 4 rlen 8 { align1 1H };
send(16)        g47UD           g2UD            0x0885a30b
                            sampler MsgDesc: ld_lz SIMD16 Surface = 11 Sampler = 3 mlen 4 rlen 8 { align1 1H };
send(16)        g57UD           g2UD            0x0885a40c
                            sampler MsgDesc: ld_lz SIMD16 Surface = 12 Sampler = 4 mlen 4 rlen 8 { align1 1H };
send(16)        g67UD           g2UD            0x0885a50d
                            sampler MsgDesc: ld_lz SIMD16 Surface = 13 Sampler = 5 mlen 4 rlen 8 { align1 1H };
send(16)        g85UD           g2UD            0x0885a60e
                            sampler MsgDesc: ld_lz SIMD16 Surface = 14 Sampler = 6 mlen 4 rlen 8 { align1 1H };
send(16)        g77UD           g2UD            0x0885a70f
                            sampler MsgDesc: ld_lz SIMD16 Surface = 15 Sampler = 7 mlen 4 rlen 8 { align1 1H };
send(16)        g83UD           g86UD           0x04205e00
                            hdc1 MsgDesc: ( untyped surface read, Surface = 0, SIMD16, Mask = 0xe) mlen 2 rlen 2 { align1 1H };
send(8)         nullUD          g122UD          0x8c0a0047
                            urb MsgDesc: 4 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         g14UD           g11UD           0x084b0202
                            sampler MsgDesc: gather4_c SIMD8 Surface = 2 Sampler = 2 mlen 4 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x0a4b0101
                            sampler MsgDesc: gather4_c SIMD8 Surface = 1 Sampler = 1 mlen 5 rlen 4 { align1 1Q };
send(8)         nullUD          g3UD            0x12080087
                            urb MsgDesc: 8 SIMD8 write mlen 9 rlen 0        { align1 1Q };
send(8)         nullUD          g123UD          0x8a0800a7
                            urb MsgDesc: 10 SIMD8 write mlen 5 rlen 0       { align1 1Q EOT };
send(8)         g6UD            g7UD            0x081a6001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 1 { align1 1Q };
send(8)         g7UD            g11UD           0x081a6102
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 1 { align1 1Q };
send(16)        g10UD           g12UD           0x0e2c6001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 2 { align1 1H };
send(16)        g12UD           g19UD           0x0e2c6102
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 2 Sampler = 1 mlen 7 rlen 2 { align1 1H };
send(8)         g31UD           g28UD           0x02380238
                            urb MsgDesc: 35 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g34UD           g28UD           0x02380438
                            urb MsgDesc: 67 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g37UD           g28UD           0x02380638
                            urb MsgDesc: 99 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g28UD           0x02380248
                            urb MsgDesc: 36 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g28UD           0x02380448
                            urb MsgDesc: 68 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g28UD           g28UD           0x02380648
                            urb MsgDesc: 100 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g22UD           g29UD           0x02380258
                            urb MsgDesc: 37 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g29UD           0x02380458
                            urb MsgDesc: 69 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g28UD           g29UD           0x02380658
                            urb MsgDesc: 101 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g22UD           g30UD           0x02380268
                            urb MsgDesc: 38 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g30UD           0x02380468
                            urb MsgDesc: 70 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g28UD           g30UD           0x02380668
                            urb MsgDesc: 102 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g22UD           g31UD           0x02380278
                            urb MsgDesc: 39 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g31UD           0x02380478
                            urb MsgDesc: 71 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g28UD           g31UD           0x02380678
                            urb MsgDesc: 103 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g25UD           g32UD           0x02380488
                            urb MsgDesc: 72 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g32UD           0x02380288
                            urb MsgDesc: 40 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g28UD           g32UD           0x02380688
                            urb MsgDesc: 104 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g25UD           g33UD           0x02380498
                            urb MsgDesc: 73 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g33UD           0x02380298
                            urb MsgDesc: 41 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g28UD           g33UD           0x02380698
                            urb MsgDesc: 105 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g25UD           g34UD           0x023806a8
                            urb MsgDesc: 106 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g34UD           0x023802a8
                            urb MsgDesc: 42 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g34UD           0x023804a8
                            urb MsgDesc: 74 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g8UD            g35UD           0x023802b8
                            urb MsgDesc: 43 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g35UD           0x023804b8
                            urb MsgDesc: 75 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g35UD           0x023806b8
                            urb MsgDesc: 107 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g36UD           0x023802c8
                            urb MsgDesc: 44 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g36UD           0x023804c8
                            urb MsgDesc: 76 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g36UD           0x023806c8
                            urb MsgDesc: 108 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g37UD           0x023802d8
                            urb MsgDesc: 45 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g37UD           0x023804d8
                            urb MsgDesc: 77 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g37UD           0x023806d8
                            urb MsgDesc: 109 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g38UD           0x023802e8
                            urb MsgDesc: 46 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g38UD           0x023804e8
                            urb MsgDesc: 78 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g38UD           0x023806e8
                            urb MsgDesc: 110 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g39UD           0x023802f8
                            urb MsgDesc: 47 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g39UD           0x023804f8
                            urb MsgDesc: 79 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g25UD           g39UD           0x023806f8
                            urb MsgDesc: 111 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g40UD           0x02380308
                            urb MsgDesc: 48 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g40UD           0x02380508
                            urb MsgDesc: 80 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g40UD           0x02380708
                            urb MsgDesc: 112 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g41UD           0x02380318
                            urb MsgDesc: 49 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g41UD           0x02380518
                            urb MsgDesc: 81 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g41UD           0x02380718
                            urb MsgDesc: 113 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g3UD            0x02380328
                            urb MsgDesc: 50 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g3UD            0x02380528
                            urb MsgDesc: 82 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g3UD            0x02380728
                            urb MsgDesc: 114 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g43UD           0x02380338
                            urb MsgDesc: 51 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g43UD           0x02380538
                            urb MsgDesc: 83 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g43UD           0x02380738
                            urb MsgDesc: 115 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g44UD           0x02380348
                            urb MsgDesc: 52 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g44UD           0x02380548
                            urb MsgDesc: 84 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g44UD           0x02380748
                            urb MsgDesc: 116 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g45UD           0x02380358
                            urb MsgDesc: 53 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g45UD           0x02380558
                            urb MsgDesc: 85 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g22UD           g45UD           0x02380758
                            urb MsgDesc: 117 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g46UD           0x02380368
                            urb MsgDesc: 54 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g46UD           0x02380568
                            urb MsgDesc: 86 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g46UD           0x02380768
                            urb MsgDesc: 118 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g47UD           0x02380378
                            urb MsgDesc: 55 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g47UD           0x02380578
                            urb MsgDesc: 87 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g47UD           0x02380778
                            urb MsgDesc: 119 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g48UD           0x02380388
                            urb MsgDesc: 56 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g48UD           0x02380588
                            urb MsgDesc: 88 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g48UD           0x02380788
                            urb MsgDesc: 120 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g49UD           0x02380398
                            urb MsgDesc: 57 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g49UD           0x02380598
                            urb MsgDesc: 89 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g49UD           0x02380798
                            urb MsgDesc: 121 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g50UD           0x023803a8
                            urb MsgDesc: 58 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g50UD           0x023805a8
                            urb MsgDesc: 90 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g50UD           0x023807a8
                            urb MsgDesc: 122 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g54UD           0x023803b8
                            urb MsgDesc: 59 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g54UD           0x023805b8
                            urb MsgDesc: 91 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g54UD           0x023807b8
                            urb MsgDesc: 123 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g55UD           0x023803c8
                            urb MsgDesc: 60 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g55UD           0x023805c8
                            urb MsgDesc: 92 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g55UD           0x023807c8
                            urb MsgDesc: 124 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g56UD           0x023803d8
                            urb MsgDesc: 61 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g56UD           0x023805d8
                            urb MsgDesc: 93 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g56UD           0x023807d8
                            urb MsgDesc: 125 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g57UD           0x023803e8
                            urb MsgDesc: 62 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g57UD           0x023805e8
                            urb MsgDesc: 94 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g57UD           0x023807e8
                            urb MsgDesc: 126 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g58UD           0x023803f8
                            urb MsgDesc: 63 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g58UD           0x023805f8
                            urb MsgDesc: 95 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g58UD           0x023807f8
                            urb MsgDesc: 127 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g59UD           0x02380208
                            urb MsgDesc: 32 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g59UD           0x02380408
                            urb MsgDesc: 64 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g59UD           0x02380608
                            urb MsgDesc: 96 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g17UD           g59UD           0x02380808
                            urb MsgDesc: 128 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         g8UD            g60UD           0x02380218
                            urb MsgDesc: 33 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g60UD           0x02380418
                            urb MsgDesc: 65 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g14UD           g60UD           0x02380618
                            urb MsgDesc: 97 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g17UD           g60UD           0x02380818
                            urb MsgDesc: 129 SIMD8 read mlen 1 rlen 3       { align1 1Q };
send(8)         nullUD          g14UD           0x0c0a8067
                            urb MsgDesc: 6 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x0c0a8077
                            urb MsgDesc: 7 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x0c0a8087
                            urb MsgDesc: 8 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x0c0a8097
                            urb MsgDesc: 9 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x0c0a80a7
                            urb MsgDesc: 10 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x0c0a80b7
                            urb MsgDesc: 11 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x0c0a80c7
                            urb MsgDesc: 12 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x0c0a80d7
                            urb MsgDesc: 13 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x0c0a80e7
                            urb MsgDesc: 14 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x0c0a80f7
                            urb MsgDesc: 15 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x0c0a8107
                            urb MsgDesc: 16 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x0c0a8117
                            urb MsgDesc: 17 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x0c0a8127
                            urb MsgDesc: 18 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x0c0a8137
                            urb MsgDesc: 19 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x0c0a8147
                            urb MsgDesc: 20 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x0c0a8157
                            urb MsgDesc: 21 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x0c0a8167
                            urb MsgDesc: 22 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x0c0a8177
                            urb MsgDesc: 23 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x0c0a8187
                            urb MsgDesc: 24 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x0c0a8197
                            urb MsgDesc: 25 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x0c0a81a7
                            urb MsgDesc: 26 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x0c0a81b7
                            urb MsgDesc: 27 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x0c0a81c7
                            urb MsgDesc: 28 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x0c0a81d7
                            urb MsgDesc: 29 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x0c0a81e7
                            urb MsgDesc: 30 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x0c0a81f7
                            urb MsgDesc: 31 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g40UD           0x0c0a8207
                            urb MsgDesc: 32 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g41UD           0x0c0a8217
                            urb MsgDesc: 33 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         g124UD          g2UD            0x02106e01
                            hdc1 MsgDesc: ( untyped surface read, Surface = 1, SIMD8, Mask = 0xe) mlen 1 rlen 1 { align1 1Q };
send(16)        g11UD           g19UD           0x0420a601
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 1, SIMD16, dec) mlen 2 rlen 2 { align1 1H };
send(16)        nullUD          g20UD           0x04008503
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 3, SIMD16, inc) mlen 2 rlen 0 { align1 1H };
send(8)         g17UD           g11UD           0x0813e001
                            sampler MsgDesc: ld2dms SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 1 { align1 1Q };
send(16)        g22UD           g2UD            0x1025e001
                            sampler MsgDesc: ld2dms SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 2 { align1 1H };
send(8)         nullUD          g122UD          0x8c088007
                            urb MsgDesc: 0 SIMD8 write masked mlen 6 rlen 0 { align1 1Q EOT };
send(8)         g2UD            g2UD            0x06423001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x06423102
                            sampler MsgDesc: sample_c SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 4 { align1 1Q };
send(16)        g2UD            g20UD           0x0c843001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 8 { align1 1H };
send(16)        g10UD           g26UD           0x0c843102
                            sampler MsgDesc: sample_c SIMD16 Surface = 2 Sampler = 1 mlen 6 rlen 8 { align1 1H };
send(8)         g14UD           g14UD           0x0a1a5001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 1 { align1 1Q };
send(8)         g15UD           g19UD           0x0a1a5102
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 1 { align1 1Q };
send(16)        g39UD           g7UD            0x122c5001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 2 { align1 1H };
send(16)        g41UD           g16UD           0x122c5102
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 2 Sampler = 1 mlen 9 rlen 2 { align1 1H };
send(8)         g2UD            g13UD           0x0c4b2001
                            sampler MsgDesc: gather4_po_c SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
send(16)        g43UD           g7UD            0x168d2001
                            sampler MsgDesc: gather4_po_c SIMD16 Surface = 1 Sampler = 0 mlen 11 rlen 8 { align1 1H };
send(8)         g54UD           g7UD            0x02280048
                            urb MsgDesc: 4 SIMD8 read mlen 1 rlen 2         { align1 1Q };
send(8)         g2UD            g8UD            0x02420001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(16)        g2UD            g15UD           0x04840001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 2 rlen 8 { align1 1H };
send(8)         g7UD            g44UD           0x02106e00
                            hdc1 MsgDesc: ( untyped surface read, Surface = 0, SIMD8, Mask = 0xe) mlen 1 rlen 1 { align1 1Q };
send(8)         nullUD          g44UD           0x02009500
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 0, SIMD8, inc) mlen 1 rlen 0 { align1 1Q };
send(8)         g7UD            g37UD           0x02480438
                            urb MsgDesc: 67 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g11UD           g37UD           0x02480638
                            urb MsgDesc: 99 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g13UD           g14UD           0x042a0148
                            urb MsgDesc: 20 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g14UD           0x042a0048
                            urb MsgDesc: 4 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g124UD          g13UD           0x0c43c000
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 0 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
send(8)         g22UD           g14UD           0x064a8404
                            sampler MsgDesc: gather4 SIMD8 Surface = 4 Sampler = 4 mlen 3 rlen 4 { align1 1Q };
send(8)         g14UD           g10UD           0x084a8202
                            sampler MsgDesc: gather4 SIMD8 Surface = 2 Sampler = 2 mlen 4 rlen 4 { align1 1Q };
send(8)         g18UD           g26UD           0x0a4a8303
                            sampler MsgDesc: gather4 SIMD8 Surface = 3 Sampler = 3 mlen 5 rlen 4 { align1 1Q };
send(8)         g6UD            g14UD           0x0e434102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 7 rlen 4 { align1 1Q };
send(8)         g8UD            g7UD            0x121b4001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 9 rlen 1 { align1 1Q };
send(8)         g9UD            g16UD           0x121b4102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 9 rlen 1 { align1 1Q };
send(8)         g2UD            g2UD            0x02380078
                            urb MsgDesc: 7 SIMD8 read mlen 1 rlen 3         { align1 1Q };
send(8)         g14UD           g10UD           0x064a8203
                            sampler MsgDesc: gather4 SIMD8 Surface = 3 Sampler = 2 mlen 3 rlen 4 { align1 1Q };
send(16)        g26UD           g34UD           0x0a8c8203
                            sampler MsgDesc: gather4 SIMD16 Surface = 3 Sampler = 2 mlen 5 rlen 8 { align1 1H };
send(8)         g50UD           g51UD           0x02180018
                            urb MsgDesc: 1 SIMD8 read mlen 1 rlen 1         { align1 1Q };
send(8)         g59UD           g64UD           0x02427002
                            sampler MsgDesc: ld SIMD8 Surface = 2 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g2UD            g64UD           0x02427003
                            sampler MsgDesc: ld SIMD8 Surface = 3 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g6UD            g64UD           0x02427004
                            sampler MsgDesc: ld SIMD8 Surface = 4 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g10UD           g64UD           0x02427005
                            sampler MsgDesc: ld SIMD8 Surface = 5 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g14UD           g64UD           0x02427006
                            sampler MsgDesc: ld SIMD8 Surface = 6 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g18UD           g64UD           0x02427007
                            sampler MsgDesc: ld SIMD8 Surface = 7 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g22UD           g64UD           0x02427008
                            sampler MsgDesc: ld SIMD8 Surface = 8 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g26UD           g64UD           0x02427009
                            sampler MsgDesc: ld SIMD8 Surface = 9 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g30UD           g64UD           0x0242700a
                            sampler MsgDesc: ld SIMD8 Surface = 10 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g34UD           g64UD           0x0242700b
                            sampler MsgDesc: ld SIMD8 Surface = 11 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g38UD           g64UD           0x0242700c
                            sampler MsgDesc: ld SIMD8 Surface = 12 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g42UD           g64UD           0x0242700d
                            sampler MsgDesc: ld SIMD8 Surface = 13 Sampler = 0 mlen 1 rlen 4 { align1 1Q };
send(8)         g124UD          g6UD            0x04438505
                            sampler MsgDesc: sample_lz SIMD8 Surface = 5 Sampler = 5 mlen 2 rlen 4 { align1 1Q };
send(8)         nullUD          g16UD           0x0a088067
                            urb MsgDesc: 6 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x0a088077
                            urb MsgDesc: 7 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x0a088087
                            urb MsgDesc: 8 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x0a088097
                            urb MsgDesc: 9 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x0a0880a7
                            urb MsgDesc: 10 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x0a0880b7
                            urb MsgDesc: 11 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x0a0880c7
                            urb MsgDesc: 12 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x0a0880d7
                            urb MsgDesc: 13 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x0a0880e7
                            urb MsgDesc: 14 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x0a0880f7
                            urb MsgDesc: 15 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x0a088107
                            urb MsgDesc: 16 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x0a088117
                            urb MsgDesc: 17 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x0a088127
                            urb MsgDesc: 18 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x0a088137
                            urb MsgDesc: 19 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x0a088147
                            urb MsgDesc: 20 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x0a088157
                            urb MsgDesc: 21 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x0a088167
                            urb MsgDesc: 22 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x0a088177
                            urb MsgDesc: 23 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x0a088187
                            urb MsgDesc: 24 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x0a088197
                            urb MsgDesc: 25 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x0a0881a7
                            urb MsgDesc: 26 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x0a0881b7
                            urb MsgDesc: 27 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x0a0881c7
                            urb MsgDesc: 28 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x0a0881d7
                            urb MsgDesc: 29 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g40UD           0x0a0881e7
                            urb MsgDesc: 30 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g41UD           0x0a0881f7
                            urb MsgDesc: 31 SIMD8 write masked mlen 5 rlen 0 { align1 1Q };
send(8)         nullUD          g4UD            0x0e0a8027
                            urb MsgDesc: 2 SIMD8 write per-slot masked mlen 7 rlen 0 { align1 1Q };
send(8)         g5UD            g6UD            0x04123001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 1 { align1 1Q };
send(8)         g6UD            g2UD            0x04123102
                            sampler MsgDesc: sample_c SIMD8 Surface = 2 Sampler = 1 mlen 2 rlen 1 { align1 1Q };
send(16)        g9UD            g11UD           0x08243001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 2 { align1 1H };
send(16)        g11UD           g2UD            0x08243102
                            sampler MsgDesc: sample_c SIMD16 Surface = 2 Sampler = 1 mlen 4 rlen 2 { align1 1H };
send(8)         g2UD            g2UD            0x0443d002
                            sampler MsgDesc: ld_mcs SIMD8 Surface = 2 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(8)         g3UD            g14UD           0x0a43c102
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 4 { align1 1Q };
send(16)        g2UD            g10UD           0x0885d002
                            sampler MsgDesc: ld_mcs SIMD16 Surface = 2 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(16)        g3UD            g25UD           0x1485c102
                            sampler MsgDesc: ld2dms_w SIMD16 Surface = 2 Sampler = 1 mlen 10 rlen 8 { align1 1H };
send(8)         g10UD           g11UD           0x0a123001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 1 { align1 1Q };
send(8)         g11UD           g16UD           0x0a123102
                            sampler MsgDesc: sample_c SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 1 { align1 1Q };
send(16)        g34UD           g9UD            0x14243001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 2 { align1 1H };
send(16)        g36UD           g19UD           0x14243102
                            sampler MsgDesc: sample_c SIMD16 Surface = 2 Sampler = 1 mlen 10 rlen 2 { align1 1H };
send(8)         g2UD            g7UD            0x08426001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(8)         g6UD            g11UD           0x08426102
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 4 { align1 1Q };
send(16)        g2UD            g11UD           0x10846001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 8 { align1 1H };
send(16)        g10UD           g19UD           0x10846102
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 2 Sampler = 1 mlen 8 rlen 8 { align1 1H };
(+f1.0) send(8) g4UD            g10UD           0x0210b502
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 2, SIMD8, inc) mlen 1 rlen 1 { align1 1Q };
(+f1.0) send(16) g5UD           g13UD           0x0420a502
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 2, SIMD16, inc) mlen 2 rlen 2 { align1 1H };
send(8)         g8UD            g9UD            0x06321001
                            sampler MsgDesc: sample_b SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 3 { align1 1Q };
send(16)        g2UD            g14UD           0x0c641001
                            sampler MsgDesc: sample_b SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 6 { align1 1H };
send(8)         g124UD          g6UD            0x04338000
                            sampler MsgDesc: sample_lz SIMD8 Surface = 0 Sampler = 0 mlen 2 rlen 3 { align1 1Q };
send(8)         g12UD           g1UD            0x02280058
                            urb MsgDesc: 5 SIMD8 read mlen 1 rlen 2         { align1 1Q };
send(8)         nullUD          g12UD           0x0e0a8067
                            urb MsgDesc: 6 SIMD8 write per-slot masked mlen 7 rlen 0 { align1 1Q };
send(8)         g12UD           g1UD            0x02280078
                            urb MsgDesc: 7 SIMD8 read mlen 1 rlen 2         { align1 1Q };
send(8)         nullUD          g12UD           0x0e0a8087
                            urb MsgDesc: 8 SIMD8 write per-slot masked mlen 7 rlen 0 { align1 1Q };
send(8)         g12UD           g1UD            0x02280098
                            urb MsgDesc: 9 SIMD8 read mlen 1 rlen 2         { align1 1Q };
send(8)         nullUD          g12UD           0x0e0a80a7
                            urb MsgDesc: 10 SIMD8 write per-slot masked mlen 7 rlen 0 { align1 1Q };
send(16)        g9UD            g17UD           0x04847002
                            sampler MsgDesc: ld SIMD16 Surface = 2 Sampler = 0 mlen 2 rlen 8 { align1 1H };
send(16)        g23UD           g32UD           0x04205e02
                            hdc1 MsgDesc: ( untyped surface read, Surface = 2, SIMD16, Mask = 0xe) mlen 2 rlen 2 { align1 1H };
send(8)         g8UD            g1UD            0x02280068
                            urb MsgDesc: 6 SIMD8 read mlen 1 rlen 2         { align1 1Q };
send(8)         g8UD            g1UD            0x02280088
                            urb MsgDesc: 8 SIMD8 read mlen 1 rlen 2         { align1 1Q };
send(8)         g8UD            g1UD            0x022800a8
                            urb MsgDesc: 10 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022800b8
                            urb MsgDesc: 11 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022800c8
                            urb MsgDesc: 12 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022800d8
                            urb MsgDesc: 13 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022800e8
                            urb MsgDesc: 14 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022800f8
                            urb MsgDesc: 15 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280108
                            urb MsgDesc: 16 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280118
                            urb MsgDesc: 17 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280128
                            urb MsgDesc: 18 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280138
                            urb MsgDesc: 19 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280148
                            urb MsgDesc: 20 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280158
                            urb MsgDesc: 21 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280168
                            urb MsgDesc: 22 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280178
                            urb MsgDesc: 23 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280188
                            urb MsgDesc: 24 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280198
                            urb MsgDesc: 25 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022801a8
                            urb MsgDesc: 26 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022801b8
                            urb MsgDesc: 27 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022801c8
                            urb MsgDesc: 28 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022801d8
                            urb MsgDesc: 29 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022801e8
                            urb MsgDesc: 30 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x022801f8
                            urb MsgDesc: 31 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g1UD            0x02280208
                            urb MsgDesc: 32 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g2UD            g3UD            0x04203000
                            pi MsgDesc: (persp, per_slot_offset, 0x00) mlen 2 rlen 2 { align1 1Q };
send(16)        g2UD            g11UD           0x08413000
                            pi MsgDesc: (persp, per_slot_offset, 0x00) mlen 4 rlen 4 { align1 1H };
send(8)         g2UD            g0UD            0x02201010
                            pi MsgDesc: (persp, sample_position, 0x10) mlen 1 rlen 2 { align1 1Q };
send(16)        g2UD            g0UD            0x02411010
                            pi MsgDesc: (persp, sample_position, 0x10) mlen 1 rlen 4 { align1 1H };
send(8)         g2UD            g0UD            0x02201020
                            pi MsgDesc: (persp, sample_position, 0x20) mlen 1 rlen 2 { align1 1Q };
send(16)        g2UD            g0UD            0x02411020
                            pi MsgDesc: (persp, sample_position, 0x20) mlen 1 rlen 4 { align1 1H };
send(8)         g2UD            g0UD            0x02201030
                            pi MsgDesc: (persp, sample_position, 0x30) mlen 1 rlen 2 { align1 1Q };
send(16)        g2UD            g0UD            0x02411030
                            pi MsgDesc: (persp, sample_position, 0x30) mlen 1 rlen 4 { align1 1H };
send(8)         g20UD           g15UD           0x04320203
                            sampler MsgDesc: sample SIMD8 Surface = 3 Sampler = 2 mlen 2 rlen 3 { align1 1Q };
send(8)         g11UD           g26UD           0x04320405
                            sampler MsgDesc: sample SIMD8 Surface = 5 Sampler = 4 mlen 2 rlen 3 { align1 1Q };
send(8)         g8UD            g24UD           0x04320304
                            sampler MsgDesc: sample SIMD8 Surface = 4 Sampler = 3 mlen 2 rlen 3 { align1 1Q };
send(16)        g26UD           g21UD           0x08640203
                            sampler MsgDesc: sample SIMD16 Surface = 3 Sampler = 2 mlen 4 rlen 6 { align1 1H };
send(16)        g12UD           g48UD           0x08640405
                            sampler MsgDesc: sample SIMD16 Surface = 5 Sampler = 4 mlen 4 rlen 6 { align1 1H };
send(16)        g38UD           g44UD           0x08640304
                            sampler MsgDesc: sample SIMD16 Surface = 4 Sampler = 3 mlen 4 rlen 6 { align1 1H };
(+f1.0) send(8) nullUD          g94UD           0x02009601
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 1, SIMD8, dec) mlen 1 rlen 0 { align1 1Q };
(+f1.0) send(8) g47UD           g94UD           0x0210b601
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 1, SIMD8, dec) mlen 1 rlen 1 { align1 1Q };
send(16)        g4UD            g1UD            0x04405c02
                            hdc1 MsgDesc: ( untyped surface read, Surface = 2, SIMD16, Mask = 0xc) mlen 2 rlen 4 { align1 1H };
send(8)         nullUD          g100UD          0x02009600
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 0, SIMD8, dec) mlen 1 rlen 0 { align1 1Q };
send(8)         g51UD           g100UD          0x0210b600
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 0, SIMD8, dec) mlen 1 rlen 1 { align1 1Q };
send(8)         g5UD            g11UD           0x064a0001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(16)        g7UD            g19UD           0x0a8c0001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 8 { align1 1H };
send(8)         nullUD          g123UD          0x8a080117
                            urb MsgDesc: 17 SIMD8 write mlen 5 rlen 0       { align1 1Q EOT };
send(8)         g3UD            g3UD            0x02415002
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 2, SIMD16, Mask = 0x0) mlen 1 rlen 4 { align1 1Q };
send(8)         g5UD            g4UD            0x02416002
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 2, SIMD8, Mask = 0x0) mlen 1 rlen 4 { align1 2Q };
send(8)         g6UD            g16UD           0x0210b500
                            hdc1 MsgDesc: ( DC untyped atomic op, Surface = 0, SIMD8, inc) mlen 1 rlen 1 { align1 1Q };
send(8)         nullUD          g119UD          0x92080097
                            urb MsgDesc: 9 SIMD8 write mlen 9 rlen 0        { align1 1Q EOT };
send(8)         nullUD          g4UD            0x120800c7
                            urb MsgDesc: 12 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g5UD            0x120800e7
                            urb MsgDesc: 14 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g123UD          0x8a080107
                            urb MsgDesc: 16 SIMD8 write mlen 5 rlen 0       { align1 1Q EOT };
send(8)         g6UD            g11UD           0x08434102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 4 { align1 1Q };
send(8)         g67UD           g36UD           0x0823e000
                            sampler MsgDesc: ld2dms SIMD8 Surface = 0 Sampler = 0 mlen 4 rlen 2 { align1 1Q };
send(8)         g2UD            g2UD            0x0a23c000
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 0 Sampler = 0 mlen 5 rlen 2 { align1 1Q };
send(8)         g9UD            g15UD           0x021ab101
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 1 Sampler = 1 mlen 1 rlen 1 { align1 1Q };
send(8)         g10UD           g16UD           0x021ab202
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 2 Sampler = 2 mlen 1 rlen 1 { align1 1Q };
send(8)         g11UD           g17UD           0x021ab303
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 3 Sampler = 3 mlen 1 rlen 1 { align1 1Q };
send(8)         g12UD           g18UD           0x021ab404
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 4 Sampler = 4 mlen 1 rlen 1 { align1 1Q };
send(8)         g13UD           g19UD           0x021ab505
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 5 Sampler = 5 mlen 1 rlen 1 { align1 1Q };
send(8)         g14UD           g18UD           0x08123102
                            sampler MsgDesc: sample_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 1 { align1 1Q };
send(16)        g24UD           g32UD           0x10243102
                            sampler MsgDesc: sample_c SIMD16 Surface = 2 Sampler = 1 mlen 8 rlen 2 { align1 1H };
send(8)         g5UD            g5UD            0x04415000
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 0, SIMD16, Mask = 0x0) mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g9UD            0x043a0028
                            urb MsgDesc: 2 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g13UD           g1UD            0x02380098
                            urb MsgDesc: 9 SIMD8 read mlen 1 rlen 3         { align1 1Q };
send(8)         g13UD           g1UD            0x023800a8
                            urb MsgDesc: 10 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g13UD           g1UD            0x023800b8
                            urb MsgDesc: 11 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g13UD           g1UD            0x023800d8
                            urb MsgDesc: 13 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g13UD           g1UD            0x023800e8
                            urb MsgDesc: 14 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g13UD           g1UD            0x023800f8
                            urb MsgDesc: 15 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g13UD           g1UD            0x02380108
                            urb MsgDesc: 16 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g13UD           g1UD            0x02380118
                            urb MsgDesc: 17 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         nullUD          g60UD           0x120800a7
                            urb MsgDesc: 10 SIMD8 write mlen 9 rlen 0       { align1 1Q };
send(8)         nullUD          g119UD          0x92080107
                            urb MsgDesc: 16 SIMD8 write mlen 9 rlen 0       { align1 1Q EOT };
send(8)         g3UD            g7UD            0x02115e01
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD16, Mask = 0xe) mlen 1 rlen 1 { align1 1Q };
send(8)         g5UD            g11UD           0x02116e01
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD8, Mask = 0xe) mlen 1 rlen 1 { align1 2Q };
send(8)         nullUD          g123UD          0x8a080067
                            urb MsgDesc: 6 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         nullUD          g80UD           0x140a00b7
                            urb MsgDesc: 11 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x140a00d7
                            urb MsgDesc: 13 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x140a00f7
                            urb MsgDesc: 15 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x140a0117
                            urb MsgDesc: 17 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x140a0137
                            urb MsgDesc: 19 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g90UD           0x140a0157
                            urb MsgDesc: 21 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g100UD          0x140a0177
                            urb MsgDesc: 23 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g110UD          0x0c0a0197
                            urb MsgDesc: 25 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g120UD          0x8c0a0197
                            urb MsgDesc: 25 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         nullUD          g123UD          0x8a0800b7
                            urb MsgDesc: 11 SIMD8 write mlen 5 rlen 0       { align1 1Q EOT };
send(8)         g22UD           g53UD           0x02180238
                            urb MsgDesc: 35 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g54UD           g53UD           0x02180438
                            urb MsgDesc: 67 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g67UD           g53UD           0x02180638
                            urb MsgDesc: 99 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g61UD           g53UD           0x02180248
                            urb MsgDesc: 36 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g66UD           g53UD           0x02180448
                            urb MsgDesc: 68 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g63UD           g53UD           0x02180648
                            urb MsgDesc: 100 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g68UD           g65UD           0x02180258
                            urb MsgDesc: 37 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g69UD           g65UD           0x02180458
                            urb MsgDesc: 69 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g70UD           g65UD           0x02180658
                            urb MsgDesc: 101 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g75UD           g24UD           0x02180268
                            urb MsgDesc: 38 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g76UD           g24UD           0x02180468
                            urb MsgDesc: 70 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g77UD           g24UD           0x02180668
                            urb MsgDesc: 102 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g82UD           g25UD           0x02180278
                            urb MsgDesc: 39 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g83UD           g25UD           0x02180478
                            urb MsgDesc: 71 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g84UD           g25UD           0x02180678
                            urb MsgDesc: 103 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g89UD           g26UD           0x02180288
                            urb MsgDesc: 40 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g90UD           g26UD           0x02180488
                            urb MsgDesc: 72 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g91UD           g26UD           0x02180688
                            urb MsgDesc: 104 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g96UD           g27UD           0x02180298
                            urb MsgDesc: 41 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g97UD           g27UD           0x02180498
                            urb MsgDesc: 73 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g98UD           g27UD           0x02180698
                            urb MsgDesc: 105 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g103UD          g28UD           0x021802a8
                            urb MsgDesc: 42 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g104UD          g28UD           0x021804a8
                            urb MsgDesc: 74 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g105UD          g28UD           0x021806a8
                            urb MsgDesc: 106 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g110UD          g29UD           0x021802b8
                            urb MsgDesc: 43 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g111UD          g29UD           0x021804b8
                            urb MsgDesc: 75 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g112UD          g29UD           0x021806b8
                            urb MsgDesc: 107 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g117UD          g30UD           0x021802c8
                            urb MsgDesc: 44 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g118UD          g30UD           0x021804c8
                            urb MsgDesc: 76 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g119UD          g30UD           0x021806c8
                            urb MsgDesc: 108 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g124UD          g31UD           0x021802d8
                            urb MsgDesc: 45 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g125UD          g31UD           0x021804d8
                            urb MsgDesc: 77 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g126UD          g31UD           0x021806d8
                            urb MsgDesc: 109 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g10UD           g32UD           0x021802e8
                            urb MsgDesc: 46 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g11UD           g32UD           0x021804e8
                            urb MsgDesc: 78 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g12UD           g32UD           0x021806e8
                            urb MsgDesc: 110 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g26UD           g33UD           0x021802f8
                            urb MsgDesc: 47 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g27UD           g33UD           0x021804f8
                            urb MsgDesc: 79 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g28UD           g33UD           0x021806f8
                            urb MsgDesc: 111 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g33UD           g35UD           0x02180308
                            urb MsgDesc: 48 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g34UD           g35UD           0x02180508
                            urb MsgDesc: 80 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g35UD           g35UD           0x02180708
                            urb MsgDesc: 112 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g64UD           g36UD           0x02180318
                            urb MsgDesc: 49 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g41UD           g36UD           0x02180518
                            urb MsgDesc: 81 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g42UD           g36UD           0x02180718
                            urb MsgDesc: 113 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g6UD            g37UD           0x02180328
                            urb MsgDesc: 50 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g48UD           g37UD           0x02180528
                            urb MsgDesc: 82 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g49UD           g37UD           0x02180728
                            urb MsgDesc: 114 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g67UD           g38UD           0x02180338
                            urb MsgDesc: 51 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g56UD           g38UD           0x02180538
                            urb MsgDesc: 83 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g57UD           g38UD           0x02180738
                            urb MsgDesc: 115 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g66UD           g39UD           0x02180348
                            urb MsgDesc: 52 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g63UD           g39UD           0x02180548
                            urb MsgDesc: 84 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g40UD           g39UD           0x02180748
                            urb MsgDesc: 116 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g69UD           g64UD           0x02180358
                            urb MsgDesc: 53 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g70UD           g64UD           0x02180558
                            urb MsgDesc: 85 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g71UD           g64UD           0x02180758
                            urb MsgDesc: 117 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g76UD           g41UD           0x02180368
                            urb MsgDesc: 54 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g77UD           g41UD           0x02180568
                            urb MsgDesc: 86 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g78UD           g41UD           0x02180768
                            urb MsgDesc: 118 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g83UD           g42UD           0x02180378
                            urb MsgDesc: 55 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g84UD           g42UD           0x02180578
                            urb MsgDesc: 87 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g85UD           g42UD           0x02180778
                            urb MsgDesc: 119 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g90UD           g43UD           0x02180388
                            urb MsgDesc: 56 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g91UD           g43UD           0x02180588
                            urb MsgDesc: 88 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g92UD           g43UD           0x02180788
                            urb MsgDesc: 120 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g97UD           g44UD           0x02180398
                            urb MsgDesc: 57 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g98UD           g44UD           0x02180598
                            urb MsgDesc: 89 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g99UD           g44UD           0x02180798
                            urb MsgDesc: 121 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g104UD          g45UD           0x021803a8
                            urb MsgDesc: 58 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g105UD          g45UD           0x021805a8
                            urb MsgDesc: 90 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g106UD          g45UD           0x021807a8
                            urb MsgDesc: 122 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g111UD          g46UD           0x021803b8
                            urb MsgDesc: 59 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g112UD          g46UD           0x021805b8
                            urb MsgDesc: 91 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g113UD          g46UD           0x021807b8
                            urb MsgDesc: 123 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g118UD          g6UD            0x021803c8
                            urb MsgDesc: 60 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g119UD          g6UD            0x021805c8
                            urb MsgDesc: 92 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g120UD          g6UD            0x021807c8
                            urb MsgDesc: 124 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g125UD          g48UD           0x021803d8
                            urb MsgDesc: 61 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g126UD          g48UD           0x021805d8
                            urb MsgDesc: 93 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g2UD            g48UD           0x021807d8
                            urb MsgDesc: 125 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g12UD           g49UD           0x021803e8
                            urb MsgDesc: 62 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g13UD           g49UD           0x021805e8
                            urb MsgDesc: 94 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g14UD           g49UD           0x021807e8
                            urb MsgDesc: 126 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g19UD           g50UD           0x021803f8
                            urb MsgDesc: 63 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g20UD           g50UD           0x021805f8
                            urb MsgDesc: 95 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g53UD           g50UD           0x021807f8
                            urb MsgDesc: 127 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g28UD           g51UD           0x02180408
                            urb MsgDesc: 64 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g29UD           g51UD           0x02180608
                            urb MsgDesc: 96 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g30UD           g51UD           0x02180808
                            urb MsgDesc: 128 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         g35UD           g22UD           0x02180218
                            urb MsgDesc: 33 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g36UD           g22UD           0x02180418
                            urb MsgDesc: 65 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g37UD           g22UD           0x02180618
                            urb MsgDesc: 97 SIMD8 read mlen 1 rlen 1        { align1 1Q };
send(8)         g38UD           g22UD           0x02180818
                            urb MsgDesc: 129 SIMD8 read mlen 1 rlen 1       { align1 1Q };
send(8)         nullUD          g6UD            0x080a8037
                            urb MsgDesc: 3 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g10UD           0x080a8047
                            urb MsgDesc: 4 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g11UD           0x080a8057
                            urb MsgDesc: 5 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g12UD           0x080a8067
                            urb MsgDesc: 6 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x080a8077
                            urb MsgDesc: 7 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g14UD           0x080a8087
                            urb MsgDesc: 8 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x080a8097
                            urb MsgDesc: 9 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x080a80a7
                            urb MsgDesc: 10 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x080a80b7
                            urb MsgDesc: 11 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x080a80c7
                            urb MsgDesc: 12 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x080a80d7
                            urb MsgDesc: 13 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x080a80e7
                            urb MsgDesc: 14 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x080a80f7
                            urb MsgDesc: 15 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x080a8107
                            urb MsgDesc: 16 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x080a8117
                            urb MsgDesc: 17 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x080a8127
                            urb MsgDesc: 18 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x080a8137
                            urb MsgDesc: 19 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x080a8147
                            urb MsgDesc: 20 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x080a8157
                            urb MsgDesc: 21 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x080a8167
                            urb MsgDesc: 22 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x080a8177
                            urb MsgDesc: 23 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x080a8187
                            urb MsgDesc: 24 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x080a8197
                            urb MsgDesc: 25 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x080a81a7
                            urb MsgDesc: 26 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x080a81b7
                            urb MsgDesc: 27 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x080a81c7
                            urb MsgDesc: 28 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x080a81d7
                            urb MsgDesc: 29 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x080a81e7
                            urb MsgDesc: 30 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x080a81f7
                            urb MsgDesc: 31 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x080a8207
                            urb MsgDesc: 32 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x080a8217
                            urb MsgDesc: 33 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         g18UD           g19UD           0x04115e00
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 0, SIMD16, Mask = 0xe) mlen 2 rlen 1 { align1 1Q };
send(8)         g2UD            g6UD            0x0623d001
                            sampler MsgDesc: ld_mcs SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 2 { align1 1Q };
send(16)        g2UD            g8UD            0x0c45d001
                            sampler MsgDesc: ld_mcs SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1H };
send(8)         g101UD          g10UD           0x0c33c001
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 3 { align1 1Q };
send(8)         g14UD           g11UD           0x084b0203
                            sampler MsgDesc: gather4_c SIMD8 Surface = 3 Sampler = 2 mlen 4 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x0a4b0102
                            sampler MsgDesc: gather4_c SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 4 { align1 1Q };
send(16)        g26UD           g2UD            0x0e8d0203
                            sampler MsgDesc: gather4_c SIMD16 Surface = 3 Sampler = 2 mlen 7 rlen 8 { align1 1H };
send(16)        g10UD           g34UD           0x128d0102
                            sampler MsgDesc: gather4_c SIMD16 Surface = 2 Sampler = 1 mlen 9 rlen 8 { align1 1H };
send(8)         g6UD            g7UD            0x0a1b4001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 1 { align1 1Q };
send(8)         g7UD            g12UD           0x0a1b4102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 1 { align1 1Q };
send(8)         g34UD           g42UD           0x02480248
                            urb MsgDesc: 36 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g38UD           g42UD           0x02480448
                            urb MsgDesc: 68 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g42UD           g42UD           0x02480648
                            urb MsgDesc: 100 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g22UD           g43UD           0x02480258
                            urb MsgDesc: 37 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g43UD           0x02480458
                            urb MsgDesc: 69 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g30UD           g43UD           0x02480658
                            urb MsgDesc: 101 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g22UD           g44UD           0x02480268
                            urb MsgDesc: 38 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g44UD           0x02480468
                            urb MsgDesc: 70 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g30UD           g44UD           0x02480668
                            urb MsgDesc: 102 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g22UD           g45UD           0x02480278
                            urb MsgDesc: 39 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g45UD           0x02480478
                            urb MsgDesc: 71 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g30UD           g45UD           0x02480678
                            urb MsgDesc: 103 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g22UD           g55UD           0x02480288
                            urb MsgDesc: 40 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g55UD           0x02480488
                            urb MsgDesc: 72 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g30UD           g55UD           0x02480688
                            urb MsgDesc: 104 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g26UD           g56UD           0x02480498
                            urb MsgDesc: 73 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g56UD           0x02480298
                            urb MsgDesc: 41 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g30UD           g56UD           0x02480698
                            urb MsgDesc: 105 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g26UD           g82UD           0x024804a8
                            urb MsgDesc: 74 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g82UD           0x024802a8
                            urb MsgDesc: 42 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g30UD           g82UD           0x024806a8
                            urb MsgDesc: 106 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g26UD           g83UD           0x024804b8
                            urb MsgDesc: 75 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g83UD           0x024802b8
                            urb MsgDesc: 43 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g30UD           g83UD           0x024806b8
                            urb MsgDesc: 107 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g26UD           g84UD           0x024806c8
                            urb MsgDesc: 108 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g84UD           0x024802c8
                            urb MsgDesc: 44 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g84UD           0x024804c8
                            urb MsgDesc: 76 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g8UD            g85UD           0x024802d8
                            urb MsgDesc: 45 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g85UD           0x024804d8
                            urb MsgDesc: 77 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g85UD           0x024806d8
                            urb MsgDesc: 109 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g6UD            0x024802e8
                            urb MsgDesc: 46 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g6UD            0x024804e8
                            urb MsgDesc: 78 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g6UD            0x024806e8
                            urb MsgDesc: 110 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g3UD            0x024802f8
                            urb MsgDesc: 47 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g3UD            0x024804f8
                            urb MsgDesc: 79 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g3UD            0x024806f8
                            urb MsgDesc: 111 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g46UD           0x02480308
                            urb MsgDesc: 48 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g46UD           0x02480508
                            urb MsgDesc: 80 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g46UD           0x02480708
                            urb MsgDesc: 112 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g47UD           0x02480318
                            urb MsgDesc: 49 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g47UD           0x02480518
                            urb MsgDesc: 81 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g47UD           0x02480718
                            urb MsgDesc: 113 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g57UD           0x02480328
                            urb MsgDesc: 50 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g57UD           0x02480528
                            urb MsgDesc: 82 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g57UD           0x02480728
                            urb MsgDesc: 114 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g58UD           0x02480338
                            urb MsgDesc: 51 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g58UD           0x02480538
                            urb MsgDesc: 83 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g26UD           g58UD           0x02480738
                            urb MsgDesc: 115 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g59UD           0x02480348
                            urb MsgDesc: 52 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g59UD           0x02480548
                            urb MsgDesc: 84 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g59UD           0x02480748
                            urb MsgDesc: 116 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g60UD           0x02480358
                            urb MsgDesc: 53 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g60UD           0x02480558
                            urb MsgDesc: 85 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g60UD           0x02480758
                            urb MsgDesc: 117 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g61UD           0x02480368
                            urb MsgDesc: 54 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g61UD           0x02480568
                            urb MsgDesc: 86 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g61UD           0x02480768
                            urb MsgDesc: 118 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g62UD           0x02480378
                            urb MsgDesc: 55 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g62UD           0x02480578
                            urb MsgDesc: 87 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g62UD           0x02480778
                            urb MsgDesc: 119 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g63UD           0x02480388
                            urb MsgDesc: 56 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g63UD           0x02480588
                            urb MsgDesc: 88 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g63UD           0x02480788
                            urb MsgDesc: 120 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g64UD           0x02480398
                            urb MsgDesc: 57 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g64UD           0x02480598
                            urb MsgDesc: 89 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g64UD           0x02480798
                            urb MsgDesc: 121 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g68UD           0x024803a8
                            urb MsgDesc: 58 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g68UD           0x024805a8
                            urb MsgDesc: 90 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g68UD           0x024807a8
                            urb MsgDesc: 122 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g69UD           0x024803b8
                            urb MsgDesc: 59 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g69UD           0x024805b8
                            urb MsgDesc: 91 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g22UD           g69UD           0x024807b8
                            urb MsgDesc: 123 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g70UD           0x024803c8
                            urb MsgDesc: 60 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g70UD           0x024805c8
                            urb MsgDesc: 92 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g16UD           g70UD           0x024807c8
                            urb MsgDesc: 124 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g71UD           0x024803d8
                            urb MsgDesc: 61 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g71UD           0x024805d8
                            urb MsgDesc: 93 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g16UD           g71UD           0x024807d8
                            urb MsgDesc: 125 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g72UD           0x024803e8
                            urb MsgDesc: 62 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g72UD           0x024805e8
                            urb MsgDesc: 94 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g16UD           g72UD           0x024807e8
                            urb MsgDesc: 126 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g8UD            g73UD           0x024803f8
                            urb MsgDesc: 63 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g12UD           g73UD           0x024805f8
                            urb MsgDesc: 95 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g16UD           g73UD           0x024807f8
                            urb MsgDesc: 127 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         g12UD           g75UD           0x02480418
                            urb MsgDesc: 65 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g16UD           g75UD           0x02480618
                            urb MsgDesc: 97 SIMD8 read mlen 1 rlen 4        { align1 1Q };
send(8)         g20UD           g75UD           0x02480818
                            urb MsgDesc: 129 SIMD8 read mlen 1 rlen 4       { align1 1Q };
send(8)         nullUD          g20UD           0x0c0a00c7
                            urb MsgDesc: 12 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x0c0a00d7
                            urb MsgDesc: 13 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x0c0a00e7
                            urb MsgDesc: 14 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x0c0a00f7
                            urb MsgDesc: 15 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x0c0a0107
                            urb MsgDesc: 16 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x0c0a0117
                            urb MsgDesc: 17 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x0c0a0137
                            urb MsgDesc: 19 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x0c0a0147
                            urb MsgDesc: 20 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x0c0a0157
                            urb MsgDesc: 21 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x0c0a0167
                            urb MsgDesc: 22 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x0c0a0177
                            urb MsgDesc: 23 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x0c0a0187
                            urb MsgDesc: 24 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x0c0a01a7
                            urb MsgDesc: 26 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x0c0a01b7
                            urb MsgDesc: 27 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x0c0a01c7
                            urb MsgDesc: 28 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x0c0a01d7
                            urb MsgDesc: 29 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x0c0a01e7
                            urb MsgDesc: 30 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x0c0a01f7
                            urb MsgDesc: 31 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q };
send(16)        g46UD           g12UD           0x02280302
                            hdc:ro MsgDesc: (2, 3, 0, 0) mlen 1 rlen 2       { align1 WE_all 1H };
send(16)        g50UD           g15UD           0x02280304
                            hdc:ro MsgDesc: (4, 3, 0, 0) mlen 1 rlen 2       { align1 WE_all 1H };
send(16)        g34UD           g20UD           0x02280303
                            hdc:ro MsgDesc: (3, 3, 0, 0) mlen 1 rlen 2       { align1 WE_all 1H };
send(16)        g16UD           g21UD           0x02280306
                            hdc:ro MsgDesc: (6, 3, 0, 0) mlen 1 rlen 2       { align1 WE_all 1H };
send(8)         g5UD            g19UD           0x02106e03
                            hdc1 MsgDesc: ( untyped surface read, Surface = 3, SIMD8, Mask = 0xe) mlen 1 rlen 1 { align1 1Q };
send(8)         g8UD            g21UD           0x02106e04
                            hdc1 MsgDesc: ( untyped surface read, Surface = 4, SIMD8, Mask = 0xe) mlen 1 rlen 1 { align1 1Q };
send(16)        g8UD            g34UD           0x04205e03
                            hdc1 MsgDesc: ( untyped surface read, Surface = 3, SIMD16, Mask = 0xe) mlen 2 rlen 2 { align1 1H };
send(16)        g14UD           g37UD           0x04205e04
                            hdc1 MsgDesc: ( untyped surface read, Surface = 4, SIMD16, Mask = 0xe) mlen 2 rlen 2 { align1 1H };
send(8)         g15UD           g12UD           0x041a0038
                            urb MsgDesc: 3 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g2UD            g54UD           0x0242a707
                            sampler MsgDesc: resinfo SIMD8 Surface = 7 Sampler = 7 mlen 1 rlen 4 { align1 1Q };
send(8)         g6UD            g55UD           0x0242a808
                            sampler MsgDesc: resinfo SIMD8 Surface = 8 Sampler = 8 mlen 1 rlen 4 { align1 1Q };
send(8)         g10UD           g56UD           0x0242a909
                            sampler MsgDesc: resinfo SIMD8 Surface = 9 Sampler = 9 mlen 1 rlen 4 { align1 1Q };
send(8)         g14UD           g57UD           0x0242aa0a
                            sampler MsgDesc: resinfo SIMD8 Surface = 10 Sampler = 10 mlen 1 rlen 4 { align1 1Q };
send(8)         g18UD           g58UD           0x0242ab0b
                            sampler MsgDesc: resinfo SIMD8 Surface = 11 Sampler = 11 mlen 1 rlen 4 { align1 1Q };
send(8)         g22UD           g59UD           0x0242ac0c
                            sampler MsgDesc: resinfo SIMD8 Surface = 12 Sampler = 12 mlen 1 rlen 4 { align1 1Q };
send(8)         nullUD          g9UD            0x0c088027
                            urb MsgDesc: 2 SIMD8 write masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g10UD           0x0c088047
                            urb MsgDesc: 4 SIMD8 write masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g11UD           0x0c088067
                            urb MsgDesc: 6 SIMD8 write masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x0c088037
                            urb MsgDesc: 3 SIMD8 write masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g7UD            0x0c088057
                            urb MsgDesc: 5 SIMD8 write masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g8UD            0x0c088077
                            urb MsgDesc: 7 SIMD8 write masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x140a0197
                            urb MsgDesc: 25 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x140a01b7
                            urb MsgDesc: 27 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x140a01d7
                            urb MsgDesc: 29 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g6UD            0x140a01f7
                            urb MsgDesc: 31 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g120UD          0x8c0a0217
                            urb MsgDesc: 33 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         g8UD            g6UD            0x041a0318
                            urb MsgDesc: 49 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g9UD            g6UD            0x041a0518
                            urb MsgDesc: 81 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g10UD           g6UD            0x041a0718
                            urb MsgDesc: 113 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g11UD           g6UD            0x041a0918
                            urb MsgDesc: 145 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g7UD            g11UD           0x041a0218
                            urb MsgDesc: 33 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g8UD            g11UD           0x041a0418
                            urb MsgDesc: 65 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g9UD            g11UD           0x041a0618
                            urb MsgDesc: 97 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g10UD           g11UD           0x041a0818
                            urb MsgDesc: 129 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         nullUD          g10UD           0x080a8227
                            urb MsgDesc: 34 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g11UD           0x080a8237
                            urb MsgDesc: 35 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g12UD           0x080a8247
                            urb MsgDesc: 36 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x080a8257
                            urb MsgDesc: 37 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g14UD           0x080a8267
                            urb MsgDesc: 38 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x080a8277
                            urb MsgDesc: 39 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x080a8287
                            urb MsgDesc: 40 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x080a8297
                            urb MsgDesc: 41 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x080a82a7
                            urb MsgDesc: 42 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x080a82b7
                            urb MsgDesc: 43 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x080a82c7
                            urb MsgDesc: 44 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x080a82d7
                            urb MsgDesc: 45 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x080a82e7
                            urb MsgDesc: 46 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x080a82f7
                            urb MsgDesc: 47 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x080a8307
                            urb MsgDesc: 48 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x080a8317
                            urb MsgDesc: 49 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x080a8327
                            urb MsgDesc: 50 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x080a8337
                            urb MsgDesc: 51 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x080a8347
                            urb MsgDesc: 52 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x080a8357
                            urb MsgDesc: 53 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x080a8367
                            urb MsgDesc: 54 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x080a8377
                            urb MsgDesc: 55 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x080a8387
                            urb MsgDesc: 56 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x080a8397
                            urb MsgDesc: 57 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x080a83a7
                            urb MsgDesc: 58 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x080a83b7
                            urb MsgDesc: 59 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x080a83c7
                            urb MsgDesc: 60 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x080a83d7
                            urb MsgDesc: 61 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x080a83e7
                            urb MsgDesc: 62 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x080a83f7
                            urb MsgDesc: 63 SIMD8 write per-slot masked mlen 4 rlen 0 { align1 1Q };
send(8)         g8UD            g9UD            0x02480008
                            urb MsgDesc: 0 SIMD8 read mlen 1 rlen 4         { align1 1Q };
send(8)         nullUD          g123UD          0x8a080007
                            urb MsgDesc: 0 SIMD8 write mlen 5 rlen 0        { align1 1Q EOT };
send(8)         g4UD            g2UD            0x04215c01
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD16, Mask = 0xc) mlen 2 rlen 2 { align1 1Q };
send(8)         g40UD           g38UD           0x04216c01
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD8, Mask = 0xc) mlen 2 rlen 2 { align1 2Q };
send(8)         g6UD            g11UD           0x104a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 8 rlen 4 { align1 1Q };
send(8)         g124UD          g2UD            0x04422001
                            sampler MsgDesc: sample_l SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(16)        g120UD          g2UD            0x08842001
                            sampler MsgDesc: sample_l SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(8)         g2UD            g7UD            0x06425001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         g6UD            g10UD           0x06425102
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 4 { align1 1Q };
send(16)        g2UD            g11UD           0x0c845001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 8 { align1 1H };
send(16)        g10UD           g18UD           0x0c845102
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 2 Sampler = 1 mlen 6 rlen 8 { align1 1H };
send(8)         nullUD          g121UD          0x8a080197
                            urb MsgDesc: 25 SIMD8 write mlen 5 rlen 0       { align1 1Q EOT };
send(8)         g124UD          g6UD            0x02415000
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 0, SIMD16, Mask = 0x0) mlen 1 rlen 4 { align1 1Q };
send(8)         g124UD          g6UD            0x06415000
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 0, SIMD16, Mask = 0x0) mlen 3 rlen 4 { align1 1Q };
send(8)         g124UD          g6UD            0x02215c00
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 0, SIMD16, Mask = 0xc) mlen 1 rlen 2 { align1 1Q };
send(8)         g17UD           g27UD           0x02115e00
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 0, SIMD16, Mask = 0xe) mlen 1 rlen 1 { align1 1Q };
send(8)         g124UD          g2UD            0x02415001
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD16, Mask = 0x0) mlen 1 rlen 4 { align1 1Q };
send(8)         g2UD            g29UD           0x02416001
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 1, SIMD8, Mask = 0x0) mlen 1 rlen 4 { align1 2Q };
send(8)         g9UD            g19UD           0x0843e102
                            sampler MsgDesc: ld2dms SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 4 { align1 1Q };
send(16)        g23UD           g7UD            0x1085e102
                            sampler MsgDesc: ld2dms SIMD16 Surface = 2 Sampler = 1 mlen 8 rlen 8 { align1 1H };
send(8)         g124UD          g5UD            0x0c4b0001
                            sampler MsgDesc: gather4_c SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
send(16)        g120UD          g7UD            0x168d0001
                            sampler MsgDesc: gather4_c SIMD16 Surface = 1 Sampler = 0 mlen 11 rlen 8 { align1 1H };
send(8)         g6UD            g7UD            0x0a134001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 1 { align1 1Q };
send(8)         g7UD            g12UD           0x0a134102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 1 { align1 1Q };
send(8)         g22UD           g10UD           0x041a0138
                            urb MsgDesc: 19 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g21UD           g10UD           0x041a0338
                            urb MsgDesc: 51 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g65UD           g10UD           0x041a0538
                            urb MsgDesc: 83 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g10UD           g10UD           0x041a0738
                            urb MsgDesc: 115 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g65UD           g11UD           0x041a0238
                            urb MsgDesc: 35 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g10UD           g11UD           0x041a0438
                            urb MsgDesc: 67 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g11UD           g11UD           0x041a0638
                            urb MsgDesc: 99 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g8UD            g7UD            0x041a0048
                            urb MsgDesc: 4 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g6UD            g10UD           0x0a4a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 4 { align1 1Q };
send(8)         g2UD            g7UD            0x06426001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 4 { align1 1Q };
send(8)         g6UD            g10UD           0x06426102
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 4 { align1 1Q };
send(16)        g2UD            g11UD           0x0c846001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 8 { align1 1H };
send(16)        g10UD           g18UD           0x0c846102
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 2 Sampler = 1 mlen 6 rlen 8 { align1 1H };
send(8)         g124UD          g2UD            0x08320001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 3 { align1 1Q };
send(16)        g120UD          g2UD            0x10640001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 6 { align1 1H };
send(8)         g6UD            g7UD            0x0c1b4001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 1 { align1 1Q };
send(8)         g7UD            g13UD           0x0c1b4102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 6 rlen 1 { align1 1Q };
send(8)         g2UD            g7UD            0x08425001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(8)         g6UD            g11UD           0x08425102
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 4 { align1 1Q };
send(16)        g2UD            g11UD           0x10845001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 8 { align1 1H };
send(16)        g10UD           g19UD           0x10845102
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 2 Sampler = 1 mlen 8 rlen 8 { align1 1H };
send(8)         g124UD          g2UD            0x02306801
                            hdc1 MsgDesc: ( untyped surface read, Surface = 1, SIMD8, Mask = 0x8) mlen 1 rlen 3 { align1 1Q };
send(16)        g120UD          g2UD            0x04605801
                            hdc1 MsgDesc: ( untyped surface read, Surface = 1, SIMD16, Mask = 0x8) mlen 2 rlen 6 { align1 1H };
send(8)         g8UD            g7UD            0x043a0128
                            urb MsgDesc: 18 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g12UD           g5UD            0x0833e001
                            sampler MsgDesc: ld2dms SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 3 { align1 1Q };
send(8)         g15UD           g17UD           0x0823e001
                            sampler MsgDesc: ld2dms SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 2 { align1 1Q };
send(16)        g7UD            g13UD           0x1065e001
                            sampler MsgDesc: ld2dms SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 6 { align1 1H };
send(16)        g33UD           g21UD           0x1045e001
                            sampler MsgDesc: ld2dms SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 4 { align1 1H };
send(8)         g14UD           g14UD           0x101b4001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 8 rlen 1 { align1 1Q };
send(8)         g15UD           g22UD           0x101b4102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 8 rlen 1 { align1 1Q };
send(8)         g8UD            g20UD           0x044a0138
                            urb MsgDesc: 19 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g20UD           0x044a0338
                            urb MsgDesc: 51 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g16UD           g20UD           0x044a0538
                            urb MsgDesc: 83 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g20UD           g20UD           0x044a0738
                            urb MsgDesc: 115 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g14UD           g22UD           0x044a0238
                            urb MsgDesc: 35 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g18UD           g22UD           0x044a0438
                            urb MsgDesc: 67 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g22UD           g22UD           0x044a0638
                            urb MsgDesc: 99 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g11UD           g5UD            0x04120003
                            sampler MsgDesc: sample SIMD8 Surface = 3 Sampler = 0 mlen 2 rlen 1 { align1 1Q };
send(8)         g12UD           g5UD            0x04120004
                            sampler MsgDesc: sample SIMD8 Surface = 4 Sampler = 0 mlen 2 rlen 1 { align1 1Q };
send(16)        g8UD            g12UD           0x08240003
                            sampler MsgDesc: sample SIMD16 Surface = 3 Sampler = 0 mlen 4 rlen 2 { align1 1H };
send(16)        g10UD           g12UD           0x08240004
                            sampler MsgDesc: sample SIMD16 Surface = 4 Sampler = 0 mlen 4 rlen 2 { align1 1H };
send(8)         g6UD            g7UD            0x08125001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 1 { align1 1Q };
send(8)         g7UD            g11UD           0x08125102
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 2 Sampler = 1 mlen 4 rlen 1 { align1 1Q };
send(16)        g10UD           g12UD           0x10245001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 8 rlen 2 { align1 1H };
send(16)        g12UD           g20UD           0x10245102
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 2 Sampler = 1 mlen 8 rlen 2 { align1 1H };
send(8)         g2UD            g13UD           0x0623a001
                            sampler MsgDesc: ld_lz SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 2 { align1 1Q };
send(16)        g6UD            g23UD           0x0c45a001
                            sampler MsgDesc: ld_lz SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1H };
send(8)         g124UD          g7UD            0x0c4b2000
                            sampler MsgDesc: gather4_po_c SIMD8 Surface = 0 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
send(8)         g13UD           g39UD           0x041a0058
                            urb MsgDesc: 5 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g4UD            g10UD           0x041a0068
                            urb MsgDesc: 6 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g4UD            g3UD            0x041a0078
                            urb MsgDesc: 7 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g4UD            g3UD            0x041a0088
                            urb MsgDesc: 8 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g4UD            g3UD            0x041a0098
                            urb MsgDesc: 9 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g4UD            g3UD            0x041a00a8
                            urb MsgDesc: 10 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g4UD            g3UD            0x041a00b8
                            urb MsgDesc: 11 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g4UD            g3UD            0x041a00c8
                            urb MsgDesc: 12 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a00d8
                            urb MsgDesc: 13 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a00e8
                            urb MsgDesc: 14 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a00f8
                            urb MsgDesc: 15 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0108
                            urb MsgDesc: 16 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0118
                            urb MsgDesc: 17 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0148
                            urb MsgDesc: 20 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0158
                            urb MsgDesc: 21 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0168
                            urb MsgDesc: 22 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0178
                            urb MsgDesc: 23 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0188
                            urb MsgDesc: 24 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0198
                            urb MsgDesc: 25 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a01a8
                            urb MsgDesc: 26 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a01b8
                            urb MsgDesc: 27 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a01c8
                            urb MsgDesc: 28 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a01d8
                            urb MsgDesc: 29 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a01e8
                            urb MsgDesc: 30 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a01f8
                            urb MsgDesc: 31 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g3UD            g2UD            0x041a0208
                            urb MsgDesc: 32 SIMD8 read per-slot mlen 2 rlen 1 { align1 1Q };
send(8)         g38UD           g38UD           0x084a8405
                            sampler MsgDesc: gather4 SIMD8 Surface = 5 Sampler = 4 mlen 4 rlen 4 { align1 1Q };
send(8)         g46UD           g23UD           0x064a8304
                            sampler MsgDesc: gather4 SIMD8 Surface = 4 Sampler = 3 mlen 3 rlen 4 { align1 1Q };
send(8)         g28UD           g28UD           0x064a8506
                            sampler MsgDesc: gather4 SIMD8 Surface = 6 Sampler = 5 mlen 3 rlen 4 { align1 1Q };
send(8)         g12UD           g23UD           0x064a8607
                            sampler MsgDesc: gather4 SIMD8 Surface = 7 Sampler = 6 mlen 3 rlen 4 { align1 1Q };
send(8)         g12UD           g32UD           0x084a8708
                            sampler MsgDesc: gather4 SIMD8 Surface = 8 Sampler = 7 mlen 4 rlen 4 { align1 1Q };
send(8)         g26UD           g13UD           0x064a8809
                            sampler MsgDesc: gather4 SIMD8 Surface = 9 Sampler = 8 mlen 3 rlen 4 { align1 1Q };
send(8)         g26UD           g26UD           0x084b090a
                            sampler MsgDesc: gather4_c SIMD8 Surface = 10 Sampler = 9 mlen 4 rlen 4 { align1 1Q };
send(8)         g2UD            g2UD            0x0a4b0a0b
                            sampler MsgDesc: gather4_c SIMD8 Surface = 11 Sampler = 10 mlen 5 rlen 4 { align1 1Q };
send(8)         g6UD            g6UD            0x084b0b0c
                            sampler MsgDesc: gather4_c SIMD8 Surface = 12 Sampler = 11 mlen 4 rlen 4 { align1 1Q };
send(16)        g30UD           g73UD           0x0a8c8304
                            sampler MsgDesc: gather4 SIMD16 Surface = 4 Sampler = 3 mlen 5 rlen 8 { align1 1H };
send(16)        g40UD           g2UD            0x0e8c8405
                            sampler MsgDesc: gather4 SIMD16 Surface = 5 Sampler = 4 mlen 7 rlen 8 { align1 1H };
send(16)        g5UD            g33UD           0x0a8c8506
                            sampler MsgDesc: gather4 SIMD16 Surface = 6 Sampler = 5 mlen 5 rlen 8 { align1 1H };
send(16)        g32UD           g55UD           0x0a8c8607
                            sampler MsgDesc: gather4 SIMD16 Surface = 7 Sampler = 6 mlen 5 rlen 8 { align1 1H };
send(16)        g30UD           g23UD           0x0e8c8708
                            sampler MsgDesc: gather4 SIMD16 Surface = 8 Sampler = 7 mlen 7 rlen 8 { align1 1H };
send(16)        g5UD            g40UD           0x0a8c8809
                            sampler MsgDesc: gather4 SIMD16 Surface = 9 Sampler = 8 mlen 5 rlen 8 { align1 1H };
send(16)        g38UD           g67UD           0x0e8d090a
                            sampler MsgDesc: gather4_c SIMD16 Surface = 10 Sampler = 9 mlen 7 rlen 8 { align1 1H };
send(16)        g38UD           g2UD            0x128d0a0b
                            sampler MsgDesc: gather4_c SIMD16 Surface = 11 Sampler = 10 mlen 9 rlen 8 { align1 1H };
send(16)        g10UD           g39UD           0x0e8d0b0c
                            sampler MsgDesc: gather4_c SIMD16 Surface = 12 Sampler = 11 mlen 7 rlen 8 { align1 1H };
send(8)         g2UD            g6UD            0x0e4b2000
                            sampler MsgDesc: gather4_po_c SIMD8 Surface = 0 Sampler = 0 mlen 7 rlen 4 { align1 1Q };
send(8)         g11UD           g7UD            0x04120102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 2 rlen 1 { align1 1Q };
send(8)         g12UD           g7UD            0x04120203
                            sampler MsgDesc: sample SIMD8 Surface = 3 Sampler = 2 mlen 2 rlen 1 { align1 1Q };
send(16)        g6UD            g11UD           0x08240102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 4 rlen 2 { align1 1H };
send(16)        g8UD            g11UD           0x08240203
                            sampler MsgDesc: sample SIMD16 Surface = 3 Sampler = 2 mlen 4 rlen 2 { align1 1H };
send(8)         g5UD            g6UD            0x04220003
                            sampler MsgDesc: sample SIMD8 Surface = 3 Sampler = 0 mlen 2 rlen 2 { align1 1Q };
send(16)        g8UD            g12UD           0x08440003
                            sampler MsgDesc: sample SIMD16 Surface = 3 Sampler = 0 mlen 4 rlen 4 { align1 1H };
send(8)         g5UD            g2UD            0x04129001
                            sampler MsgDesc: lod SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 1 { align1 1Q };
send(16)        g6UD            g2UD            0x08249001
                            sampler MsgDesc: lod SIMD16 Surface = 1 Sampler = 0 mlen 4 rlen 2 { align1 1H };
send(8)         g11UD           g4UD            0x04415002
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 2, SIMD16, Mask = 0x0) mlen 2 rlen 4 { align1 1Q };
send(8)         g7UD            g5UD            0x04416002
                            hdc1 MsgDesc: ( DC typed surface read, Surface = 2, SIMD8, Mask = 0x0) mlen 2 rlen 4 { align1 2Q };
send(8)         nullUD          g16UD           0x0e0a8057
                            urb MsgDesc: 5 SIMD8 write per-slot masked mlen 7 rlen 0 { align1 1Q };
send(8)         g6UD            g18UD           0x043a0318
                            urb MsgDesc: 49 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g9UD            g18UD           0x043a0518
                            urb MsgDesc: 81 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g12UD           g18UD           0x043a0718
                            urb MsgDesc: 113 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g15UD           g18UD           0x043a0918
                            urb MsgDesc: 145 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g11UD           g23UD           0x043a0218
                            urb MsgDesc: 33 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g14UD           g23UD           0x043a0418
                            urb MsgDesc: 65 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g17UD           g23UD           0x043a0618
                            urb MsgDesc: 97 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g20UD           g23UD           0x043a0818
                            urb MsgDesc: 129 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         nullUD          g12UD           0x0c0a8227
                            urb MsgDesc: 34 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g13UD           0x0c0a8237
                            urb MsgDesc: 35 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g14UD           0x0c0a8247
                            urb MsgDesc: 36 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g15UD           0x0c0a8257
                            urb MsgDesc: 37 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g16UD           0x0c0a8267
                            urb MsgDesc: 38 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g17UD           0x0c0a8277
                            urb MsgDesc: 39 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g18UD           0x0c0a8287
                            urb MsgDesc: 40 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g19UD           0x0c0a8297
                            urb MsgDesc: 41 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g20UD           0x0c0a82a7
                            urb MsgDesc: 42 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g21UD           0x0c0a82b7
                            urb MsgDesc: 43 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g22UD           0x0c0a82c7
                            urb MsgDesc: 44 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g23UD           0x0c0a82d7
                            urb MsgDesc: 45 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g24UD           0x0c0a82e7
                            urb MsgDesc: 46 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g25UD           0x0c0a82f7
                            urb MsgDesc: 47 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g26UD           0x0c0a8307
                            urb MsgDesc: 48 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g27UD           0x0c0a8317
                            urb MsgDesc: 49 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g28UD           0x0c0a8327
                            urb MsgDesc: 50 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g29UD           0x0c0a8337
                            urb MsgDesc: 51 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g30UD           0x0c0a8347
                            urb MsgDesc: 52 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x0c0a8357
                            urb MsgDesc: 53 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g32UD           0x0c0a8367
                            urb MsgDesc: 54 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g33UD           0x0c0a8377
                            urb MsgDesc: 55 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g34UD           0x0c0a8387
                            urb MsgDesc: 56 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g35UD           0x0c0a8397
                            urb MsgDesc: 57 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g36UD           0x0c0a83a7
                            urb MsgDesc: 58 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g37UD           0x0c0a83b7
                            urb MsgDesc: 59 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g38UD           0x0c0a83c7
                            urb MsgDesc: 60 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g39UD           0x0c0a83d7
                            urb MsgDesc: 61 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g40UD           0x0c0a83e7
                            urb MsgDesc: 62 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         nullUD          g41UD           0x0c0a83f7
                            urb MsgDesc: 63 SIMD8 write per-slot masked mlen 6 rlen 0 { align1 1Q };
send(8)         g8UD            g7UD            0x10134001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 8 rlen 1 { align1 1Q };
send(8)         g9UD            g15UD           0x10134102
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 2 Sampler = 1 mlen 8 rlen 1 { align1 1Q };
send(8)         g16UD           g16UD           0x044a0148
                            urb MsgDesc: 20 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g38UD           g38UD           0x084a8404
                            sampler MsgDesc: gather4 SIMD8 Surface = 4 Sampler = 4 mlen 4 rlen 4 { align1 1Q };
send(8)         g46UD           g23UD           0x064a8303
                            sampler MsgDesc: gather4 SIMD8 Surface = 3 Sampler = 3 mlen 3 rlen 4 { align1 1Q };
send(8)         g28UD           g28UD           0x064a8505
                            sampler MsgDesc: gather4 SIMD8 Surface = 5 Sampler = 5 mlen 3 rlen 4 { align1 1Q };
send(8)         g12UD           g23UD           0x064a8606
                            sampler MsgDesc: gather4 SIMD8 Surface = 6 Sampler = 6 mlen 3 rlen 4 { align1 1Q };
send(8)         g12UD           g32UD           0x084a8707
                            sampler MsgDesc: gather4 SIMD8 Surface = 7 Sampler = 7 mlen 4 rlen 4 { align1 1Q };
send(8)         g26UD           g13UD           0x064a8808
                            sampler MsgDesc: gather4 SIMD8 Surface = 8 Sampler = 8 mlen 3 rlen 4 { align1 1Q };
send(8)         g26UD           g26UD           0x084b0909
                            sampler MsgDesc: gather4_c SIMD8 Surface = 9 Sampler = 9 mlen 4 rlen 4 { align1 1Q };
send(8)         g2UD            g2UD            0x0a4b0a0a
                            sampler MsgDesc: gather4_c SIMD8 Surface = 10 Sampler = 10 mlen 5 rlen 4 { align1 1Q };
send(8)         g10UD           g10UD           0x084b0b0b
                            sampler MsgDesc: gather4_c SIMD8 Surface = 11 Sampler = 11 mlen 4 rlen 4 { align1 1Q };
send(8)         g2UD            g15UD           0x043a0048
                            urb MsgDesc: 4 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g12UD           g15UD           0x043a0058
                            urb MsgDesc: 5 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0068
                            urb MsgDesc: 6 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0078
                            urb MsgDesc: 7 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0088
                            urb MsgDesc: 8 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0098
                            urb MsgDesc: 9 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a00a8
                            urb MsgDesc: 10 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a00b8
                            urb MsgDesc: 11 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a00c8
                            urb MsgDesc: 12 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a00d8
                            urb MsgDesc: 13 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a00e8
                            urb MsgDesc: 14 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a00f8
                            urb MsgDesc: 15 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0108
                            urb MsgDesc: 16 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0118
                            urb MsgDesc: 17 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0138
                            urb MsgDesc: 19 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0148
                            urb MsgDesc: 20 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0158
                            urb MsgDesc: 21 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0168
                            urb MsgDesc: 22 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0178
                            urb MsgDesc: 23 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0188
                            urb MsgDesc: 24 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0198
                            urb MsgDesc: 25 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a01a8
                            urb MsgDesc: 26 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a01b8
                            urb MsgDesc: 27 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a01c8
                            urb MsgDesc: 28 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a01d8
                            urb MsgDesc: 29 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a01e8
                            urb MsgDesc: 30 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a01f8
                            urb MsgDesc: 31 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g2UD            g2UD            0x043a0208
                            urb MsgDesc: 32 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         nullUD          g11UD           0x140a0047
                            urb MsgDesc: 4 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g31UD           0x140a0087
                            urb MsgDesc: 8 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q };
send(8)         nullUD          g118UD          0x940a0087
                            urb MsgDesc: 8 SIMD8 write per-slot mlen 10 rlen 0 { align1 1Q EOT };
send(8)         g14UD           g11UD           0x0a4b0202
                            sampler MsgDesc: gather4_c SIMD8 Surface = 2 Sampler = 2 mlen 5 rlen 4 { align1 1Q };
send(8)         g18UD           g18UD           0x0c4b0303
                            sampler MsgDesc: gather4_c SIMD8 Surface = 3 Sampler = 3 mlen 6 rlen 4 { align1 1Q };
send(8)         g22UD           g24UD           0x084b0404
                            sampler MsgDesc: gather4_c SIMD8 Surface = 4 Sampler = 4 mlen 4 rlen 4 { align1 1Q };
send(8)         g15UD           g2UD            0x06423203
                            sampler MsgDesc: sample_c SIMD8 Surface = 3 Sampler = 2 mlen 3 rlen 4 { align1 1Q };
send(16)        g19UD           g27UD           0x0c843203
                            sampler MsgDesc: sample_c SIMD16 Surface = 3 Sampler = 2 mlen 6 rlen 8 { align1 1H };
send(8)         g7UD            g9UD            0x0a13c001
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 1 { align1 1Q };
send(16)        g20UD           g7UD            0x1425c001
                            sampler MsgDesc: ld2dms_w SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 2 { align1 1H };
send(8)         g21UD           g5UD            0x0a33c001
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 3 { align1 1Q };
send(8)         g18UD           g24UD           0x0a23c001
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 2 { align1 1Q };
send(16)        g15UD           g21UD           0x1465c001
                            sampler MsgDesc: ld2dms_w SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 6 { align1 1H };
send(16)        g7UD            g31UD           0x1445c001
                            sampler MsgDesc: ld2dms_w SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 4 { align1 1H };
send(8)         g124UD          g6UD            0x04438303
                            sampler MsgDesc: sample_lz SIMD8 Surface = 3 Sampler = 3 mlen 2 rlen 4 { align1 1Q };
send(8)         g11UD           g17UD           0x043a0338
                            urb MsgDesc: 51 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g14UD           g17UD           0x043a0538
                            urb MsgDesc: 83 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g17UD           g17UD           0x043a0738
                            urb MsgDesc: 115 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g9UD            g18UD           0x043a0038
                            urb MsgDesc: 3 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g12UD           g18UD           0x043a0238
                            urb MsgDesc: 35 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g15UD           g18UD           0x043a0438
                            urb MsgDesc: 67 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g18UD           g18UD           0x043a0638
                            urb MsgDesc: 99 SIMD8 read per-slot mlen 2 rlen 3 { align1 1Q };
send(8)         g6UD            g10UD           0x08424001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 4 { align1 1Q };
send(8)         g9UD            g5UD            0x04420002
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(16)        g13UD           g7UD            0x08840002
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 0 mlen 4 rlen 8 { align1 1H };
(+f1.0) send(8) g124UD          g2UD            0x0211a501
                            hdc1 MsgDesc: ( DC typed atomic, Surface = 1, SIMD16, inc) mlen 1 rlen 1 { align1 1Q };
(+f1.0) send(8) g121UD          g3UD            0x0211b501
                            hdc1 MsgDesc: ( DC typed atomic, Surface = 1, SIMD8, inc) mlen 1 rlen 1 { align1 2Q };
send(8)         g22UD           g32UD           0x02280238
                            urb MsgDesc: 35 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g24UD           g32UD           0x02280438
                            urb MsgDesc: 67 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g26UD           g32UD           0x02280638
                            urb MsgDesc: 99 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g28UD           g32UD           0x02280248
                            urb MsgDesc: 36 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g30UD           g32UD           0x02280448
                            urb MsgDesc: 68 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g32UD           g32UD           0x02280648
                            urb MsgDesc: 100 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g22UD           g33UD           0x02280258
                            urb MsgDesc: 37 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g24UD           g33UD           0x02280458
                            urb MsgDesc: 69 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g26UD           g33UD           0x02280658
                            urb MsgDesc: 101 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g22UD           g34UD           0x02280268
                            urb MsgDesc: 38 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g24UD           g34UD           0x02280468
                            urb MsgDesc: 70 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g26UD           g34UD           0x02280668
                            urb MsgDesc: 102 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g24UD           g35UD           0x02280478
                            urb MsgDesc: 71 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g35UD           0x02280278
                            urb MsgDesc: 39 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g26UD           g35UD           0x02280678
                            urb MsgDesc: 103 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g24UD           g36UD           0x02280688
                            urb MsgDesc: 104 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g36UD           0x02280288
                            urb MsgDesc: 40 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g36UD           0x02280488
                            urb MsgDesc: 72 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g8UD            g37UD           0x02280298
                            urb MsgDesc: 41 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g37UD           0x02280498
                            urb MsgDesc: 73 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g24UD           g37UD           0x02280698
                            urb MsgDesc: 105 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g38UD           0x022802a8
                            urb MsgDesc: 42 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g38UD           0x022804a8
                            urb MsgDesc: 74 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g24UD           g38UD           0x022806a8
                            urb MsgDesc: 106 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g39UD           0x022802b8
                            urb MsgDesc: 43 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g39UD           0x022804b8
                            urb MsgDesc: 75 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g24UD           g39UD           0x022806b8
                            urb MsgDesc: 107 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g40UD           0x022802c8
                            urb MsgDesc: 44 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g40UD           0x022804c8
                            urb MsgDesc: 76 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g40UD           0x022806c8
                            urb MsgDesc: 108 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g41UD           0x022802d8
                            urb MsgDesc: 45 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g41UD           0x022804d8
                            urb MsgDesc: 77 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g41UD           0x022806d8
                            urb MsgDesc: 109 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g42UD           0x022802e8
                            urb MsgDesc: 46 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g42UD           0x022804e8
                            urb MsgDesc: 78 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g42UD           0x022806e8
                            urb MsgDesc: 110 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g43UD           0x022802f8
                            urb MsgDesc: 47 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g43UD           0x022804f8
                            urb MsgDesc: 79 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g22UD           g43UD           0x022806f8
                            urb MsgDesc: 111 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g44UD           0x02280308
                            urb MsgDesc: 48 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g44UD           0x02280508
                            urb MsgDesc: 80 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g44UD           0x02280708
                            urb MsgDesc: 112 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g45UD           0x02280318
                            urb MsgDesc: 49 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g45UD           0x02280518
                            urb MsgDesc: 81 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g45UD           0x02280718
                            urb MsgDesc: 113 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g46UD           0x02280328
                            urb MsgDesc: 50 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g46UD           0x02280528
                            urb MsgDesc: 82 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g46UD           0x02280728
                            urb MsgDesc: 114 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g47UD           0x02280338
                            urb MsgDesc: 51 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g47UD           0x02280538
                            urb MsgDesc: 83 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g47UD           0x02280738
                            urb MsgDesc: 115 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g48UD           0x02280348
                            urb MsgDesc: 52 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g48UD           0x02280548
                            urb MsgDesc: 84 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g48UD           0x02280748
                            urb MsgDesc: 116 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g49UD           0x02280358
                            urb MsgDesc: 53 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g49UD           0x02280558
                            urb MsgDesc: 85 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g49UD           0x02280758
                            urb MsgDesc: 117 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g50UD           0x02280368
                            urb MsgDesc: 54 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g50UD           0x02280568
                            urb MsgDesc: 86 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g50UD           0x02280768
                            urb MsgDesc: 118 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g53UD           0x02280378
                            urb MsgDesc: 55 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g53UD           0x02280578
                            urb MsgDesc: 87 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g53UD           0x02280778
                            urb MsgDesc: 119 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g54UD           0x02280388
                            urb MsgDesc: 56 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g54UD           0x02280588
                            urb MsgDesc: 88 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g54UD           0x02280788
                            urb MsgDesc: 120 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g55UD           0x02280398
                            urb MsgDesc: 57 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g55UD           0x02280598
                            urb MsgDesc: 89 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g55UD           0x02280798
                            urb MsgDesc: 121 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g56UD           0x022803a8
                            urb MsgDesc: 58 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g56UD           0x022805a8
                            urb MsgDesc: 90 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g56UD           0x022807a8
                            urb MsgDesc: 122 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g57UD           0x022803b8
                            urb MsgDesc: 59 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g57UD           0x022805b8
                            urb MsgDesc: 91 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g57UD           0x022807b8
                            urb MsgDesc: 123 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g58UD           0x022803c8
                            urb MsgDesc: 60 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g58UD           0x022805c8
                            urb MsgDesc: 92 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g58UD           0x022807c8
                            urb MsgDesc: 124 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g59UD           0x022803d8
                            urb MsgDesc: 61 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g59UD           0x022805d8
                            urb MsgDesc: 93 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g59UD           0x022807d8
                            urb MsgDesc: 125 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g60UD           0x022803e8
                            urb MsgDesc: 62 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g60UD           0x022805e8
                            urb MsgDesc: 94 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g60UD           0x022807e8
                            urb MsgDesc: 126 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g61UD           0x022803f8
                            urb MsgDesc: 63 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g61UD           0x022805f8
                            urb MsgDesc: 95 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g61UD           0x022807f8
                            urb MsgDesc: 127 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g10UD           g62UD           0x02280408
                            urb MsgDesc: 64 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g62UD           0x02280608
                            urb MsgDesc: 96 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g14UD           g62UD           0x02280808
                            urb MsgDesc: 128 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g8UD            g63UD           0x02280218
                            urb MsgDesc: 33 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g10UD           g63UD           0x02280418
                            urb MsgDesc: 65 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g12UD           g63UD           0x02280618
                            urb MsgDesc: 97 SIMD8 read mlen 1 rlen 2        { align1 1Q };
send(8)         g14UD           g63UD           0x02280818
                            urb MsgDesc: 129 SIMD8 read mlen 1 rlen 2       { align1 1Q };
send(8)         g29UD           g18UD           0x04420008
                            sampler MsgDesc: sample SIMD8 Surface = 8 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(8)         g35UD           g18UD           0x04420109
                            sampler MsgDesc: sample SIMD8 Surface = 9 Sampler = 1 mlen 2 rlen 4 { align1 1Q };
send(8)         g41UD           g18UD           0x0442020a
                            sampler MsgDesc: sample SIMD8 Surface = 10 Sampler = 2 mlen 2 rlen 4 { align1 1Q };
send(8)         g2UD            g18UD           0x0442030b
                            sampler MsgDesc: sample SIMD8 Surface = 11 Sampler = 3 mlen 2 rlen 4 { align1 1Q };
send(8)         g6UD            g18UD           0x0442040c
                            sampler MsgDesc: sample SIMD8 Surface = 12 Sampler = 4 mlen 2 rlen 4 { align1 1Q };
send(8)         g10UD           g18UD           0x0442050d
                            sampler MsgDesc: sample SIMD8 Surface = 13 Sampler = 5 mlen 2 rlen 4 { align1 1Q };
send(8)         g14UD           g18UD           0x0442060e
                            sampler MsgDesc: sample SIMD8 Surface = 14 Sampler = 6 mlen 2 rlen 4 { align1 1Q };
send(8)         g18UD           g18UD           0x0442070f
                            sampler MsgDesc: sample SIMD8 Surface = 15 Sampler = 7 mlen 2 rlen 4 { align1 1Q };
send(16)        g32UD           g22UD           0x08840008
                            sampler MsgDesc: sample SIMD16 Surface = 8 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(16)        g42UD           g22UD           0x08840109
                            sampler MsgDesc: sample SIMD16 Surface = 9 Sampler = 1 mlen 4 rlen 8 { align1 1H };
send(16)        g60UD           g22UD           0x0884020a
                            sampler MsgDesc: sample SIMD16 Surface = 10 Sampler = 2 mlen 4 rlen 8 { align1 1H };
send(16)        g70UD           g22UD           0x0884030b
                            sampler MsgDesc: sample SIMD16 Surface = 11 Sampler = 3 mlen 4 rlen 8 { align1 1H };
send(16)        g78UD           g22UD           0x0884040c
                            sampler MsgDesc: sample SIMD16 Surface = 12 Sampler = 4 mlen 4 rlen 8 { align1 1H };
send(16)        g86UD           g22UD           0x0884050d
                            sampler MsgDesc: sample SIMD16 Surface = 13 Sampler = 5 mlen 4 rlen 8 { align1 1H };
send(16)        g94UD           g22UD           0x0884060e
                            sampler MsgDesc: sample SIMD16 Surface = 14 Sampler = 6 mlen 4 rlen 8 { align1 1H };
send(16)        g52UD           g22UD           0x0884070f
                            sampler MsgDesc: sample SIMD16 Surface = 15 Sampler = 7 mlen 4 rlen 8 { align1 1H };
send(8)         g16UD           g42UD           0x04438101
                            sampler MsgDesc: sample_lz SIMD8 Surface = 1 Sampler = 1 mlen 2 rlen 4 { align1 1Q };
send(8)         g20UD           g42UD           0x04438202
                            sampler MsgDesc: sample_lz SIMD8 Surface = 2 Sampler = 2 mlen 2 rlen 4 { align1 1Q };
send(8)         g29UD           g42UD           0x04438404
                            sampler MsgDesc: sample_lz SIMD8 Surface = 4 Sampler = 4 mlen 2 rlen 4 { align1 1Q };
send(8)         g38UD           g42UD           0x04438606
                            sampler MsgDesc: sample_lz SIMD8 Surface = 6 Sampler = 6 mlen 2 rlen 4 { align1 1Q };
send(8)         g124UD          g42UD           0x04438707
                            sampler MsgDesc: sample_lz SIMD8 Surface = 7 Sampler = 7 mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g16UD           0x044a0058
                            urb MsgDesc: 5 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0068
                            urb MsgDesc: 6 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0078
                            urb MsgDesc: 7 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0088
                            urb MsgDesc: 8 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0098
                            urb MsgDesc: 9 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a00a8
                            urb MsgDesc: 10 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a00b8
                            urb MsgDesc: 11 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a00c8
                            urb MsgDesc: 12 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a00d8
                            urb MsgDesc: 13 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a00e8
                            urb MsgDesc: 14 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a00f8
                            urb MsgDesc: 15 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0108
                            urb MsgDesc: 16 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0118
                            urb MsgDesc: 17 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0158
                            urb MsgDesc: 21 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0168
                            urb MsgDesc: 22 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0178
                            urb MsgDesc: 23 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0188
                            urb MsgDesc: 24 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0198
                            urb MsgDesc: 25 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a01a8
                            urb MsgDesc: 26 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a01b8
                            urb MsgDesc: 27 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a01c8
                            urb MsgDesc: 28 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a01d8
                            urb MsgDesc: 29 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a01e8
                            urb MsgDesc: 30 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a01f8
                            urb MsgDesc: 31 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g12UD           g2UD            0x044a0208
                            urb MsgDesc: 32 SIMD8 read per-slot mlen 2 rlen 4 { align1 1Q };
send(8)         g14UD           g15UD           0x0a125001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 1 { align1 1Q };
send(8)         g15UD           g20UD           0x0a125102
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 2 Sampler = 1 mlen 5 rlen 1 { align1 1Q };
send(16)        g41UD           g7UD            0x14245001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 10 rlen 2 { align1 1H };
send(16)        g43UD           g17UD           0x14245102
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 2 Sampler = 1 mlen 10 rlen 2 { align1 1H };
send(8)         g2UD            g5UD            0x06223001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 2 { align1 1Q };
send(16)        g2UD            g7UD            0x0c443001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 4 { align1 1H };
send(8)         g2UD            g2UD            0x06323001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 3 { align1 1Q };
send(16)        g2UD            g24UD           0x0c643001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 6 rlen 6 { align1 1H };
send(8)         nullUD          g120UD          0x8c0a0117
                            urb MsgDesc: 17 SIMD8 write per-slot mlen 6 rlen 0 { align1 1Q EOT };
send(8)         g11UD           g1UD            0x02380128
                            urb MsgDesc: 18 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x02380138
                            urb MsgDesc: 19 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x02380148
                            urb MsgDesc: 20 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x02380158
                            urb MsgDesc: 21 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x02380168
                            urb MsgDesc: 22 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x02380178
                            urb MsgDesc: 23 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x02380188
                            urb MsgDesc: 24 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x02380198
                            urb MsgDesc: 25 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x023801a8
                            urb MsgDesc: 26 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x023801b8
                            urb MsgDesc: 27 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x023801c8
                            urb MsgDesc: 28 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x023801d8
                            urb MsgDesc: 29 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x023801e8
                            urb MsgDesc: 30 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g11UD           g1UD            0x023801f8
                            urb MsgDesc: 31 SIMD8 read mlen 1 rlen 3        { align1 1Q };
send(8)         g10UD           g2UD            0x04420004
                            sampler MsgDesc: sample SIMD8 Surface = 4 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(16)        g18UD           g2UD            0x08840004
                            sampler MsgDesc: sample SIMD16 Surface = 4 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(8)         g10UD           g2UD            0x04420003
                            sampler MsgDesc: sample SIMD8 Surface = 3 Sampler = 0 mlen 2 rlen 4 { align1 1Q };
send(16)        g18UD           g2UD            0x08840003
                            sampler MsgDesc: sample SIMD16 Surface = 3 Sampler = 0 mlen 4 rlen 8 { align1 1H };
send(8)         g11UD           g13UD           0x042a0058
                            urb MsgDesc: 5 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a0068
                            urb MsgDesc: 6 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a0078
                            urb MsgDesc: 7 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a0088
                            urb MsgDesc: 8 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a0098
                            urb MsgDesc: 9 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a00a8
                            urb MsgDesc: 10 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a00b8
                            urb MsgDesc: 11 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a00c8
                            urb MsgDesc: 12 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a00d8
                            urb MsgDesc: 13 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a00e8
                            urb MsgDesc: 14 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a00f8
                            urb MsgDesc: 15 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a0108
                            urb MsgDesc: 16 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g11UD           0x042a0118
                            urb MsgDesc: 17 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a0158
                            urb MsgDesc: 21 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a0168
                            urb MsgDesc: 22 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a0178
                            urb MsgDesc: 23 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a0188
                            urb MsgDesc: 24 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a0198
                            urb MsgDesc: 25 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a01a8
                            urb MsgDesc: 26 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a01b8
                            urb MsgDesc: 27 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a01c8
                            urb MsgDesc: 28 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a01d8
                            urb MsgDesc: 29 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a01e8
                            urb MsgDesc: 30 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a01f8
                            urb MsgDesc: 31 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g2UD            g3UD            0x042a0208
                            urb MsgDesc: 32 SIMD8 read per-slot mlen 2 rlen 2 { align1 1Q };
send(8)         g9UD            g15UD           0x021ab102
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 2 Sampler = 1 mlen 1 rlen 1 { align1 1Q };
send(8)         g10UD           g16UD           0x021ab203
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 3 Sampler = 2 mlen 1 rlen 1 { align1 1Q };
send(8)         g11UD           g17UD           0x021ab304
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 4 Sampler = 3 mlen 1 rlen 1 { align1 1Q };
send(8)         g12UD           g18UD           0x021ab405
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 5 Sampler = 4 mlen 1 rlen 1 { align1 1Q };
send(8)         g13UD           g19UD           0x021ab506
                            sampler MsgDesc: sampleinfo SIMD8 Surface = 6 Sampler = 5 mlen 1 rlen 1 { align1 1Q };
send(16)        g14UD           g16UD           0x022cb102
                            sampler MsgDesc: sampleinfo SIMD16 Surface = 2 Sampler = 1 mlen 1 rlen 2 { align1 1H };
send(16)        g16UD           g18UD           0x022cb203
                            sampler MsgDesc: sampleinfo SIMD16 Surface = 3 Sampler = 2 mlen 1 rlen 2 { align1 1H };
send(16)        g18UD           g20UD           0x022cb304
                            sampler MsgDesc: sampleinfo SIMD16 Surface = 4 Sampler = 3 mlen 1 rlen 2 { align1 1H };
send(16)        g20UD           g22UD           0x022cb405
                            sampler MsgDesc: sampleinfo SIMD16 Surface = 5 Sampler = 4 mlen 1 rlen 2 { align1 1H };
send(16)        g22UD           g24UD           0x022cb506
                            sampler MsgDesc: sampleinfo SIMD16 Surface = 6 Sampler = 5 mlen 1 rlen 2 { align1 1H };
send(8)         g14UD           g11UD           0x0a4b0203
                            sampler MsgDesc: gather4_c SIMD8 Surface = 3 Sampler = 2 mlen 5 rlen 4 { align1 1Q };
send(8)         g18UD           g18UD           0x0c4b0304
                            sampler MsgDesc: gather4_c SIMD8 Surface = 4 Sampler = 3 mlen 6 rlen 4 { align1 1Q };
send(8)         g22UD           g24UD           0x084b0405
                            sampler MsgDesc: gather4_c SIMD8 Surface = 5 Sampler = 4 mlen 4 rlen 4 { align1 1Q };
send(16)        g18UD           g26UD           0x128d0203
                            sampler MsgDesc: gather4_c SIMD16 Surface = 3 Sampler = 2 mlen 9 rlen 8 { align1 1H };
send(16)        g26UD           g35UD           0x168d0304
                            sampler MsgDesc: gather4_c SIMD16 Surface = 4 Sampler = 3 mlen 11 rlen 8 { align1 1H };
send(16)        g34UD           g46UD           0x0e8d0405
                            sampler MsgDesc: gather4_c SIMD16 Surface = 5 Sampler = 4 mlen 7 rlen 8 { align1 1H };
send(8)         g124UD          g9UD            0x0c4b0000
                            sampler MsgDesc: gather4_c SIMD8 Surface = 0 Sampler = 0 mlen 6 rlen 4 { align1 1Q };
