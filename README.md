# RDMA lib

To use the lib in makefile, add the following options during linkage

```
-L/path/to/this/repo -lRDMA_lib -libverbs
```

## scripts

### get_cloudlab_node_settings.py

```bash
python3 ./scripts/get_cloudlab_node_settings.py
```
You will get the information needed for RDMA connection establishment, like device index, ib port number and gid index.

## examples

### ping_pong

Change the `device_idx`, `sgid_idx`, `ib_port` of `rparams` in the source code `./examples/ping_pong.c` according to the output of `get_cloudlab_node_settings.py`.

```c
    struct rdma_param rparams = {
        .device_idx = 3,
        .sgid_idx = 3,
        .ib_port = 1,
        .qp_num = 2,
        .remote_mr_num = 2,
        .remote_mr_size = MR_SIZE,
        .init_cqe_num = 128,
        .max_send_wr = 100,
        .n_send_wc = 10,
        .n_recv_wc = 10,
    };
```

Then compile the library with `make all`

To use this example on single node, use

```bash
./bin/ping_pong --port 10000 # server
./bin/ping_pong --port 10000 --server_ip localhost # client
```

The `port` and the `server_ip` parameter can be changed to run separately on different machine.

To use specific interface for the socket connection, the option `local_ip` can be used.

```bash
./bin/ping_pong --port 10000 --local_ip 10.10.0.1 # server
./bin/ping_pong --port 10000 --server_ip 10.10.0.1 --local_ip 10.10.0.2 # client
```

The client will establish socket connection to the server first.
Then client and server will establish RC RDMA connection.
