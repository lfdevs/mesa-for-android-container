#!/usr/bin/env python3

import re
import sys
import gzip
import io


class GMEMPass:
    def __init__(self):
        self.cleared = None
        self.gmem_reason = None
        self.num_draws = None
        self.samples = None

        self.width = None
        self.height = None
        self.nbinx = None
        self.nbiny = None

        self.formats = []         # format per MRT + zsbuf
        self.lrz_clear_time = 0
        self.binning_time = 0
        self.restore_clear_time = 0
        self.draw_time = 0
        self.resolve_time = 0
        self.elapsed_time = 0

def dump_gmem_passes(gmem_passes, blit_time, sysmem_time, total_time):
    i = 0
    lrz_clear_time = 0
    binning_time = 0
    restore_clear_time = 0
    draw_time = 0
    resolve_time = 0
    elapsed_time = 0
    for gmem in gmem_passes:
        print("  GMEM[{}]: {}x{} ({}x{} tiles), {} draws, lrz clear: {:,} ns, binning: {:,} ns, restore/clear: {:,} ns, draw: {:,} ns, resolve: {:,} ns, total: {:,} ns, rt/zs: {}".format(
                i, gmem.width, gmem.height, gmem.nbinx, gmem.nbiny, gmem.num_draws,
                gmem.lrz_clear_time, gmem.binning_time, gmem.restore_clear_time,
                gmem.draw_time, gmem.resolve_time, gmem.elapsed_time,
                ", ".join(gmem.formats)
            ))
        lrz_clear_time += gmem.lrz_clear_time
        binning_time += gmem.binning_time
        restore_clear_time += gmem.restore_clear_time
        draw_time += gmem.draw_time
        resolve_time += gmem.resolve_time
        elapsed_time += gmem.elapsed_time
        i += 1

    print("  TOTAL: lrz clear: {:,} ns ({}%), binning: {:,} ns ({}%), restore/clear: {:,} ns ({}%), draw: {:,} ns ({}%), resolve: {:,} ns ({}%), blit: {:,} ns ({}%), sysmem: {:,} ns ({}%), total: {:,} ns\n".format(
            lrz_clear_time, 100.0 * lrz_clear_time / total_time,
            binning_time, 100.0 * binning_time / total_time,
            restore_clear_time, 100.0 * restore_clear_time / total_time,
            draw_time, 100.0 * draw_time / total_time,
            resolve_time, 100.0 * resolve_time / total_time,
            blit_time, 100.0 * blit_time / total_time,
            sysmem_time, 100.0 * sysmem_time / total_time,
            total_time
        ))

def main():
    filename = sys.argv[1]
    if filename.endswith(".gz"):
        file = gzip.open(filename, "r")
        file = io.TextIOWrapper(file)
    else:
        file = open(filename, "r")
    lines = file.read().split('\n')

    compute_match = re.compile(r"COMPUTE: START")
    gmem_start_match = re.compile(r": GMEM: cleared=(\S+), gmem_reason=(\S+), num_draws=(\S+), samples=(\S+)")
    gmem_match = re.compile(r": rendering (\S+)x(\S+) tiles (\S+)x(\S+)")
    gmem_surf_match = re.compile(r": {format = (\S+),")
    gmem_lrz_clear_match = re.compile(r"\+(\S+): END LRZ CLEAR")
    gmem_binning_match = re.compile(r"\+(\S+): GMEM: END BINNING IB")
    gmem_restore_clear_match = re.compile(r"\+(\S+): TILE: END CLEAR/RESTORE")
    gmem_draw_match = re.compile(r"\+(\S+): TILE\[\S+\]: END DRAW IB")
    gmem_resolve_match = re.compile(r"\+(\S+): TILE: END RESOLVE")
    sysmem_match = re.compile(r": rendering sysmem (\S+)x(\S+)")
    blit_match = re.compile(r": END BLIT")
    elapsed_match = re.compile(r"ELAPSED: (\S+) ns")
    eof_match = re.compile(r"END OF FRAME (\S+)")

    # Times in ns:
    times_blit = []
    times_sysmem = []
    times_gmem = []
    times_compute = []
    times = None
    gmem_passes = []     # set of GMEM passes in frame
    gmem = None          # current GMEM pass

    for line in lines:
        match = re.search(compute_match, line)
        if match is not None:
            #printf("GRID/COMPUTE")
            if times  is not None:
                print("expected times to not be set yet")
            times = times_compute
            continue

        match = re.search(gmem_start_match, line)
        if match is not None:
            if gmem is not None:
                print("expected gmem to not be set yet")
            if times is not None:
                print("expected times to not be set yet")
            times = times_gmem
            gmem = GMEMPass()
            gmem.cleared = match.group(1)
            gmem.gmem_reason = match.group(2)
            gmem.num_draws = match.group(3)
            gmem.samples = match.group(4)

        if gmem is not None:
            match = re.search(gmem_match, line)
            if match is not None:
                gmem.width = int(match.group(1))
                gmem.height = int(match.group(2))
                gmem.nbinx = int(match.group(3))
                gmem.nbiny = int(match.group(4))
                continue

            match = re.search(gmem_surf_match, line)
            if match is not None:
                gmem.formats.append(match.group(1))
                continue

            match = re.search(gmem_lrz_clear_match, line)
            if match is not None:
                gmem.lrz_clear_time += int(match.group(1))
                continue

            match = re.search(gmem_binning_match, line)
            if match is not None:
                gmem.binning_time += int(match.group(1))
                continue

            match = re.search(gmem_restore_clear_match, line)
            if match is not None:
                gmem.restore_clear_time += int(match.group(1))
                continue

            match = re.search(gmem_draw_match, line)
            if match is not None:
                gmem.draw_time += int(match.group(1))
                continue

            match = re.search(gmem_resolve_match, line)
            if match is not None:
                gmem.resolve_time += int(match.group(1))
                continue


        match = re.search(sysmem_match, line)
        if match is not None:
            #print("SYSMEM")
            if times is not None:
                print("expected times to not be set yet")
            times = times_sysmem
            continue

        match = re.search(blit_match, line)
        if match is not None:
            #print("BLIT")
            if times is not None:
                print("expected times to not be set yet")
            times = times_blit
            continue

        match = re.search(eof_match, line)
        if match is not None:
            frame_nr = int(match.group(1))
            print("FRAME[{}]: {} blits ({:,} ns), {} SYSMEM ({:,} ns), {} GMEM ({:,} ns), {} COMPUTE ({:,} ns)".format(
                    frame_nr,
                    len(times_blit), sum(times_blit),
                    len(times_sysmem), sum(times_sysmem),
                    len(times_gmem), sum(times_gmem),
                    len(times_compute), sum(times_compute)
                ))
            if len(gmem_passes) > 0:
                total_time = sum(times_blit) + sum(times_sysmem) + sum(times_gmem) + sum(times_compute)
                dump_gmem_passes(gmem_passes, sum(times_blit), sum(times_sysmem), total_time)
            times_blit = []
            times_sysmem = []
            times_gmem = []
            times = None
            gmem_passes = []
            times_compute = []
            gmem = None
            continue

        match = re.search(elapsed_match, line)
        if match is not None:
            time = int(match.group(1))
            #print("ELAPSED: " + str(time) + " ns")
            times.append(time)
            times = None
            if gmem is not None:
                gmem.elapsed_time = time
                gmem_passes.append(gmem)
                gmem = None
            continue


if __name__ == "__main__":
    main()

