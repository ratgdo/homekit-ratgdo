// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

#ifndef WIFI_INFO_H_
#define WIFI_INFO_H_

#include <set>
#include <string>

void improv_loop();

void wifi_connect();

bool connect_wifi(std::string, std::string);

extern std::set<String> wifiNets;

#endif /* WIFI_INFO_H_ */
