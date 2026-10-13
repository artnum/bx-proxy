# Single header library for Snowflake ID

## Snowflake ID with IPC accross processes.

This implementation generate a **full 64 bits** ID so timestamp is 42 bits
instead of 41 bits of original implementation.

Sequence is sync accross multiple process with semaphore and shared memory,
each process register itself and the last one to exit properly will unlink
named semaphore and shared memory (of course if one crashes, the refcount
won't be decreased and it will be lying around).

Passage of time use clock_gettime: CLOCK_MONOTONIC and a relative view of
the time, meaning that if one process becomes somehow much in advance than
others, the others will take that process timestamp as their wall clock. We
make sure that the time is going forward and that the biggest timestamp of
the sequence become the time of the process.


## Documentation

Doxgen documentation within the source code. Running `doxygen snowflake.h` give
you usable documentation.

### 3 functions API

 - Open a context to generate snowflake id with 
```c
bool     snowflake_open  (snowflake_ctx_t *ctx, uint16_t node_id); 
```

 - Close a context with
```c
void     snowflake_close (snowflake_ctx_t *ctx);
```

 - Get an id with
```c
uint64_t snowflake_get   (snowflake_ctx_t *ctx);
```

For a working example, see the file `example.c`.

## License

Public domain.
