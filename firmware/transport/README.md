# Transport

Transport owns the bounded priority queue that carries typed DashLink control
packets over the control UART. DashLink owns framing, validation, and checksum
recovery; dedicated call/music codecs use the separate media UART. Transport
does not own calls, messages, music, or contact behavior.
