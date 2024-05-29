#!/usr/bin/env python3
import serial
import sys
import argparse
from enum import Enum
   
improv_header="IMPROV"
improv_version=1

class improv_type(Enum):
    curr_state=1
    error_state=2
    rpc_cmd=3
    rpc_result=4

class rpc_cmd(Enum):
    wifi_cmd_id=1
    current_state_cmd_id=2
    device_info_cmd_id=3
    scan_cmd_id=4

def send_improv_cmd(ser, cmd):
    improv_cmd=list(improv_header)
    improv_cmd.append(improv_version)
    improv_cmd.append(improv_type.rpc_cmd.value)
    improv_cmd.append(len(cmd))
    improv_cmd += cmd
    improv_cmd.append(0)

    tx_cmd=[]

    cksum=0
    for i in improv_cmd:
        if isinstance(i, int):
            cksum += i
            tx_cmd.append(i)
        else:
            cksum += ord(i)
            tx_cmd.append(ord(i))

    tx_cmd[-1] = cksum & 0xff
    #print(bytearray(tx_cmd))
    ser.write(bytearray(tx_cmd))
    ser.write(bytearray([ord('\n')]))
    ser.flush()

def set_wifi(ser, ssid, passwd):
    wifi_cmd=[rpc_cmd.wifi_cmd_id.value]
    wifi_cmd.append(len(ssid)+len(passwd)+2)
    wifi_cmd.append(len(ssid))
    wifi_cmd += list(ssid)
    wifi_cmd.append(len(passwd))
    wifi_cmd += list(passwd)

    send_improv_cmd(ser, wifi_cmd)
    monitor(ser)

def scan_wifi(ser):
    wifi_cmd=[rpc_cmd.scan_cmd_id.value, 0]

    send_improv_cmd(ser, wifi_cmd)
    monitor(ser)

def dev_info(ser):
    wifi_cmd=[rpc_cmd.device_info_cmd_id.value, 0]

    send_improv_cmd(ser, wifi_cmd)
    monitor(ser)

def dev_state(ser):
    wifi_cmd=[rpc_cmd.current_state_cmd_id.value, 0]

    send_improv_cmd(ser, wifi_cmd)
    monitor(ser)

def monitor(ser):
    class states(Enum):
        gethdr=0
        getver=1
        gettype=2
        getlen=3
        getdata=4
        getrpcresp=5
        getdatlen=6
        getstrlen=7
        getrpcdat=8 
        
    state=states.gethdr

    imp_type=0
    rx_header=""
    while True:
        bs = ser.read(1)
        #print(bs, end="")
        ch=int.from_bytes(bs, "big")
       
        if state == states.gethdr:
            try:
                rx_header += chr(ch)
                if len(rx_header) > len(improv_header):
                    rx_header = rx_header[1:]
                if rx_header == improv_header:
                    state=states.getver
                    rx_header=""
            except:
                rx_header = ""

        elif state == states.getver:
            if ch==improv_version:
                state=states.gettype
            else:
                state=states.gethdr

        elif state == states.gettype:
            #print("type", ch)
            imp_type=ch
            state=states.getlen

        elif state == states.getlen:
            #print("len", ch)
            cmdlen=ch
            if cmdlen > 0:
                if imp_type == improv_type.rpc_result.value:
                    state=states.getrpcresp
                else:
                    state=states.getdata
            else:
                state=states.gethdr

        elif state == states.getrpcresp:
            #print("response to command", ch)
            resp_cmd=ch
            state=state.getdatlen

        elif state == states.getdatlen:
            #print("datlen", ch)
            datlen=ch
            if datlen == 0:
                return
            state=state.getstrlen

        elif state == states.getstrlen:
            #print("strlen", ch)
            datlen -= 1
            strlen=ch
            state=state.getrpcdat

        elif state == states.getrpcdat:
            print(chr(ch), end="")
            strlen -= 1
            datlen -= 1
            if strlen == 0:
                if datlen <= 0:
                    print()
                    if resp_cmd == rpc_cmd.scan_cmd_id.value:
                        state=states.gethdr
                    else:
                        return
                else:
                    print(" ", end="")
                    state=states.getstrlen
                    sys.stdout.flush()

        elif state == states.getdata:
            if imp_type == improv_type.curr_state.value:
                if (ch == 0):
                    print("WiFi stopped")
                    return
                elif (ch == 1):
                    print("Wifi awaiting authorization")
                elif (ch == 2):
                    print("Wifi authorized")
                elif (ch == 3):
                    print("Wifi provisioning")
                elif (ch == 4):
                    print("Wifi provisioned")
                state=states.gethdr
            elif imp_type == improv_type.error_state.value:
                if (ch == 0):
                    print("No Error")
                elif (ch == 1):
                    print("Invalid RPC packet")
                elif (ch == 2):
                    print("Unknown RPC command")
                elif (ch == 3):
                    print("Unable to connect")
                elif (ch == 4):
                    print("Unknown Error")
                return
                
            else:
                state=states.gethdr
        
        #else:
            #try:
            #    print(bs.decode(), end="")
            #except:
            #    print(bs, end="")
        #sys.stdout.flush()    


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-d', dest='dev',    help="USB Device")
    parser.add_argument('-s', dest='ssid',   help="Wifi SSID")
    parser.add_argument('-p', dest='passwd', help="Wifi Password")
    parser.add_argument('-S', dest='scan', default=False, action=argparse.BooleanOptionalAction, help="Scan Wifi Networks")
    parser.add_argument('-i', dest='info', default=False, action=argparse.BooleanOptionalAction, help="Get device information")
    parser.add_argument('-g', dest='getstate', default=False, action=argparse.BooleanOptionalAction, help="Get current state")
    args = parser.parse_args()

    if (not args.dev):
        print("Must select serial device")
        exit()

    ser = serial.Serial(args.dev, 115200)
    if (not ser):
        print("Invalid serial device")
        exit()

    if (args.scan):
        scan_wifi(ser)

    if (args.info):
        dev_info(ser)

    if (args.getstate):
        dev_state(ser)

    if (args.ssid and args.passwd):
        set_wifi(ser, args.ssid, args.passwd)

