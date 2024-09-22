import subprocess
import json

Import("env")


# Define the paths
primary_path = '/home/runner/work/homekit-ratgdo/homekit-ratgdo/docs/manifest.json'
fallback_path = './docs/manifest.json'

def get_firmware_specifier_build_flag():
    # Try to open the primary path, if it fails, use the fallback path
    try:
        with open(primary_path) as f:
            data = json.load(f)
            print ("Primary path used")
    except FileNotFoundError:
        with open(fallback_path, 'r') as f:
            data = json.load(f)
            print ("Fallback path used")
    f.close()
    build_version = data['version'].replace('v', '', 1) #remove letter v from front of version string
    build_flag = "-D AUTO_VERSION=\\\"" + build_version + "\\\""
    print ("Firmware Revision: " + build_version)
    return (build_flag)

env.Append(
    BUILD_FLAGS=[get_firmware_specifier_build_flag()]
)
