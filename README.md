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

Compile the library with `make all`

Change the `-d`, `-x`, `-i` of command line parameter according to the output of `get_cloudlab_node_settings.py`.

`-d` is the device index, `-x` is the sgid index, `-i` is the ib port.

To use this example on single node, use

```bash
# server side
./bin/ping_pong --local_ip 10.10.1.1 --port 10001  -i 1 -x 3 -d 2 --server_ip 10.10.1.1
# client side
./bin/ping_pong --port 10001  -i 1 -x 3 -d 2 --server_ip 10.10.1.1
```

The `port` and the `server_ip` parameter can be changed to run separately on different machine.

To use specific interface for the socket connection, the option `local_ip` can be used.

The client will establish socket connection to the server first.
Then client and server will establish RC RDMA connection.
