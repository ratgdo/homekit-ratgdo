#!/usr/bin/env sh
UUID=$(uuidgen)
URL=$(curl -s "http://${1}/rest/events/subscribe?id=${UUID}&log=1&heartbeat=0")
curl -s "http://${1}/showlog"
curl -s -N "http://${1}${URL}?id=${UUID}" | sed -n -u '/^event: logger$/{n;s/^data: //p;}'
exit 0
