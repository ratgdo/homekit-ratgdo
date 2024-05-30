#!/usr/bin/env sh
IP=${1}
FILE=${2}
if [ $(which md5sum) ]; then
    # Linux
    MD5=$(md5sum "${FILE}" | cut -d' ' -f1)
    SIZE=$(stat -c%s "${FILE}")
elif [ $(which md5) ]; then
    # macOS
    MD5=$(md5 -q "${FILE}")
    SIZE=$(stat -f%z "${FILE}")
fi
if [ -z "${MD5}" ]; then
    echo "Unable to calculate MD5 hash for file ${$FILE}, terminating"
    exit 1
fi
echo "Calculated MD5 hash for file ${FILE}: ${MD5}, Size: ${SIZE} bytes"
JSON="{\"md5\":\"${MD5}\",\"size\":${SIZE},\"uuid\":\"n/a\"}"
#echo "Inform host of file size and MD5 hash..."
#curl -s -X POST -F "updateUnderway=${JSON}" "http://${IP}/setgdo" > /dev/null
echo "Verifying file..."
#curl -s -F "content=@${FILE}" "http://${IP}/verify"
curl -s -F "content=@${FILE}" "http://${IP}/update?action=verify&size=${SIZE}&md5=${MD5}"
echo "Verify complete"
exit 0
