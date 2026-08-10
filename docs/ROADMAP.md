# Battery Current Monitor Roadmap

## Implemented in this bundle

- INA228 direct I2C measurement
- SSD1309 128x64 OLED display
- BLE GATT telemetry with readable descriptors
- Combined BLE telemetry stream
- Wi-Fi SoftAP
- Live web dashboard
- JSON telemetry endpoint
- Current/min/max tracking for voltage, shunt voltage, current, power and INA228 temperature
- Web min/max reset
- Physical min/max reset pushbutton
- Physical OLED on/off pushbutton
- Debounced active-low button input
- Modular application architecture

## Next planned modules

### Energy and charge
- Ah accumulation
- Wh accumulation
- session reset
- signed charge flow
- native INA228 energy/charge comparison

### Measurement quality
- zero-current calibration
- scale calibration
- selectable INA228 ADCRANGE
- configurable shunt value
- outlier filtering

### Physical shunt integration
- remove INA228 breakout R015 shunt
- connect original Watt's Up Kelvin sense points
- change nominal shunt resistance to 1 mOhm

### Temperature
- optional DS18B20 mounted on the high-current shunt
- shunt thermal warning/alarm

### Persistence
- NVS settings
- persistent calibration
- optional persistence of session energy counters

### Logging
- rolling RAM history
- graphing
- CSV export
- optional LittleFS history

### Networking
- Wi-Fi station mode
- AP + station mode
- configurable network credentials
- mDNS hostname

### Serviceability
- OTA firmware update
- diagnostic page
- firmware/build metadata
