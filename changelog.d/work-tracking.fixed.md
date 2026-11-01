Queued mock operations now register `io_context` work, so `io_context::run()`
no longer returns while a read or a delayed write is still outstanding.
