import subprocess
import json
import os.path

Import("env")

def get_firmware_specifier_build_flag(tag = ""):
    f = open('./docs/manifest.json')
    data = json.load(f)
    f.close()
    build_version = data['version'].replace('v', '', 1) #remove letter v from front of version string
    build_flag = "-D AUTO_VERSION=\\\"" + build_version + tag +"\\\""
    print ("Firmware Version: " + build_version + tag)
    return (build_flag)


# look for "-D VERSION_TAG=" in build flags
result = next((bflags for bflags in env.get("BUILD_FLAGS") if "-D VERSION_TAG=" in bflags), None)
verTag = ""
if result:
    # create version tag "-xxx"
    verTag = "-" + result.split("=")[1].strip('"')

# set AUTO_VERSION build flag with optional version tag appended
env.Append(BUILD_FLAGS=[get_firmware_specifier_build_flag(verTag)])
