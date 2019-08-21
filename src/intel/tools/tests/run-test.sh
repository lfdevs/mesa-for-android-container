#!/bin/sh

SRCDIR="${srcdir}"
I965_ASM="$1"

test="TEST"

if [ -n "$2" ] ; then
   test="$2"
fi

if [ -n "$3" ] ; then
   gen="$3"
fi

for file in ${SRCDIR}/${test}/*.asm; do
   if [ -f "$file" ]; then
      filename="${file%.*}"
      "${I965_ASM}" -t hex -g ${gen} -o "${file}.out" "${file}"
      if cmp "${file}.out" "${filename}.expected" 2> /dev/null; then
         echo "${file} : PASS"
      else
         echo "Output comparison for ${file}"
         diff -u "${filename}.expected" "${file}.out"
      fi
   fi
done
