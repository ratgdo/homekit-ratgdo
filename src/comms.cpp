#include "SoftwareSerial.h"
#include "ratgdo.h"

SoftwareSerial sw_serial;
extern struct GarageDoor garage_door;

/********************************** MAIN LOOP CODE *****************************************/

void setup_comms() {
    sw_serial.begin(9600, SWSERIAL_8N1, UART_RX_PIN, UART_TX_PIN, true);
    sw_serial.enableIntTx(false);
    sw_serial.enableAutoBaud(true); // found in ratgdo/espsoftwareserial branch autobaud
}

void comms_loop() {
    // TODO read from sw_serial and notify the characteristic
}

void open_door() {
    Serial.print("TODO: open door\n");
}

void close_door() {
    Serial.print("TODO: close door\n");
}

// TODO update garage_door based on serial comms
