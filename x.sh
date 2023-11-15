#! /bin/sh -

while test $# -gt 0
do
    case "$1" in
        -v) VERBOSE="-vvv"
            ;;
        upload) pio run -t upload
            ;;
        monitor) pio device monitor
            ;;
        run) pio run -e leftovers $VERBOSE
            ;;
        test) pio test -e native $VERBOSE
            ;;
        *) echo "bad option $1, either --run or --test"
            exit 1
            ;;
    esac
    shift
done

exit 0
