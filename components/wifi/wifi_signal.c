#include "wifi_signal.h"

int wifi_signal_bars(int rssi_dbm) {
    if (rssi_dbm >= -55) return 4;
    if (rssi_dbm >= -65) return 3;
    if (rssi_dbm >= -75) return 2;
    return 1;
}
