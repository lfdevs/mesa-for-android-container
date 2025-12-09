#!/usr/bin/python3
#
# Copyright © 2024-2025 Tomeu Vizoso
#
# SPDX-License-Identifier: MIT

import collections
import csv
import subprocess
import sys
from itertools import dropwhile
import camelot

trm_file = sys.argv[1]
if trm_file.endswith(".pdf"):
    data = subprocess.check_output(["pdftotext", "-tsv", sys.argv[1], "-"]).decode()
else:
    assert(trm_file.endswith(".txt"))
    data = open(sys.argv[1]).read()

data = csv.reader(data.splitlines(), delimiter="\t")
data = collections.deque([x[11] for x in data])

def popcell(data):
    cell = []
    while data[0] != "###FLOW###":
        text = data.popleft()
        cell.append(text)
    data.popleft() ###FLOW###
    data.popleft() ###LINE###
    return cell

text = None
while data[0] != "RKNN_pc_operation_enable":
    data.popleft()

def read_reg_offset(data):
    while data:
        text = data.popleft()
        if text.startswith("(0x"):
            return text.replace("(", "").replace(")", "")

reg_names = []
offsets = []
while text != "RKNN_global_operation_enable":
    text = data.popleft()

    if text.startswith("RKNN_"):
        reg_names.append(text)
        offsets.append(read_reg_offset(data))

print("Found %d registers in RKNN block" % len(reg_names))

"""
print(reg_names)
print(offsets)
sys.exit(0)
"""

tables = camelot.read_pdf(sys.argv[1], line_scale=35, pages="0-60")
tables = collections.deque([x.data for x in tables[3:]])

# Join tables split by page breaks
new_tables = []
while tables:
    new_table = tables.popleft()
    last_bitfield = new_table[-1][0].split(" ")[0]
    while last_bitfield != "0" and not last_bitfield.endswith(":0"):
        second_part = tables.popleft()
        new_table.extend(second_part[1:])
        last_bitfield = second_part[-1][0].split(" ")[0]
    new_tables.append(new_table)
tables = new_tables
print("Found %d tables in PDF" % len(tables))

domains = {}
for i in range(0, len(reg_names)):
    reg_name = reg_names[i]
    if "dpu_rdma" in reg_name:
        domain = "dpu_rdma"
    elif "ppu_rdma" in reg_name:
        domain = "ppu_rdma"
    else:
        domain = reg_name.split("_")[1]
    table = tables[i]

    if domain not in domains.keys():
        domains[domain] = []

    reg = {}
    reg["name"] = reg_name
    reg["offset"] = offsets[i]
    reg["field_names"] = []
    reg["field_bits"] = []

    reserved_count = 0
    for row in table[1:]:
        name = row[3].split('\n')[0]

        if name == "reserved":
            name = "reserved_%d" % reserved_count
            reserved_count += 1

        reg["field_bits"].append(row[0].split(' ')[0])
        reg["field_names"].append(name)

    domains[domain].append(reg)

for domain in domains.keys():
    print('    <domain name="%s" width="32">' % domain.upper())
    for reg in domains[domain]:
        print('        <reg32 offset="%s" name="%s">' % (reg["offset"], "_".join(reg["name"].strip().upper().split("_")[2:])))
        for i in range(0, len(reg["field_names"])):
            if ":" in reg["field_bits"][i]:
                high, low = reg["field_bits"][i].split(":")
                bits = 'low="%s" high="%s"' % (low, high)
            else:
                bits = 'pos="%s"' % reg["field_bits"][i]
            print('            <bitfield name="%s" %s type="uint"/>' % (reg["field_names"][i].strip().upper(), bits))
        print('        </reg32>')
    print('    </domain>')
