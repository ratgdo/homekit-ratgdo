// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef WIFI_INFO_H_
#define WIFI_INFO_H_

#include <set>

void improv_loop();

void wifi_connect();

void wifi_scan();

bool connect_wifi(const std::string& ssid, const std::string& password, const uint8_t *bssid);
bool connect_wifi(const std::string& ssid, const std::string& password);

typedef struct
{
    String ssid;
    int32_t rssi;
    int32_t channel;
    uint8_t bssid[6];
} wifiNet_t;
extern std::multiset<wifiNet_t, bool (*)(wifiNet_t, wifiNet_t)> wifiNets;
extern station_config wifiConf;

#endif /* WIFI_INFO_H_ */
