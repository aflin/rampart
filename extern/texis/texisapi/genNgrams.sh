#!/bin/sh

s=$1; shift;
f=$1; shift;

rm -f $f
echo '#include <stdio.h>'       >$f
echo '#include <stdlib.h>'     >>$f
echo '#include <sys/types.h>'  >>$f
echo '#include "texint.h"'     >>$f
for langf in `echo $@`; do \
  lang=`basename $langf .txt`; \
  ./genNgrams genNgramSet 3 ${langf} ${lang}; \
done >>$f
echo "const TXNGRAMSETLANG   TXngramsetlangs[] =" >>$f
echo "{"                       >>$f
for langf in `echo $@`; do \
  lang=`basename $langf .txt`; \
  echo "  { &${lang}Ngramset, \"${lang}\" }," || exit 1; \
done >>$f
echo "  { NULL, \"\" }"        >>$f
echo "};"                      >>$f
