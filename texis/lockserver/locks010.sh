#!/bin/bash


delay() {
	read
	if [ "$1" = "Unlock" ]; then
		sleep 3;
	fi
	if [ "$1" = "Done" ]; then
		sleep 3;
	fi
}

PRETTY=false
LOCKMODE=PR

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

echo -e '{"connect":{"database":"db1","pretty":' $PRETTY '}}' \
&& delay "Lock" && echo -e '{"lock": { "name":"Table","mode":"PR"}}' \
&& delay "Unlock" && echo -e '{"unlock": { "name":"Table","mode":"PR"}}' \
&& delay "Lock" && echo -e '{"lock": { "name":"Table","mode":"PR"}}' \
&& delay "Unlock" && echo -e '{"unlock": { "name":"Table","mode":"PR"}}' \
&& delay "Lock" && echo -e '{"lock": { "name":"Table","mode":"PR"}}' \
&& delay "Unlock" && echo -e '{"unlock": { "name":"Table","mode":"PR"}}' \
&& delay "Lock" && echo -e '{"lock": { "name":"Table","mode":"PW"}}' \
&& delay "Status" && echo -e '{"status":null}' \
&& delay "Unlock" && echo -e '{"unlock": { "name":"Table","mode":"PW"}}' \
&& delay "Lock" && echo -e '{"lock": { "name":"Table","mode":"PR"}}' \
&& delay "Unlock" && echo -e '{"unlock": { "name":"Table","mode":"PR"}}' \
&& delay "Lock" && echo -e '{"lock": { "name":"Table","mode":"PR"}}' \
&& delay "Unlock" && echo -e '{"unlock": { "name":"Table","mode":"PR"}}' \
&& delay "Lock" && echo -e '{"lock": { "name":"Table","mode":"PR"}}' \
&& delay "Unlock" && echo -e '{"unlock": { "name":"Table","mode":"PR"}}' \
&& delay "Lock" && echo -e '{"lock": { "name":"Table","mode":"PR"}}' \
&& delay "Unlock" && echo -e '{"unlock": { "name":"Table","mode":"PR"}}' \
&& delay "Status" && echo -e '{"status":null}' \
&& delay "Done" && exit 0
