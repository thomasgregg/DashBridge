# Release compatibility

Firmware and the iOS app have independent versions and releases. Compatibility
is determined by their protocol versions, not by matching product version
numbers.

| Product | Current release | Required contract |
| --- | --- | --- |
| Board A and Board B firmware | 0.5.3-alpha | Setup GATT v1 for the app; DashLink v2 between boards |
| iOS app | 1.0 (build 17) | Setup GATT v1 |

Board A and Board B are one firmware product and must be installed from the
same firmware release. The iOS app may be released on its own as long as it
continues to support Setup GATT v1.

An incompatible setup change must be introduced as a new contract beside v1.
Firmware should support both versions during the transition; the app should
detect the available version and retain its v1 path. Removing a released
contract requires an explicit compatibility decision and physical validation.
