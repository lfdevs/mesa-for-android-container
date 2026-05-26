sendc(8)        nullUD          g124UD          0x88031400
                            render MsgDesc: RT write SIMD8 LastRT Surface = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g120UD          0x90031000
                            render MsgDesc: RT write SIMD16 LastRT Surface = 0 mlen 8 rlen 0 { align1 1H EOT };
sendc(16)       nullUD          g114UD          0x82031100
                            render MsgDesc: RT write SIMD16/RepData LastRT Surface = 0 mlen 1 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g124UD          0x880ba001
                            sampler MsgDesc: ld_lz SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g121UD          0x8e0da001
                            sampler MsgDesc: ld_lz SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g125UD          0x860a0001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0c0001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1H EOT };
(+f0.1) sendc(8) nullUD         g124UD          0x88031400
                            render MsgDesc: RT write SIMD8 LastRT Surface = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g122UD          0x8c0be001
                            sampler MsgDesc: ld2dms SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g117UD          0x960de001
                            sampler MsgDesc: ld2dms SIMD16 Surface = 1 Sampler = 0 mlen 11 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g124UD          0x880a0001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g121UD          0x8e0c0001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g118UD          0x940a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 10 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g125UD          a0<0,1,0>UD     0x80000200
                            sampler MsgDesc: indirect                       { align1 1Q EOT };
sendc(8)        nullUD          g124UD          0x880a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g121UD          0x8e0bc001
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g121UD          0x8e0a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g125UD          0x860a2001
                            sampler MsgDesc: sample_l SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0c2001
                            sampler MsgDesc: sample_l SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g122UD          0x8c0b1401
                            render MsgDesc: RT write SIMD8 LastRT Surface = 1 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g118UD          0x940b1001
                            render MsgDesc: RT write SIMD16 LastRT Surface = 1 mlen 10 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g13UD           0x0e0b0401
                            render MsgDesc: RT write SIMD8 Surface = 1 mlen 7 rlen 0 { align1 1Q };
sendc(8)        nullUD          g121UD          0x8e0b1402
                            render MsgDesc: RT write SIMD8 LastRT Surface = 2 mlen 7 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g7UD            0x180b0001
                            render MsgDesc: RT write SIMD16 Surface = 1 mlen 12 rlen 0 { align1 1H };
sendc(16)       nullUD          g116UD          0x980b1002
                            render MsgDesc: RT write SIMD16 LastRT Surface = 2 mlen 12 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g123UD          0x8a0a1001
                            sampler MsgDesc: sample_b SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g119UD          0x920c1001
                            sampler MsgDesc: sample_b SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 0 { align1 1H EOT };
sendc(1)        g2UD            g2UD            0x0209c000
                            hdc0 MsgDesc: ( DC mfence, 0, 0) mlen 1 rlen 0  { align1 WE_all 1N };
sendc(8)        nullUD          g120UD          0x900b4001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 8 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g123UD          0x8a0b4001
                            sampler MsgDesc: sample_d_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(8)        g6UD            g2UD            0x044b4100
                            render MsgDesc: RT read MsgCtrl = 0x1 Surface = 0 mlen 2 rlen 4 { align1 1Q };
sendc(16)       g9UD            g27UD           0x048b4000
                            render MsgDesc: RT read MsgCtrl = 0x0 Surface = 0 mlen 2 rlen 8 { align1 1H };
sendc(8)        nullUD          g124UD          0x880a3001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g121UD          0x8e0c3001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g123UD          0x8a031400
                            render MsgDesc: RT write SIMD8 LastRT Surface = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g118UD          0x94031000
                            render MsgDesc: RT write SIMD16 LastRT Surface = 0 mlen 10 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g5UD            0x0c0b0400
                            render MsgDesc: RT write SIMD8 Surface = 0 mlen 6 rlen 0 { align1 1Q };
sendc(8)        nullUD          g5UD            0x0c0b0401
                            render MsgDesc: RT write SIMD8 Surface = 1 mlen 6 rlen 0 { align1 1Q };
