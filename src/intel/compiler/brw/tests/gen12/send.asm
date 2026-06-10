        send.hdc1 (16)            r113    r12    null    0x00000000  a0.0         {@1,$6}
(f1.0)  send.hdc1 (16)            null    r15    r17:2   0x00000080  a0.0         {@1,$4}
        send.hdc1 (8|M8)          r104    r119:2 null    0x00000000  0x04116E13   {$8}  // typed_read.hdc1.x.bti[19]
        send.hdc1 (8)             null    r92:1  r117    a0.2        0x020350FC   {@1,$8}
(W&f0.0.any8h) send.hdc0 (8)      r55     r118:1 null    0x00000000  0x02184201   {@3,$9}  // oword_unaligned_block_read.hdc0.owords2.bti[1]
(W)     send.ts (8)               null    r126:1 null    0x00000000  0x02000000   {EOT,@1}
        send.hdc1 (8)             r18     r24:2  null    0x00000000  0x04115E10   {$1}  // typed_read.hdc1.x.simd16.bti[16]
        send.hdc1 (8|M8)          r19     r28:2  null    0x00000000  0x04116E10   {@7,$2}  // typed_read.hdc1.x.bti[16]
        send.smpl (16)         r50     r36    null    0x00000000  a0.0         {@1,$3}
        send.hdc1 (8)             null    r25:1  r21:4   0x00000100  0x02035001   {$9}  // typed_write.hdc1.xyzw.simd16.bti[1]
        send.hdc1 (8)             r5      r25:1  null    0x00000000  0x02415001   {$10}  // typed_read.hdc1.xyzw.simd16.bti[1]
        send.hdc1 (8)             r27     r35:2  null    0x00000000  0x04146EFD   {@1,$0}  // a64_untyped_read.hdc1.x
        send.hdc1 (8)             null    r36:2  r38:4   0x00000100  0x04035001   {@1,$1}  // typed_write.hdc1.xyzw.simd16.bti[1]
        send.urb (8)              null    r126:1 r118:8  0x00000200  0x02080007   {EOT,@1}  // simd8_write.urb
        send.hdc0 (8)             r14     r37:1  null    0x00000000  0x02110401   {@1,$0}  // byte_scattered_read.hdc0.d16.bti[1]
(W)     send.hdc0 (1)             r100    r0:1   null    0x00000000  0x0219E000   {$1}  // memory_fence.hdc0
(W)     send.hdc0 (1)             r15     r0:1   null    0x00000000  0x0219E000   {$5}  // memory_fence.hdc0
        sendc.render (16)         null    r119:8 null    0x00000000  0x10031000   {EOT,@1}  // rt_write.render.last_rt.bti[0]
        sendc.render (8)          null    r125:2 r123:2  0x00000080  0x04031400   {EOT,@1}  // rt_write.render.last_rt.bti[0]
        sendc.render (16)         null    r123:4 r119:4  0x00000100  0x08031000   {EOT,@1}  // rt_write.render.last_rt.bti[0]
