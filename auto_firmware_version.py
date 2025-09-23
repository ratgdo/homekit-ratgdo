import subprocess
import json
import os.path

Import("env")

def get_firmware_specifier_build_flag():
    f = open('./docs/manifest.json')
    data = json.load(f)
    f.close()
    build_version = data['version'].replace('v', '', 1) #remove letter v from front of version string
    build_flag = "-D AUTO_VERSION=\\\"" + build_version + "\\\""
    print ("Firmware Revision: " + build_version)
    return (build_flag)

# make sure its not already in the BUILD_FLAGS (allows for manual set in platformio.ini)
if not [bflags for bflags in env.get("BUILD_FLAGS") if "-D AUTO_VERSION=" in bflags]:
    env.Append(BUILD_FLAGS=[get_firmware_specifier_build_flag()])
