#pragma once

#include <cstdint>

void statusScreenPortal();
void statusScreenConnectFailed();
void statusScreenWifiReset();

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();

/** Shown briefly after joining Wi-Fi: where to find the web page. */
void statusScreenConnected(const char* ssid, const char* ip, uint16_t http_port);
