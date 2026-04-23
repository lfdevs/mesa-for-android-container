# Glossary

**lane**: A single work-item.

**subgroup**: A collection of 8, 16, or 32 lanes executing in lockstep.
Avoid using the term _thread_ as it is ambiguous.

**uniform**: A value that has the same value in every active lane of a subgroup.
Sometimes called _convergent_. Opposite of "non-uniform".

**non-uniform**: A value that may have different values in different active
lanes within a subgroup. Sometimes called _divergent_. Opposite of "uniform".

**GPR**: General-purpose register, a single non-uniform value viewed from the
perspective of a single lane. This is a 'virtual' or 'logical' register within
the SIMT programming model. It does not represent a physical machine
register. For that, see "GRF".

**UGPR**: Uniform general purpose register, a single uniform value. This is
again a virtual or logical register.

**GRF**: A physical Intel GPU register. On Xe2+, a GRF is 512-bits. On older
platforms, a GRF is 256-bits. Depending on the platform and the SIMD width,
different numbers of GRFs required to store a single GPR, and different numbers
of UGPRs fit into a single GRF. In SIMD32 mode on Xe2, 1 GPR requires 2 GRFs,
and 16 UGPRs fit into 1 GRF.

**scalar**: A single value from the perspective of a single lane; a single GPR
or UGPR. Note that a scalar may be either uniform or non-uniform. Opposite of
"vector".

**vector**: A collection of multiple values from the perspective of a single
lane. All scalars within the vector must be identically be GPRs or UGPRs.

# Introduction

Jay separates the logical register files (GPR and UGPR) from the
unified physical register file. We assign registers independently for each
logical file, and then post-RA we remap to physical GRFs. This simplifies RA.

We decide a static GPR/UGPR split up front. Ideally, we'd just use the
first N registers for GPRs and the rest for UGPRs, or something like
that. Unfortunately, several hardware issues complicate this scheme...

# End-of-thread SENDs

End-of-thread SENDs require their source is in r112-r127. As their source will
always be per-thread, we want to make sure these are GPRs.

# Payloads

At the start of each thread, the register file is preloaded with a payload.
Parts of the payload act like UGPRs, parts act like GPRs, and parts act like...
something weird and in between. To minimize copying, we want to assign UGPRs to
the UGPR parts of the payload and GPRs to the GPR parts. As for the weird cases,
we model them as UGPR vectors and use special opcodes (lowered late to
regioning) to unpack to GPRs for normal handling.
