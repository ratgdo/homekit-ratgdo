import subprocess
import json

Import("env")

def get_firmware_specifier_build_flag():
    f = open('./docs/manifest.json')
    data = json.load(f)
    f.close()
    build_version = data['version'].replace('v', '', 1) #remove letter v from front of version string
    build_flag = "-D AUTO_VERSION=\\\"" + build_version + "\\\""
    print ("Firmware Revision: " + build_version)
    return (build_flag)

env.Append(
    BUILD_FLAGS=[get_firmware_specifier_build_flag()]
)
