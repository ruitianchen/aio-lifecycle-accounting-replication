# AIO ownership and completion

The reader harvests every accepted request before returning from a blocking read, including requests accepted before a partial submission. It checks completion status and byte counts while the buffers and reader state remain valid.

Before `Shutdown`, callers stop new use, finish data I/O, return leases, and coordinate worker and thread-local cleanup. The runtime lives until its holders finish. The `pending` counter tracks `io_setup` and `io_destroy` operations separately from data requests.

| Slot state | Meaning |
| --- | --- |
| FREE | No retained context |
| CREATING | Context allocation in progress |
| BORROWED | Context leased to a holder |
| IDLE | Context retained for reuse |
| DESTROYING | Context reclamation in progress |
| FAILED | Failed operation retained for ownership tracking |

In the fine-grained variant, the pool metadata lock guards state transitions, and the kernel operation runs outside that lock. The reader's `ctx_mut` protects the holder mapping and bulk lease return.

The reader waits for all scheduled reclamation before destruction returns. Per-group workers are joined at that boundary. Persistent workers are joined during final shutdown. Worker startup and shutdown are part of the complete task. A failed destroy leaves the context in the ownership ledger. Compare the local ledger with the kernel's `aio-nr` at stable idle and terminal observations. Kernel quota may be released before the local ledger closes the destroy operation.
