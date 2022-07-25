# MidiEvil

Midi Loop Switcher built on an Arduino Nano 

MidiEvil is a dirt simple midi-controlled loop and amp switch. It’s been designed to be easy to configure and can be used with or without a midi controller.

•	Switchable Input Buffer (Based on a Klon TL072)

•	4 True Bypass Loops using good quality NEC Signal Relays (Switches 1 – 4)

•	1 Normally Open Latching Relay (Switch 5)

•	2 Footswitch Modes (Direct or Program)

•	Program Mode allows you to recall 10 presets across 2 banks

•	Direct Mode allows for control over each loop and amp switch

•	User assignable CC Numbers to each Switch

•	Controller values from 0-63 will switch off the output, values from 64-127 will switch it on

•	Can store up to 128 presets (not that you’ll need that many)

•	Ability to enable and disable CC switch control and PC recall functionality if required

•	1 Midi in Connector (5 Pin Din)

•	3 Midi Through Connectors to connect other devices (5 Pin Din)

src code contains the sketch and libraries required to compile the code

build document contains bill of materials, scheamtic, drill template and note

user manual describes operation and configutataion fo the device

updater.exe packages the current .hex, avrdude and a batch script to make uploading firmware easy if you aren't a programmer