sendc(8)        nullUD          g5UD            0x0c0b0402
                            render MsgDesc: RT write SIMD8 Surface = 2 mlen 6 rlen 0 { align1 1Q };
sendc(8)        nullUD          g5UD            0x0c0b0403
                            render MsgDesc: RT write SIMD8 Surface = 3 mlen 6 rlen 0 { align1 1Q };
sendc(8)        nullUD          g5UD            0x0c0b0404
                            render MsgDesc: RT write SIMD8 Surface = 4 mlen 6 rlen 0 { align1 1Q };
sendc(8)        nullUD          g122UD          0x8c0b1405
                            render MsgDesc: RT write SIMD8 LastRT Surface = 5 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g5UD            0x140b0000
                            render MsgDesc: RT write SIMD16 Surface = 0 mlen 10 rlen 0 { align1 1H };
sendc(16)       nullUD          g5UD            0x140b0001
                            render MsgDesc: RT write SIMD16 Surface = 1 mlen 10 rlen 0 { align1 1H };
sendc(16)       nullUD          g5UD            0x140b0002
                            render MsgDesc: RT write SIMD16 Surface = 2 mlen 10 rlen 0 { align1 1H };
sendc(16)       nullUD          g5UD            0x140b0003
                            render MsgDesc: RT write SIMD16 Surface = 3 mlen 10 rlen 0 { align1 1H };
sendc(16)       nullUD          g5UD            0x140b0004
                            render MsgDesc: RT write SIMD16 Surface = 4 mlen 10 rlen 0 { align1 1H };
sendc(16)       nullUD          g118UD          0x940b1005
                            render MsgDesc: RT write SIMD16 LastRT Surface = 5 mlen 10 rlen 0 { align1 1H EOT };
sendc(8)        g6UD            g6UD            0x044b4101
                            render MsgDesc: RT read MsgCtrl = 0x1 Surface = 1 mlen 2 rlen 4 { align1 1Q };
sendc(8)        g10UD           g10UD           0x044b4102
                            render MsgDesc: RT read MsgCtrl = 0x1 Surface = 2 mlen 2 rlen 4 { align1 1Q };
sendc(8)        g14UD           g14UD           0x044b4103
                            render MsgDesc: RT read MsgCtrl = 0x1 Surface = 3 mlen 2 rlen 4 { align1 1Q };
sendc(8)        nullUD          g122UD          0x8c0b1403
                            render MsgDesc: RT write SIMD8 LastRT Surface = 3 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       g32UD           g14UD           0x048b4001
                            render MsgDesc: RT read MsgCtrl = 0x0 Surface = 1 mlen 2 rlen 8 { align1 1H };
sendc(16)       g40UD           g16UD           0x048b4002
                            render MsgDesc: RT read MsgCtrl = 0x0 Surface = 2 mlen 2 rlen 8 { align1 1H };
sendc(16)       g48UD           g18UD           0x048b4003
                            render MsgDesc: RT read MsgCtrl = 0x0 Surface = 3 mlen 2 rlen 8 { align1 1H };
