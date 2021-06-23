# Aranet4Linux

Unofficial Linux client for the [Aranet4 Wireless Air Quality Monitor](https://aranet4.com).

## Building

```
./configure
make
```

or for [NixOS](https://nixos.org) users:

```
nix-build
```

## Usage

Your device must be already paired to your computer via an external bluetooth manager.

Assuming your Aranet4 is paired, the program will connect to the Aranet4 device with the bluetooth
address from the `ARANET4_ADDRESS` environment variable, read the current values from
the sensor, and print the values to stdout. ie.

```
$ export ARANET4_ADDRESS="XX:XX:XX:XX:XX:XX"
$ aranet4
Aranet4
CO2: 689
Temperature: 23.4C
Pressure: 1008hPa
Humidity: 44%
Battery: 98%
```

(It's recommended to set `ARANET4_ADDRESS` in your profile)

TODO: Add support for historical data on device

## Thanks
Thanks to [Aranet4-Python](https://github.com/Anrijs/Aranet4-Python) for documenting the GATT Characteristics
and output formats.
