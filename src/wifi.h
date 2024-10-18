// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef WIFI_INFO_H_
#define WIFI_INFO_H_

#include <set>
#include <string>

void improv_loop();

void wifi_connect();

void wifi_scan();

bool connect_wifi(std::string, std::string);

struct wifi_nets {
    String  SSID;
    int32_t RSSI;
};
extern std::set<struct wifi_nets> wifiNets;

#endif /* WIFI_INFO_H_ */
