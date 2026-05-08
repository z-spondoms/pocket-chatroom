# Pocket Chatroom

An ultra-light, no-dependency offline chatroom for the ESP8266. The device starts its own access point, hosts a captive portal, stores the latest chat messages, and serves a web UI directly from LittleFS.

## About

Pocket Chatroom is inspired by [leviv/pocket-wifi](https://github.com/leviv/pocket-wifi), which showed how lightweight and charming an ESP8266-hosted offline web experience can be.

This project adapts that idea into a local captive-portal chatroom with:

- captive portal redirect via `DNSServer`
- `POST /send` and `GET /messages` for local chat messages
- compact client-side Unix timestamps for real date and time display
- `POST /admin/reset` with a minimal password check
- retention limited to the last 100 messages to protect ESP8266 resources
- basic IP-based spam protection on `POST /send`
- a vanilla JavaScript frontend with local username persistence

## Hardware / Software

- [ESP8266 board such as a Wemos D1 mini](https://www.amazon.de/dp/B0DCBVHBB3)
- [Arduino IDE](https://www.arduino.cc/en/software)
- [ESP8266 board package](https://arduino.esp8266.com/stable/package_esp8266com_index.json)
- [LittleFS upload plugin for Arduino IDE](https://github.com/earlephilhower/arduino-littlefs-upload)

## Setup

### 1. Install Arduino IDE

Download and install the Arduino IDE:

[Arduino IDE](https://www.arduino.cc/en/software)

---

### 2. Add ESP8266 board support

1. Open:

`Arduino IDE → Preferences… → Additional Boards Manager URLs`

2. Add the following URL:

```text
https://arduino.esp8266.com/stable/package_esp8266com_index.json
```

3. Click **OK**.

---

### 3. Install the ESP8266 board package

1. Open **Boards Manager** (⌘ + ⇧ + B)
2. Search for: `esp8266`

3. Install:

```text
esp8266 by ESP8266 Community
```

---

### 4. Install the LittleFS uploader plugin

1. Download the latest `.vsix` release of:

[arduino-littlefs-upload releases](https://github.com/earlephilhower/arduino-littlefs-upload/releases/latest)

2. Create the plugin directory if it does not already exist:

```bash
mkdir -p ~/.arduinoIDE/plugins
```

3. Copy the downloaded `.vsix` file into:

```text
~/.arduinoIDE/plugins/
```

4. Then completely restart the Arduino IDE.

---

### 5. Clone this repository

```bash
git clone https://github.com/z-spondoms/pocket-chatroom.git
```

Open:

```text
pocket-chatroom.ino
```

in the Arduino IDE.

---

### 6. Select the correct board

Go to:

`Arduino IDE → Tools → Board → esp8266`

Select:

```text
LOLIN(WEMOS) D1 R2 & mini
```

---

### 7. Connect the board

Connect your board to your computer with a USB cable.

Then select the correct serial port:

`Arduino IDE → Tools → Port → /dev/cu.usbserial-XXX`

---

### 8. Upload the LittleFS filesystem

1. Press: **⌘ + ⇧ + P**

2. Search for: `Upload LittleFS to Pico/ESP8266/ESP32`

3. Run the command and wait until the upload is finished.

---

### 9. Upload the sketch

Upload the sketch normally using the Arduino IDE upload button (arrow button).

---

### 10. Connect to Pocket Chatroom

Connect your phone or laptop to the Wi-Fi network:

```text
Pocket Chatroom
```

The captive portal should open automatically.

## Admin Reset

Reset the chat log with a POST request:

```bash
curl -X POST http://192.168.4.1/admin/reset \
  -d "password=pocket-reset"
```