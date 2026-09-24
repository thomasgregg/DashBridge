# ADR 0001: Clean core with incremental migration

- Status: accepted
- Date: 2026-09-24

## Decision

DashBridge will use a newly designed portable domain core while retaining
proven Bluetooth implementations behind adapters during migration. The iOS
setup GATT v1 contract remains compatible.

Legacy orchestration, shared global state, generic wire messages, and relay
lifecycle rules are not architectural dependencies of the new core. They are
temporary migration inputs and must be removed domain by domain.

## Consequences

- Hardware interoperability can be validated continuously instead of being
  recreated in one untestable rewrite.
- The target architecture remains independent from the current runtime.
- Every compatibility adapter needs an explicit removal condition.
- New features must be implemented through the new core, even while legacy
  features still run beside it.
