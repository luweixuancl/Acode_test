# ESP32-C3 firmware (phone flash)

| File | Flash address |
|------|---------------|
| `firmware_merged_0x0.bin` | `0x0` (all-in-one, **first install**) |
| `esp32c3-firmware-phase-a.zip` | bootloader@0x0, partitions@0x8000, firmware@0x10000 |
| `firmware.bin` (inside zip) | `0x10000` (**updates — preferred**) |

Chip: ESP32-C3. Do not connect phone WiFi to SoftAP while flashing (keeps cellular 5G).

## Keep WiFi credentials across updates

NVS (WiFi SSID/password) lives **outside** the app image. If the flasher **erases whole flash**, credentials are wiped and the device opens SoftAP.

**Recommended for updates:**

1. Flash only `firmware.bin` at **`0x10000`**
2. Turn **OFF** “Erase flash” / “全片擦除” in the Android app
3. Do **not** rewrite bootloader/partitions unless the partition table changed

Use `firmware_merged_0x0.bin` only for blank boards or when you intentionally want a clean NVS.
