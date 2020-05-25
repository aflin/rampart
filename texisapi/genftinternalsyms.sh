#!/bin/bash

outfile=$1
shift

echo "#ifndef FTINTERNALSYMBOLS_LIST" > $outfile
echo -n "#define FTINTERNALSYMBOLS_LIST " >> $outfile

for i in `cat $@ 2>/dev/null | sort`; do
	echo " \\" >> $outfile
	echo -n "I($i)" >> $outfile
done;
echo "" >> $outfile
echo "#endif /* FTINTERNALSYMBOLS_LIST */" >> $outfile
