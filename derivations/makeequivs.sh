#!/bin/bash

SSOURCE==$(readlink -- "${BASH_SOURCE[0]}")
SDIR=$(dirname "${SSOURCE}")

die() {
    echo $1
    exit 1
}

if [ "$RAMPART" == "" ]; then
    RAMPART=`which rampart`;
fi

if [ ! -e "$RAMPART" ] ; then
    die "can't find rampart executable"
fi

if [ "$BACKREF" == "" ]; then
    BACKREF=`which backref`;
fi

if [ ! -e "$BACKREF" ] ; then
    die "can't find backref executable"
fi

DIRS=`ls -d */`;

for dir in $DIRS; do
    dir="${dir%?}"
    echo $dir;
    eqvfile="${SDIR}/${dir}/${dir}-deriv"
    lstfile="${eqvfile}.lst"
    revfile="${eqvfile}-rev.lst"
    DOPARSE="";
    if [ ! -e "$lstfile" ] ; then
        DOPARSE="true";
    else
        for tsv in `ls ${SDIR}/${dir}/*.tsv`; do
            if [ "$tsv" -nt "$lstfile" ]; then
                DOPARSE="true"
                break
            fi
        done
    fi
    if [ "$DOPARSE" == "true" ] ; then
        $RAMPART ./parsederiveqv.js ${SDIR}/${dir}/*.tsv > $lstfile || die "error parsing ${dir}" 
    fi;
    DOBACKREF=""
    if [ ! -e "$eqvfile" ] ; then
        DOBACKREF="true"
    else
        if [ "$lstfile" -nt "$eqvfile" ] ; then
            DOBACKREF="true"
        fi
    fi
    if [ "$DOBACKREF" == "true" ] ; then
        $BACKREF -d $lstfile $eqvfile > $revfile || die "error creating derivations file for ${dir}"
    fi
done
