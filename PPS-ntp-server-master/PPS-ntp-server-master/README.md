# ESP32 NTP Server

NTP time server implementation using NEO-6MV2 GPS module as a reference clock. Originally based on https://github.com/sigorilla/arduino-ntp-server, with heavy modifications to work on espressif ESP32 platform, and to use the global positioning system (GPS) instead of the russian GLONASS. The GPS is re-configured for 115200 baud and 1 PPS with 100ms pulse duration at run-time. The rewrite also incorporates a forked version of the well-know "time.h" arduino library modified to provide microsecond resolution and the ability to use a PPS clock input signal. Specifically, the code is designed to run on an "olimex ESP32-GATEWAY" development board having an on-board RJ45 100MBit ethernet jack, but can easily be adapted to work with other ESP32 boards using external ethernet connectivity (or WiFi connection). Also, a cheap 128x64 0.96" Oled LCD is used for displaying basic information. Average precision was measured (by comparison to known-good time sources) with 1 ms, or 0.001 seconds (the smallest reliable time unit this GPS module can deliver) offset compared to e.g. the PTB NTP server [ptbtime1.ptb.de](https://www.ptb.de/cms/en/ptb/fachabteilungen/abtq/gruppe-q4/ref-q42/time-synchronization-of-computers-using-the-network-time-protocol-ntp.html), or other official time sources, provided the offset is adjusted accordingly using `#define TIME_OFFSET_USEC`. The NTP server uses DHCP for IPv4 address retrieval, and replies to "simple mode" NTP queries by sending packets with "stratum 1" flag (testable with w32time.exe on windows: `w32tm.exe /stripchart /computer:<IP-of-NTP-server> /period:1 /samples:3 /packetinfo`, or with ntpdate on linux: `ntpdate -q <your-NTP-server-IP>`). GPS reception is good even indoors when using an external GPS antenna instead of the small ceramic antenna shipping with commonly sold GY-NEO6MV2 modules. Precision can be further improved when using one of the more  expensive "NEO7T"/"NEO8T" modules (note the "T"), which allow for fixed location precision timing modes to be used, giving a PPS pulse with superior accuracy.


## Used Hardware

* Microcontroller: `Olimex ESP32-Gateway`
* GPS time reference: `GY-NEO6MV2`
* OLED: cheap 0.96" OLED
* Antenna: active GPS antenna with 5m wire

## Wiring
```
GPS <-> ESP32
-----------------
GND --> GND
Tx  --> PIN10 (Rx)
Rx  --> PIN32 (Tx)
VCC --> +3.3V
LED/PPS output --> 600 ohm resistor --> PIN36  
(this needs some fine soldering with thin wire to attach PIN36 to the trace running to the GPS module LED)
```


## Links

* [u-center software](https://www.u-blox.com/en/product/u-center-windows)
* [u-blox command reference manual](https://www.u-blox.com/sites/default/files/products/documents/u-blox6-GPS-GLONASS-QZSS-V14_ReceiverDescrProtSpec_%28GPS.G6-SW-12013%29_Public.pdf)
* [NTP packet reference](https://www.cisco.com/c/en/us/about/press/internet-protocol-journal/back-issues/table-contents-58/154-ntp.html)
