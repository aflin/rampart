#!/bin/bash

cd $1
outfile_c=$2
outfile_h=$3

echo '#include "texint.h"' > $outfile_c

for f in mappings-monobyte/*.txt; do \
	awk -f procmap $f >> $outfile_c ; \
done

awk '/CONST/ {gsub(" *=","");printf("extern %s;\n", $$0);}; /#define/ {print $0};' $outfile_c >$outfile_h
