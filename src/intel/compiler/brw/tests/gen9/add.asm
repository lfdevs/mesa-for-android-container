        add (8)                   r124:f        r7<8;8,1>:d       1:d
        add (16)                  r120:f        r11<8;8,1>:d      1:d
        add (16)                  r4:f          r1<0>:f           -r1.4<0>:f
(W)     add (8)                   r3.8:uw       r3<8;8,1>:uw      8:uw
        add (16)                  r3:d          r18<8;8,1>:d      r12<8;8,1>:d
(W)     add (16)                  r6:uw         r1.4<1;4,0>:uw    0x11001010:v
(W)     add (32)                  r10:uw        r1.4<1;4,0>:uw    0x11001010:v
        add (8)                   r2:d          r96<8;8,1>:d      -1023:d
        add (8)                   r4:f          r5.6<0>:f         r7.2<0>:f
        add (8)                   r53:df        r49<4;4,1>:df     r51<4;4,1>:df
        add.sat (16)              r5            r3<8;8,1>         0x00000001
(W)     add (1)                   r125.3        r0.3<0>           r7<0>
        add (8)                   a0:uw         r34<16;8,2>:uw    128:uw
        add (8|M8)                r8:df         r2<0>:df          r3.2<0>:df
        add (16)                  a0:uw         r3<16;8,2>:uw     64:uw
        add.sat (8)    (le)f0.0   r125:f        -r6<8;8,1>:f      0x3f000000:f
        add (8)        (eq)f0.0   r8:f          r2<0>:f           -r2.4<0>:f
        add (16)       (eq)f0.0   r3:f          r2<0>:f           -r2.1<0>:f
        add (8)                   r3            r2<8;8,1>         0xffffffff
(f0.0)  add (8)                   r15:d         -r15<8;8,1>:d     31:d
(W)     add (1)                   a0            a0<0>             0x00000200
        add.sat (8)               r124:f        r7<8;8,1>:f       -r6<8;8,1>:f
        add (8)                   r8            r6<8;8,1>:d       0x00000001
        add (16)                  r11           r9<8;8,1>:d       0x00000001
(f0.0)  add (16)                  r8:d          -r8<8;8,1>:d      31:d
        add.sat (16)              r126:f        r2<0>:f           r2.4<0>:f
        add.sat (8)               r124:f        r17<8;8,1>:d      1:d
        add (16|M16)              r114:d        r118<8;8,1>:d     r116<8;8,1>:d
        add (16)       (eq)f0.0   null          r120<8;8,1>:d     1:d
        add (16|M16)   (eq)f0.0   null          r116<8;8,1>:d     1:d
        add (8)        (eq)f0.0   r3:d          r5<8;8,1>:d       r4<8;8,1>:d
        add (16)                  r20           r17<8;8,1>        1:d
        add (8)                   r7:f          -r6<4;0,0>.xyxy:f r6<4;0,0>.zwzw:f {Align16}
        add (16)                  r9:f          -r7<4;0,0>.xyxy:f r7<4;0,0>.zwzw:f {Align16}
(W)     add (8)                   r7            r2<8;8,1>         -r6<8;8,1>
        add (16)       (le)f0.0   r1:d          r3.1<0>:d         -r6<8;8,1>:d
        add.sat (8)               r10           r9<8;8,1>         0x00000001
(W)     add (1|M8)                r14           r14<0>            0x00000001
        add (8)                   r25:q         r22<4;4,1>:q      -r24<4;4,1>:q
        add (8|M8)                r12:q         r5<4;4,1>:q       -r11<4;4,1>:q
