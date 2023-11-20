#! /bin/sh -

while test $# -gt 0
do
    case "$1" in
        -v) VERBOSE="-vvv"
            ;;
        upload) pio run -t upload -e ratgdo_esp8266_hV25
            ;;
        monitor) pio device monitor -e ratgdo_esp8266_hV25
            ;;
        run) pio run -e ratgdo_esp8266_hV25 $VERBOSE
            ;;
        test) pio test -e native $VERBOSE
            ;;
        *) echo "usage: x.sh [-v] <upload|monitor|run|test>"
            exit 1
            ;;
    esac
    shift
done

exit 0
