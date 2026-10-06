# ESP8266 RangeExtender + EvilTwin

A dual-mode **ESP8266 NodeMCU** firmware project that combines a Wi-Fi range extender with NAPT support and a security-testing EvilTwin/DeAuth/captive-portal mode behind a single web-based mode selector.

> **Project:** ESP8266 Firmware — Selection UI  
> **Author:** SA SUJON  
> **Target:** NodeMCU 1.0 (ESP-12E) / ESP8266  
> **Flash:** 4 MB  
> **Filesystem:** 2 MB  
> **OTA partition:** ~1019 KB

---

## ⚠️ Security & Legal Warning

This project contains functionality that can be used for **Wi-Fi security testing**, including EvilTwin/captive-portal and deauthentication-related functionality.

Use it **only on networks, devices, and wireless environments that you own or have explicit permission to test**.

Unauthorized interception of credentials, impersonation of access points, deauthentication of users, or disruption of wireless networks may be illegal and can cause real-world service disruption.

**Do not use this firmware against public, residential, enterprise, school, workplace, or other third-party networks without explicit authorization.**

The author is not responsible for damage, disruption, data loss, credential exposure, or legal consequences resulting from misuse.

For responsible research, use an isolated lab with test access points and test client devices.

---

## Features

### 1. RangeExtender Mode

A Wi-Fi repeater/router mode designed to extend an existing wireless network through the ESP8266.

Features include:

- Wi-Fi station connection
- Configurable ESP8266 access point
- **NAPT** for client internet access
- Network scanning
- Connection/disconnection controls
- Configuration dashboard
- Status information
- Connected-client count
- Signal-quality information
- Encryption-type detection
- Persistent configuration storage
- Reconnection handling
- Web-based reboot/reset controls
- Arduino OTA support

### 2. EvilTwin Security-Test Mode

A separate security-testing mode containing:

- Wi-Fi network discovery/scanning
- Target-network selection
- EvilTwin-style access-point functionality
- Captive DNS/web interface
- Deauthentication-related testing functionality
- Test captive-portal workflow
- Password-capture testing workflow
- Web-based administration interface
- Reset back to the main mode selector

**This mode is intended strictly for authorized security research and controlled laboratory environments.**

### 3. Boot Mode Selector

On startup, the ESP8266 provides a web-based selection interface.

The selector provides:

- **EvilTwin**
- **RangeExtender**
- Firmware OTA upload
- Mode persistence
- Reset/reboot workflow

The selected mode is stored in EEPROM so the device can remember its operating mode.

### 4. OTA Firmware Updates

The project includes web-based firmware upload functionality and Arduino OTA support.

This makes it possible to update firmware without repeatedly connecting the board through USB.

> **Security note:** OTA should only be exposed inside a trusted, controlled environment. The firmware currently contains default credentials/settings that should be changed before any real deployment.

---

## Hardware

### Recommended Board

- ESP8266 NodeMCU 1.0
- ESP-12E based board
- 4 MB flash recommended

### Current Target

```text
Board:       NodeMCU 1.0 (ESP-12E)
MCU:         ESP8266
Flash:       4 MB
Filesystem:  2 MB
OTA Space:   ~1019 KB
```

---

## Software Requirements

You will need:

- Arduino IDE or another ESP8266-compatible build environment
- ESP8266 Arduino Core
- A compatible NodeMCU/ESP8266 board configuration
- USB data cable for initial flashing

### Libraries / Components Used

The firmware uses ESP8266/Arduino components including:

```cpp
Arduino.h
ArduinoOTA.h
DNSServer.h
EEPROM.h
ESP8266WebServer.h
ESP8266WiFi.h
lwip/dns.h
lwip/napt.h
user_interface.h
```

Most of these are provided by the ESP8266 Arduino core or the ESP8266 SDK environment.

---

## Initial Setup

