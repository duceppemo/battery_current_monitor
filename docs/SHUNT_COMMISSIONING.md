# Kelvin Shunt Commissioning

The planned shunt is specified as 100 A / 50 mV. Its nominal resistance is
`50 mV / 100 A = 0.5 mOhm` (`0.0005 ohm`). Do not change the active firmware
calibration until the hardware is installed and this checklist can be run.

## Low-side wiring

```text
battery/source negative -- large source-side stud [ SHUNT ] large load-side stud -- load negative
                                      |                         |
                               INA228 IN-                 INA228 IN+
                                      |
                         INA228/XIAO ground
```

Use the two small screws exclusively for INA228 Kelvin sense wiring. Put the
INA228/XIAO ground on the source-side low-side terminal. Do not route load
current through either sense lead. The positive battery rail remains the INA228
bus-voltage connection.

With this orientation, the firmware's convention is preserved: current flowing
from the source through the shunt to the load reads positive (discharge).

## Bring-up checklist

1. With all power removed, verify resistance/isolation and tighten the two
   high-current connections to the shunt manufacturer's torque specification.
2. Verify the two sense leads electrically reach the correct small screws:
   `IN-` on source side, `IN+` on load side.
3. Power the monitor with no load current. Confirm bus voltage is credible,
   shunt voltage is near zero and current sign is not inverted.
4. Apply a low, known DC load first. Compare the dashboard current and shunt
   voltage with a trusted meter; check that power has the expected sign.
5. Test at several safe currents. Inspect the shunt and terminals for heating
   before raising current.
6. Run a timed, constant-current check before trusting Ah/Wh totals.
7. In the dashboard, use Guided calibration: capture zero current, capture a
   stable reference sample, enter the trusted reference current, calculate the
   gain, then explicitly save it. Retain the reference, load, ambient
   temperature and resulting settings with the hardware.

The dashboard exposes the active INA228 configuration and whether calibration
is the first-boot default or an NVS-stored profile. Its Guided calibration
controls are deliberately manual and explicit: they require a valid telemetry
sample and an explicit save, and restoring the default asks for confirmation.
