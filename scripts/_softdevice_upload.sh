#!/bin/bash

SOFTDEVICE_DIR=${1:? "$0 requires \"softdevice bin directory\" as first argument"}

SERIAL_NUM=$2

path="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $path/_utils.sh
source $path/_config.sh

SCRIPT_DIR=$path/jlink
TEMP_DIR=$path/tmp
mkdir -p $TEMP_DIR

#DEVICE=nrf51822
DEVICE=nRF52832_xxAA

if [ ! -e ${SOFTDEVICE_DIR} ]; then
	cs_err "Error: ${SOFTDEVICE_DIR} does not exist..."
	exit 1
fi

sed "s|@SOFTDEVICE_DIR@|$SOFTDEVICE_DIR|" $SCRIPT_DIR/softdevice.script > $TEMP_DIR/softdevice.script

if [ $SOFTDEVICE_NO_SEPARATE_UICR_SECTION == 1 ]; then
	cs_log "No UICR section, so remove line to flash it"
	sed -i '/uicr/d' $TEMP_DIR/softdevice.script
fi

if [ -z $SERIAL_NUM ]; then
	cs_log "$JLINK -Device $DEVICE -If SWD $TEMP_DIR/softdevice.script -ExitonError 1"
	$JLINK -Device $DEVICE -If SWD $TEMP_DIR/softdevice.script -ExitonError 1
else
	cs_log "$JLINK -Device $DEVICE -SelectEmuBySN $SERIAL_NUM -If SWD $TEMP_DIR/softdevice.script -ExitonError 1"
	$JLINK -Device $DEVICE -SelectEmuBySN $SERIAL_NUM -If SWD $TEMP_DIR/softdevice.script -ExitonError 1
fi