1. Open the `.ino` file in Arduino IDE.
2. Select the appropriate ESP8266 NodeMCU board.
3. Select the correct COM port.
4. Configure the required flash layout.
5. Compile the firmware.
6. Flash it to the ESP8266 over USB.
7. Reboot the board.
8. Connect to the firmware's initial access point.
9. Open:

```text
http://192.168.4.1
```

10. Select the required operating mode.

### Default Selector Access Point

The current source defines:

```text
SSID:      NodeMCU
Password:  12345678
IP:        192.168.4.1
```

**Change/remove default credentials before deploying the firmware outside an isolated test environment.**

---

## RangeExtender Mode

The RangeExtender mode uses the ESP8266 as a small Wi-Fi router/repeater.

At a high level:

```text
             Existing Wi-Fi
                  │
                  │ STA
                  ▼
          ┌───────────────┐
          │    ESP8266    │
          │ RangeExtender │
          │     + NAPT    │
          └───────┬───────┘
                  │ AP
          ┌───────┴────────┐
          │                │
          ▼                ▼
      Client #1         Client #2
```

The firmware initializes NAPT when the upstream connection is available, allowing connected clients to use the ESP8266 access point for network access.

The web dashboard exposes functionality for:

- AP configuration
- Upstream Wi-Fi configuration
- Network scanning
- Connection management
- Status monitoring
- Client information
- Reboot/reset operations

---

## EvilTwin Mode

The EvilTwin mode is intended for **authorized wireless-security testing**.

It includes functionality associated with:

- Wireless network discovery
- Target selection
- AP impersonation testing
- Captive DNS
- Captive web pages
- Deauthentication testing
- Credential-capture simulation/testing

Because these features can directly affect nearby wireless networks and users, they should be operated only inside an isolated test environment or against systems for which you have explicit authorization.

### Recommended Lab Setup

Use:

```text
[Test Access Point]
        │
        ├── Test Client
        │
        └── ESP8266 Test Device
```

Avoid testing against networks belonging to other people or organizations.

---

## Web Interfaces

The firmware provides several HTTP interfaces.

### Mode Selector

```text
http://192.168.4.1/
```

### RangeExtender

The RangeExtender dashboard exposes endpoints for status, scanning, configuration, client information, rebooting, and mode reset.

Representative endpoints include:

```text
/api/status
/api/scan
/api/connect
/api/disconnect
/api/settings
/api/clients
/api/reboot
/api/resetmode
```

### EvilTwin

The EvilTwin mode provides web pages for its testing workflow and administration interface.

Representative routes include:

```text
/
/result
/admin
/reset-mode
```

These routes are implementation details of the current firmware and may change in future releases.

---

## Persistent Configuration

The firmware uses EEPROM for persistent data.

The source reserves:

```text
EEPROM Size: 512 bytes
Mode Address: 511
```

The active mode is stored so the ESP8266 can restore the selected operating mode after reboot.

The RangeExtender configuration is also persisted within the allocated EEPROM region.

---

## OTA Updates

There are two OTA-related mechanisms in the firmware:

1. Web-based `.bin` firmware upload from the selector/dashboard.
2. Arduino OTA support in RangeExtender mode.

### Web OTA

The selector exposes a firmware upload interface that accepts a compiled `.bin` file.

The firmware then:

```text
Receive firmware
      ↓
Validate/update flash
      ↓
Finish update
      ↓
Restart ESP8266
```

### Important

OTA is a powerful administrative interface. Do not expose it to an untrusted network.

Before deployment, review:

- OTA passwords
- Web endpoints
- AP credentials
- Firmware-update access
- Network isolation
- Default configuration values

---

## Build Notes

The RangeExtender mode relies on ESP8266 NAPT support:

```cpp
#include <lwip/napt.h>
```

The firmware is therefore dependent on an ESP8266 core/build configuration that provides the required NAPT functionality.

The source also references:

```text
lwIP
ESP8266 SDK user_interface
```

