#!/bin/sh

for i in ./*.xml; do
    echo -n "Processing $i... "
    python ./gen_sort_tags.py $i > $i.tmp
    mv $i.tmp $i
    echo "done."
done
