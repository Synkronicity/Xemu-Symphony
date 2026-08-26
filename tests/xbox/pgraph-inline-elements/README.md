# PGRAPH inline packet test

`make check` validates the capacity proof used before bulk packet writes and
the exact low-then-high element order for `NV097_ARRAY_ELEMENT16`.

The runtime bulk path remains disabled for incrementing packets, single-word
packets, and active method tracing. Its telemetry reports bulk packet/word
counts plus the scalar fallback reason. Long title or composite runs are still
required for performance claims; this test is a correctness contract only.
