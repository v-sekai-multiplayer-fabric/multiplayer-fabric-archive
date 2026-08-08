## Pattern

```
create_transaction
  -> get(district)        [async future]
    -> on_district_read   [callback: extract next_o_id]
      -> get(customer)    [async future]
        -> on_customer_read [callback: extract discount]
          -> get(stock[0]) [async future]
            -> on_stock_read [callback: update stock, next item]
              -> ... (loop for 5-15 items)
              -> set(oorder, new_order, order_line[], stock[])
              -> commit [async future]
                -> on_commit [callback: send HTTP response]
```

## Why async, not blocking

FDB's C API is callback-based by design. `fdb_future_set_callback()`
fires when the future resolves. Blocking with
`fdb_future_block_until_ready()` would stall the event loop thread.

In the actor-lite architecture (`rfd/0072-actor-lite-worker-pool`),
the H2O network thread dispatches work to worker threads via SPSC
rings. If a worker blocks on FDB, it can't process the next request
from its ring.

## Error handling and retry

On any FDB error (conflict, timeout, etc.), call
`fdb_transaction_on_error(tr, err)`. This returns a future. When it
resolves, the transaction has been reset. The callback re-reads the
first key and restarts the chain.

The retry is transparent: the same `new_order_ctx_t` struct flows
through the chain, carrying the transaction parameters. On reset,
the read state is cleared and the chain restarts from step 1.

## Memory management

Each transaction allocates a context struct (`new_order_ctx_t`,
`payment_ctx_t`, etc.) on the heap. The context is freed in the final
callback (commit success or unrecoverable error). The FDB transaction
handle is destroyed in the same callback.

No reference counting needed: the callback chain is linear, each step
has exactly one outstanding future, and the context outlives all
callbacks.

## Limitations

- No batching across transactions: each HTTP request creates its
  own FDB transaction. Batching multiple requests into one transaction
  would improve throughput but violate TPC-C spec (each terminal
  executes one transaction at a time).
- Stack depth is bounded: the callback chain is finite (max 15 stock reads +
  writes + commit), so no stack overflow risk.
