# espSwitcher
ESP32 443MHz relay controller with display, weather info and alarm clock

Configured to use KlikAanKlikUit relays but could be configured to use different ones

Uses the following libraries:
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- [LittlevGL](https://github.com/littlevgl/lv_arduino)
- [RemoteTransmitter](https://bitbucket.org/fuzzillogic/433mhzforarduino/wiki/Home)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
- [WiFiManager](https://github.com/tzapu/WiFiManager)(-dev)

A version of these are included in this repo, except ArduinoJSON.

Some of it could be written a lot better but my goal was to minimize memory-usage (littlevgl uses quite a lot).
