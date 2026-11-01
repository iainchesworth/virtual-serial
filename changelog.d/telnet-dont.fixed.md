A Telnet `DONT` for the com port option now resets both halves of the
negotiation, so a later `WILL` is answered with `DO` again.
