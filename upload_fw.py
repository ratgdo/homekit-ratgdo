import requests
import os
import hashlib
import threading
from time import sleep
Import("env")

#################################################
# copy .bin to my dropbox
#################################################
def post_program_action(source, target, env):
    print(".bin file built, coping to dropbox")
    binPath = target[0].get_abspath()
    env.Execute(f"copy {binPath} \"C:\\Users\\mitchjs\\Dropbox\Public\\\"")
#
#env.AddPostAction("$BUILD_DIR/firmware.bin", post_program_action)

#################################################
# custom upload
#################################################
def print_dots(event: threading.Event):
    while not event.is_set():
        print(".", end='')
        sleep(2)

def on_upload(source, target, env):

    # get ip adddress from platformio.ini
    IP = env["UPLOAD_PORT"]

    # get the firmware.bin file with path
    binPath = str(source[0])
    #print("binPath", binPath)

    # get file size of "firmware.bin"
    fileSize = os.path.getsize(binPath)
    # compute MD5
    with open(binPath, 'rb') as f:
       fileMD5 = hashlib.md5(f.read()).hexdigest()

    print("fileSize: ", fileSize)
    print("fileMD5: ", fileMD5)

    query_params = {
        "action": "update",
        "size": fileSize,
        "md5": fileMD5
    }

    try:
        with open(binPath, 'rb') as file_to_upload:
            files_payload = {'content': file_to_upload}
            print(f"Uploading .bin file via HTTP to {IP}", end="")
            event = threading.Event()
            thread = threading.Thread(target=print_dots, args=(event,))
            thread.start()
            response = requests.post(f"http://{IP}/update", params=query_params, files=files_payload, timeout=30)
            event.set()
            print("")

        response_body = response.text
        http_status   = response.status_code
        #print(response_body)
        #print("http_status ",http_status)

        # all good?
        if (http_status == 200):
            print("Upload successful, rebooting...")
            response = requests.post(f"http://{IP}/reboot", timeout=30)
        else:
            print("Upload FAILED!")
            exit(1)
    
    except FileNotFoundError:
        print(f"Error: File not found at '{program_path}' during open attempt.")
        exit(1)
    except requests.exceptions.ConnectionError as e:
        print(f"Error: Could not connect to the server at '{IP}'. Please check the IP/port and network connection.")
        print(f"Details: {e}")
        exit(1)
    except requests.exceptions.Timeout:
        print(f"Error: The request timed out after 30 seconds.")
        exit(1)
    except requests.exceptions.RequestException as e:
        print(f"An unexpected error occurred during the HTTP request: {e}")
        exit(1)
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        exit(1)

#
if env["UPLOAD_PROTOCOL"] == "custom":
    env.Replace(UPLOADCMD=on_upload)
