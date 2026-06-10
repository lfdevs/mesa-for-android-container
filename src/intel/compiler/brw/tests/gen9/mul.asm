        mul (8)                   r22:f         r4<8;8,1>:f       r2<0>:f
        mul (16)                  r26:f         r2<0>:f           r2<0>:f
        mul (8)                   r36:df        r8<0>:df          r8<0>:df
        mul (8)                   r9            r86<8;8,1>        0x00000004
        mul (8)                   acc0          r17<8;8,1>        43691:uw
        mul (8)                   acc0:d        r17<8;8,1>:d      21846:uw
        mul (8)                   r21:d         r20<8;8,1>:d      3:d
        mul (8|M8)                acc0          r39<8;8,1>        43691:uw
        mul (16)                  r45:d         r43<8;8,1>:d      3:d
        mul (8|M8)                acc0:d        r39<8;8,1>:d      21846:uw
        mul (8)        (eq)f0.0   r10:f         r5<0>:f           r9<8;8,1>:f
        mul (8|M8)                r39:df        r3.3<0>:df        r3.3<0>:df
        mul (16)       (eq)f0.0   r6:f          r2<0>:f           r4<8;8,1>:f
        mul.sat (8)               r17:f         r4<8;8,1>:f       r16<8;8,1>:f
        mul.sat (16)              r9:f          r3<8;8,1>:f       r7<8;8,1>:f
        mul (8)        (lt)f0.0   null          r6<0>:f           r5.7<0>:f
        mul.sat (8)               r8:df         r34<4;4,1>:df     r5<4;4,1>:df
        mul (8)                   r4:uq         r8<4;4,1>         r12<4;4,1>
        mul (8|M8)                r20:uq        r5<4;4,1>         r13<4;4,1>
        mul (8)                   r5:q          r9<4;4,1>:d       r13<4;4,1>:d
        mul.sat (8|M8)            r10:df        r10<4;4,1>:df     r16<4;4,1>:df
        mul (8)        (lt)f0.0   r20:f         r2<8;8,1>:f       0x42700000:f
        mul (16)       (lt)f0.0   r32:f         r2<8;8,1>:f       0x42700000:f
(W)     mul (1)                   r6            r12<0>            0x00000101
        mul (8|M8)                r21:q         r6<4;4,1>:d       r14<4;4,1>:d
        mul (16)       (lt)f0.0   null          r2.2<0>:f         r2.1<0>:f
        mul (8)                   r6:uw         r6<8;8,1>:uw      2056:uw
        mul (16)                  r15:uw        r14<16;16,1>:uw   2056:uw
        mul (8)        (ne)f0.0   r6:f          r12<8;8,1>:f      0x3f808000:f
        mul (16)       (ne)f0.0   r9:f          r7<8;8,1>:f       0x3f808000:f
(W)     mul (1|M8)                r4            r4<0>             0x00000101
