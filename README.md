# espSwitcher
ESP32 443MHz relay controller with display and weather info
Configured to use KlikAanKlikUit relays but could be configured to use different ones

Uses the following libraries:
- ArduinoJSON
- littlevgl
- RemoteTransmitter
- TFT_eSPI
- WiFiManager(-dev)

A version of these are included in this repo, except ArduinoJSON.

Some of it could be written a lot better but my goal was to minimize memory-usage (littlevgl uses quite a lot).