If compilation fails around NAPT or low-level Wi-Fi functions, verify the installed ESP8266 Arduino Core version and board configuration.

---

## Project Structure

A typical repository can be organized as:

```text
RangeExtender-EvilTwin/
│
├── ESP8266.ino
├── README.md
```

If you publish compiled firmware, clearly label each binary with:

- Version
- Board target
- Flash layout
- Build date
- Git commit/tag

Example:

```text
EvilXtender-v1.0.0-NodeMCU-4MB.bin
```

---

## Troubleshooting

### The selector page does not open

Check:

- ESP8266 is powered correctly.
- Your device is connected to the ESP8266 AP.
- The board received an IP address.
- You are opening `http://192.168.4.1`.
- No browser VPN/proxy is interfering with local traffic.

### RangeExtender cannot connect upstream

Check:

- SSID and password.
- Upstream signal strength.
- ESP8266 channel compatibility.
- NAPT support in the selected ESP8266 build.
- Serial Monitor output.

### NAPT does not start

Verify:

- The ESP8266 core supports NAPT.
- The upstream STA connection is established.
- The correct lwIP variant is selected.
- Sufficient heap memory is available.

### OTA update fails

Check:

- Firmware is compiled for the same board.
- Flash size/layout is compatible.
- The binary fits within the OTA partition.
- The ESP8266 has sufficient free space.
- The device is not losing power during the update.

---

## Serial Monitor

The firmware prints useful diagnostic information through `Serial`.

For debugging, monitor messages related to:

```text
[MODE]
[SELECTOR]
[AP]
[STA]
[NAPT]
[DNS]
[WEB]
[OTA]
[READY]
```

These messages can help identify boot, Wi-Fi, NAPT, web-server, and OTA problems.

---

## Security Hardening Checklist

Before using the firmware outside a controlled lab:

- [ ] Replace default AP credentials.
- [ ] Replace default OTA credentials.
- [ ] Do not expose the web interface to an untrusted network.
- [ ] Disable/remove security-testing functionality if it is not required.
- [ ] Do not deploy credential-capture functionality on real users.
- [ ] Use an isolated test network for EvilTwin testing.
- [ ] Keep firmware binaries and source under version control.
- [ ] Review all HTTP endpoints before deployment.
- [ ] Keep the ESP8266 physically secure during testing.

---

## Responsible Use

This project is suitable for learning about:

- ESP8266 networking
- Wi-Fi AP/STA operation
- NAPT
- DNS handling
- Embedded web interfaces
- OTA firmware updates
- Wi-Fi security concepts
- Captive portals
- Wireless-security lab environments

The security-testing components should be treated as **research tooling**, not as a mechanism for accessing networks or credentials without authorization.

---

## Roadmap

Potential future improvements:

- [ ] Replace hard-coded credentials with first-boot configuration.
- [ ] Add authentication to web administration.
- [ ] Add CSRF protection to administrative endpoints.
- [ ] Add safer OTA authentication and firmware validation.
- [ ] Add configuration backup/restore.
- [ ] Add version information to the dashboard.
- [ ] Add build/version metadata to firmware.
- [ ] Improve error reporting and recovery.
- [ ] Add stronger separation between normal networking and security-testing functionality.
- [ ] Add a dedicated laboratory/test-mode indicator.

---

## Disclaimer

This software is provided for educational, development, and authorized security-testing purposes.

The project author does not authorize or encourage:

- Unauthorized Wi-Fi access
- Credential theft
- Network disruption
- Privacy violations
- Impersonation of third-party networks
- Attacks against systems without permission

**You are solely responsible for complying with applicable laws, regulations, policies, and obtaining appropriate authorization before testing.**

---

## Credits

**SA SUJON**

ESP8266 dual-mode firmware project combining:

- RangeExtender / Wi-Fi repeater functionality
- NAPT networking
- Web-based configuration
- OTA firmware updates
- Wireless-security testing functionality

---
