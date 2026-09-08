# PGRAPH inline packet test

`make check` validates the capacity proof used before bulk packet writes and
the exact low-then-high element order for `NV097_ARRAY_ELEMENT16`. It also
checks pending draw-array values, exact-capacity acceptance, overflow-safe
arithmetic, and that a failed plan leaves its result untouched.

The runtime bulk path remains disabled for incrementing packets and active
method tracing. Untraced non-incrementing packets use the same bounded bulk
path at every length, avoiding an unmeasured packet-size cutoff. Diagnostic
counters are intentionally absent from the production hot path. Long title or
composite runs are still required for performance claims; this test is a
correctness contract only.

An array packet that cannot fit is logged as a guest error and discarded as a
whole dispatch batch. Non-incrementing packets consume every rejected word so
PFIFO can continue; incrementing packets consume only the current method word.
The destination arrays and pending draw-array state remain unchanged. This is
malformed-input hardening, not evidence of an ordinary retail overflow.