sendc(16)       nullUD          g118UD          0x940b1003
                            render MsgDesc: RT write SIMD16 LastRT Surface = 3 mlen 10 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g124UD          0x880a1001
                            sampler MsgDesc: sample_b SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g121UD          0x8e0c1001
                            sampler MsgDesc: sample_b SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g125UD          0x860ba001
                            sampler MsgDesc: ld_lz SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0da001
                            sampler MsgDesc: ld_lz SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g126UD          0x840a0001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 2 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g125UD          0x860c0001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 3 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g124UD          0x880a2001
                            sampler MsgDesc: sample_l SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g121UD          0x8e0c2001
                            sampler MsgDesc: sample_l SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g123UD          0x8a0be001
                            sampler MsgDesc: ld2dms SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g119UD          0x920de001
                            sampler MsgDesc: ld2dms SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g120UD          0x900a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 8 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g123UD          0x8a0a2001
                            sampler MsgDesc: sample_l SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g119UD          0x920c2001
                            sampler MsgDesc: sample_l SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g125UD          0x860a0304
                            sampler MsgDesc: sample SIMD8 Surface = 4 Sampler = 3 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0c0304
                            sampler MsgDesc: sample SIMD16 Surface = 4 Sampler = 3 mlen 5 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g122UD          0x8c0a1001
                            sampler MsgDesc: sample_b SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g117UD          0x960c1001
                            sampler MsgDesc: sample_b SIMD16 Surface = 1 Sampler = 0 mlen 11 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g125UD          0x860a3001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0c3001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g122UD          0x8c0b1402
                            render MsgDesc: RT write SIMD8 LastRT Surface = 2 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g118UD          0x940b1002
                            render MsgDesc: RT write SIMD16 LastRT Surface = 2 mlen 10 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g124UD          0x880a6001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g121UD          0x8e0c6001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g123UD          0x8a0a5001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g119UD          0x920c5001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g122UD          0x8c0a2001
                            sampler MsgDesc: sample_l SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g117UD          0x960c2001
                            sampler MsgDesc: sample_l SIMD16 Surface = 1 Sampler = 0 mlen 11 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g122UD          0x8c0bc001
                            sampler MsgDesc: ld2dms_w SIMD8 Surface = 1 Sampler = 0 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g117UD          0x960dc001
                            sampler MsgDesc: ld2dms_w SIMD16 Surface = 1 Sampler = 0 mlen 11 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g122UD          0x8c0b1400
                            render MsgDesc: RT write SIMD8 LastRT Surface = 0 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g118UD          0x940b1000
                            render MsgDesc: RT write SIMD16 LastRT Surface = 0 mlen 10 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g124UD          0x880a7001
                            sampler MsgDesc: ld SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g121UD          0x8e0c7001
                            sampler MsgDesc: ld SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g118UD          0x940b1200
                            render MsgDesc: RT write SIMD8/DualSrcLow LastRT Surface = 0 mlen 10 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g3UD            0x140b1200
                            render MsgDesc: RT write SIMD8/DualSrcLow LastRT Surface = 0 mlen 10 rlen 0 { align1 1Q };
sendc(8)        nullUD          g118UD          0x940b1300
                            render MsgDesc: RT write SIMD8/DualSrcHigh LastRT Surface = 0 mlen 10 rlen 0 { align1 2Q EOT };
sendc(8)        nullUD          g123UD          0x8a0a0001
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g119UD          0x920c0001
                            sampler MsgDesc: sample SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 0 { align1 1H EOT };
sendc(16)       g11UD           g37UD           0x048b6000
                            render MsgDesc: RT read MsgCtrl = 0x32 Surface = 0 mlen 2 rlen 8 { align1 1H };
sendc(8)        nullUD          g23UD           0x0c0b0405
                            render MsgDesc: RT write SIMD8 Surface = 5 mlen 6 rlen 0 { align1 1Q };
sendc(8)        nullUD          g29UD           0x0c0b0406
                            render MsgDesc: RT write SIMD8 Surface = 6 mlen 6 rlen 0 { align1 1Q };
sendc(8)        nullUD          g122UD          0x8c0b1407
                            render MsgDesc: RT write SIMD8 LastRT Surface = 7 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g57UD           0x140b0005
                            render MsgDesc: RT write SIMD16 Surface = 5 mlen 10 rlen 0 { align1 1H };
sendc(16)       nullUD          g67UD           0x140b0006
                            render MsgDesc: RT write SIMD16 Surface = 6 mlen 10 rlen 0 { align1 1H };
sendc(16)       nullUD          g118UD          0x940b1007
                            render MsgDesc: RT write SIMD16 LastRT Surface = 7 mlen 10 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g125UD          0x860a1001
                            sampler MsgDesc: sample_b SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0c1001
                            sampler MsgDesc: sample_b SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g10UD           0x0e0b0400
                            render MsgDesc: RT write SIMD8 Surface = 0 mlen 7 rlen 0 { align1 1Q };
