# Setup GATT v1

`contract.json` is the frozen compatibility boundary between released iOS apps
and Board A. Existing fields, UUIDs, command meanings, permissions, names, and
status bits must not be changed in place.

Additive setup functionality must use a separately versioned contract. The
architecture check compares this contract with both the Swift client and the
firmware adapter.
