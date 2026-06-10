(W&f0.0.any8h) send.ugm (1)       r57     r58:1  null    0x02000000  0x6210C500   {$5}  // load.ugm.d32x8t.a32.bti[2]
(W&f0.0.any8h) send.ugm (1)       r28     r29:1  null    0x02000000  0x6210C500   {$2}  // load.ugm.d32x8t.a32.bti[2]
(W&f0.0.any32h) send.ugm (1)      r57     r58:1  null    0x02000000  0x6210C500   {$0}  // load.ugm.d32x8t.a32.bti[2]
        send.ugm (8)              null    r79:1  r10:4   0x04000100  0x6200F506   {$0}  // store_cmask.ugm.d32.xyzw.a32.bti[4]
        send.ugm (16)             null    r9:2   r7      a0.2        0x44000504   {@1,$0}  // store.ugm.d32.a32.ss[a0.2]
(W)     send.tgm (1)              r4      r0:1   null    0x00000000  0x0210151F   {$3}  // fence.tgm.tile.evict
        send.slm (8)              null    r36:1  r37:1   0x00000040  0x02000B04   {$1}  // store.slm.d16u32.a32
        send.slm (8)              null    r34:1  r35:1   0x00000040  0x02000B04   {$0}  // store.slm.d16u32.a32
        send.slm (8)              null    r6:1   r7:4    0x00000100  0x0200F506   {$6}  // store_cmask.slm.d32.xyzw.a32
        send.slm (16|M16)         null    r82:2  r91:2   0x00000080  0x04040519   {$0}  // atomic_or.slm.d32.a32.uc.wb
(W)     send.slm (1)              r10     r0:1   null    0x00000000  0x0210011F   {$1}  // fence.slm.threadgroup.none
(W)     send.ugm (1)              r23     r117:1 null    a0.2        0x2210C500   {ExBSO,@1,$10}  // load.ugm.d32x8t.a32.bss[a0.2]
        send.hdc1 (8)             null    r14:2  r24:4   a0.2        0x040350FC   {ExBSO,@1,$5}
        send.rtaccel (8)          null    r51:1  r52:1   0x00000040  0x02000000   {$2}  // trace_ray.rtaccel
        send.rtaccel (16)         null    r88:1  r98:2   0x00000080  0x02000100   {$6}  // trace_ray.rtaccel