sendc(8)        nullUD          g121UD          0x8e0b1401
                            render MsgDesc: RT write SIMD8 LastRT Surface = 1 mlen 7 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g2UD            0x160b0000
                            render MsgDesc: RT write SIMD16 Surface = 0 mlen 11 rlen 0 { align1 1H };
sendc(16)       nullUD          g117UD          0x960b1001
                            render MsgDesc: RT write SIMD16 LastRT Surface = 1 mlen 11 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g122UD          0x8c0b1404
                            render MsgDesc: RT write SIMD8 LastRT Surface = 4 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g118UD          0x940b1004
                            render MsgDesc: RT write SIMD16 LastRT Surface = 4 mlen 10 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g122UD          0x8c0b1406
                            render MsgDesc: RT write SIMD8 LastRT Surface = 6 mlen 6 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g118UD          0x940b1006
                            render MsgDesc: RT write SIMD16 LastRT Surface = 6 mlen 10 rlen 0 { align1 1H EOT };
sendc(16)       nullUD          g119UD          0x92031000
                            render MsgDesc: RT write SIMD16 LastRT Surface = 0 mlen 9 rlen 0 { align1 1H EOT };
sendc(16)       nullUD          g116UD          0x980b1001
                            render MsgDesc: RT write SIMD16 LastRT Surface = 1 mlen 12 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g123UD          0x8a0a6001
                            sampler MsgDesc: sample_l_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g119UD          0x920c6001
                            sampler MsgDesc: sample_l_c SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g125UD          0x860a0102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0c0102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 5 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g124UD          0x880a5001
                            sampler MsgDesc: sample_b_c SIMD8 Surface = 1 Sampler = 0 mlen 4 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g121UD          0x8e0c5001
                            sampler MsgDesc: sample_b_c SIMD16 Surface = 1 Sampler = 0 mlen 7 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g123UD          0x8a0a4001
                            sampler MsgDesc: sample_d SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g123UD          0x8a0a3001
                            sampler MsgDesc: sample_c SIMD8 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g119UD          0x920c3001
                            sampler MsgDesc: sample_c SIMD16 Surface = 1 Sampler = 0 mlen 9 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g125UD          0x860a0f10
                            sampler MsgDesc: sample SIMD8 Surface = 16 Sampler = 15 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0c0f10
                            sampler MsgDesc: sample SIMD16 Surface = 16 Sampler = 15 mlen 5 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g126UD          0x840a0102
                            sampler MsgDesc: sample SIMD8 Surface = 2 Sampler = 1 mlen 2 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g125UD          0x860c0102
                            sampler MsgDesc: sample SIMD16 Surface = 2 Sampler = 1 mlen 3 rlen 0 { align1 1H EOT };
sendc(16)       nullUD          g11UD           0x180b0000
                            render MsgDesc: RT write SIMD16 Surface = 0 mlen 12 rlen 0 { align1 1H };
sendc(8)        nullUD          g122UD          0x8c031400
                            render MsgDesc: RT write SIMD8 LastRT Surface = 0 mlen 6 rlen 0 { align1 1Q EOT };
sendc(8)        nullUD          g125UD          0x860a0506
                            sampler MsgDesc: sample SIMD8 Surface = 6 Sampler = 5 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0c0506
                            sampler MsgDesc: sample SIMD16 Surface = 6 Sampler = 5 mlen 5 rlen 0 { align1 1H EOT };
sendc(8)        nullUD          g125UD          0x860b8001
                            sampler MsgDesc: sample_lz SIMD8 Surface = 1 Sampler = 0 mlen 3 rlen 0 { align1 1Q EOT };
sendc(16)       nullUD          g123UD          0x8a0d8001
                            sampler MsgDesc: sample_lz SIMD16 Surface = 1 Sampler = 0 mlen 5 rlen 0 { align1 1H EOT };
