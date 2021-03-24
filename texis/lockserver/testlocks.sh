#!/bin/bash


delay() {
#	sleep `echo $RANDOM | sed 's/^\(.\)/\1\./'`
	sleep 1;
	read -p $*
}

PRETTY=true
LOCKMODE=CR

POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    -p|--pretty)
  	PRETTY="$2"
    shift # past argument
    shift # past value
    ;;
    -l|--lockmode)
    LOCKMODE="$2"
    shift # past argument
    shift # past value
    ;;
    *)    # unknown option
    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

( \
	echo -e '{"connect":{"database":"db1","pretty":' $PRETTY '}}' \
	 && delay "Connected-Status" && echo -e '{"status":null}' \
	 && delay "Counter" && echo -e '{"counter":null}' \
	 && delay "Status-Lock" && echo -e '{"lock": { "name":"Table","mode":"'$LOCKMODE'"}}' \
	 && delay "Lock-Status" && echo -e '{"status":null}' \
	 && delay "Status-Unlock" && echo -e '{"unlock": { "name":"Table","mode":"'$LOCKMODE'"}}' \
	 && delay "Unlock-Status" && echo -e '{"status":null}' \
	 && delay "Done" && exit 0
) | ncat localhost 40713
