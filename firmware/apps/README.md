# Board applications

`dashbridge_app` is the only composition root. The selected build configuration
chooses Board A or Board B, then the app wires the matching adapters to the
portable core and starts the platform runtime. Feature logic is not allowed here.
