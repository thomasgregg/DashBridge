# Portable core

Domain state machines, value types, commands, events, services, and outbound
port interfaces live here. Code in this directory must compile without
ESP-IDF, FreeRTOS, a Bluetooth stack, or the legacy runtime.

Domain folders will be introduced one at a time with host tests before they are
wired to hardware adapters.
