#!/bin/bash

RP="rampart"

EXTRACTOR="./WikiExtractor.py"

DATADIR="./wikidata"
DBDIR="./wikidb"
HAVEPV=""

die () {
	echo $1
	exit 1
}


curl --version &>/dev/null || die "curl must be installed and in the current \$PATH before running this script"

pv --help &>/dev/null && {
	HAVEPV="1"
} || {
	echo "WARNING: The pv util is not installed or is not in the current \$PATH.  If you wish to have a progress bar while unzipping the downloaded wikipedia file, please exit and install with e.g. \"apt install pv\""
	echo
}

echo "In order to create the wikipedia demo search, several directories will be made and the current English Wikipedia dump will be downloaded."
echo "The dump file is very large (>17Gb) and will also take significant time to unzip."
echo "The file will be downloaded using curl.  If interrupted, please run this script again and curl will attempt to resume the download."
echo "If an old version of enwiki-latest-pages-articles.xml.bz2 exists in this directory, please quit and move/delete that file first in order to download the latest dump."
echo
read -p "Continue [y|N]? " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]
then
    echo "bye"
    exit 1
fi

echo "Downloading enwiki-latest-pages-articles.xml.bz2 to current directory"
curl -C - -o enwiki-latest-pages-articles.xml.bz2 https://dumps.wikimedia.org/enwiki/latest/enwiki-latest-pages-articles.xml.bz2 || die "download failed"

echo "Decompressing..."

if [ -e enwiki-latest-pages-articles.xml ]; then
    echo "enwiki-latest-pages-articles.xml exists.";
    read -p "[o]verwrite or [c]ontinue with existing" -n 1 -r
    if [[ $REPLY =~ ^[oO]$ ]]; then
	echo
        if [ "$HAVEPV" == "1" ] ; then
                cat enwiki-latest-pages-articles.xml.bz2 | pv -s $(ls -l enwiki-latest-pages-articles.xml.bz2 | awk '{print $5}') | bzcat -d > enwiki-latest-pages-articles.xml || die "Failed to decompress file"
        else
                bunzip2 -k enwiki-latest-pages-articles.xml.bz2
        fi
    fi
else
    if [ "$HAVEPV" == "1" ] ; then
            cat enwiki-latest-pages-articles.xml.bz2 | pv -s $(du -sb enwiki-latest-pages-articles.xml.bz2 | awk '{print $1}') | bzcat -d > enwiki-latest-pages-articles.xml || die "Failed to decompress file"
    else
            bunzip2 -k enwiki-latest-pages-articles.xml.bz2
    fi
fi

echo "Extracting text from enwiki-latest-pages-articles.xml"

mkdir -p "$DATADIR/txt" || die "could not make directory $DATADIR/txt"

#./WikiExtractor.py -o "$DATADIR/txt" enwiki-latest-pages-articles.xml|| die "failed to extract text from enwiki-latest-pages-articles.xml"
./WikiExtractor.py -o "$DATADIR/txt" enwiki-latest-pages-articles.xml 2>&1 | tee extractor-output.txt | while read i; do 
	line=$(echo -n $i | grep -oE '[[:digit:]]+.+'); 
	printf "%s            \r" "$line"; 
done || die "failed to extract text from enwiki-latest-pages-articles.xml"

echo "importing data"
rampart import.js
echo "creating text index"
rampart mkindex.js

#echo "Now you can run \"rampart import.js\" and \"rampart mkindex.js\" to create the database."
#echo "After it is made, \"rampart wksearch.js\" will start the server"

