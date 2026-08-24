### etnaviv_noop backend

This implements the minimum of etnaviv in order to make shader-db work.
The submit ioctl is stubbed out to not execute anything.

Export `MESA_LOADER_DRIVER_OVERRIDE=etnaviv
LD_PRELOAD=$prefix/lib/libetnaviv_noop_drm_shim.so`.

It is possible to select a specific GPU by setting the ETNA_SHIM_GPU
environment variable to its identity, given in hex as
`model:revision[:product:customer:eco]`. The three trailing values default to
zero, and the default identity is a GC2000. For example the GC7000 in an
i.MX8QM is reached with `ETNA_SHIM_GPU=7000:6009:70008`.

Any GPU in the hardware database can be selected this way. `bin/drm-shim.py`
carries a name for the ones worth remembering one for, so

```
drm-shim.py gc7000-r6214 <command>
```

is the same thing without looking the numbers up. Querying an identity the hardware
database does not know of results in an error: the shim describes only the identity of
the GPU, so there is nothing left to fall back to.
