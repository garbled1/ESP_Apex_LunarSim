# ESP_Apex_LunarSim
ESP8266 Lunar Simulator for Neptune Apex

## Why?

The Neptune Apex is an incredible controller. The profiles, the management,
Supper happy with it, except..

It has no way to reasonably emulate moonlight properly.

You can set lights to come on at the right moonrise/set, it has that code, and
that is pretty awesome.  But you can't set the illumination.  You can only do
so with a Lunar Sim module, which gives you 5 dinky LED's.  This will not cut
it at all for an 800g aquarium.

## What?

This hack is simple-ish.  We poll the Apex looking for a VDM port.  We read that
port to get the current ramp value.  Then we poll NTP, get the current time,
calculate the phase of the moon, and use that to set a digital potentiometer
to the right resistance to allow control of a MeanWell LED Powersupply.

## How?

You need the following bits:

1) A Wemos D1 Mini
2) A 128x64 OLED (i2c)
3) A DFRobot Digital potentiometer https://www.dfrobot.com/product-1650.html
4) A breadboard
5) Optional - 3d printer to make the case
6) A real VDM module, with a spare port. (You can use the useless serial port)
7) LED's driven by a MeanWell 3-in-1 dimmer style driver, or any driver that
   uses 10-100k Ohm resistance for dimming.

Setup a lighting schedule for your moonlight, and operate it with a normal
outlet on an EnergyBar.  This controls on/off.

Setup a ramp profile in the unused Serial port of a VDM.  I used BluLED_4_5, yours
will differ.  Set a reasonable ramp-up, and ramp-down program at the start and
end of your moonphase (use the If Moon function!)

Build the circuit.

Wire the dimmer control of the MeanWell into the circuit, load the program, set
the wifi and options in the web UI.

Enjoy.

## When?

Now, well, once I put up a circuit diagram.  I'm still testing mine.
