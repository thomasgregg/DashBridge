# Portable core

Domain state machines, value types, commands, events, services, and outbound
port interfaces live here. Code in this directory must compile without
ESP-IDF, FreeRTOS, a Bluetooth stack, or the platform runtime.

Every domain rule has host tests independent of its hardware adapters.
