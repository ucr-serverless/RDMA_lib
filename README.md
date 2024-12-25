# RDMA lib

To use the lib in makefile, add the following options to the ldflags

```
-L/path/to/this/repo -lRDMA_lib -libverbs
```

To use this library with a meson project, simply add `subdir('<path_to_this_lib>')` and then use the `libRDMA_lib_dep` in the dependencies of the compilation target.

To develop this library(compile tests and perftest, etc), use the `make all` or `make debug`

## determine RDMA specific settings

The `-d`, `-x` and `-i` setting specifies the RDMA device index, sgid index and ip port settings. These settings should be adjusted on a per node basis.

Please follow the following steps to determine these values.
![](./figures/gid_instruction.png)

1. determine a interface to use, note the interfaces on two nodes should in same IP sub network so they can talk to each other.

2. Choose the row with v2 instead of v1, which stands for RoCEv2 support.

3. determine the device index, which is number in yellow square labeled 3, and the full name of the device.

4. determine the ip port setting(`-i`), which is the number in the yellow sqare labeled 4.

5. determine the sgid index setting(`-x`), which is the number in the yellow sqare labeled 5.

For example, follow the setting in the picture, we should be using `python exp1.py --n_core 16 --n_qp 128 -x 3 -i 1 -d 2` on the server node and `python exp1.py --n_core 16 --n_qp 128 -x 3 -i 1 -d 2 --server_ip 10.10.1.1` on the client node.

## scripts

### `get_cloudlab_node_settings.py`

```bash
python3 ./scripts/get_cloudlab_node_settings.py
```
You will get the information needed for RDMA connection establishment, like device index, ib port number and gid index.

## examples

### `ping_pong`

The server will post a two side send request to client.
After receiving the request, client will post back a send request.

Compile the library with `make all`.

Change the `-d`, `-x`, `-i` of command line parameter according to the instruction from [determine RDMA specific settings](#determine-rdma-specific-settings).

`-d` is the device index, `-x` is the sgid index, `-i` is the ib port.
This example involves two nodes, one server and one client.

Assuming the picture bellow is from the client machine.
We can determine the RDMA specific settings accordingly.
The interface we are using is bound to IP `10.10.1.2`.

Assuming on the server machine, whose IP is `10.10.1.1`.
The client will use this IP (in its -H option) as the server destination.

![](./figures/gid_instruction.png)

```bash
# client side
./bin/ping_pong -p 10001  -i 1 -x 3 -d 2 -L 10.10.1.1
# server side
./bin/ping_pong -p 10001  -i 1 -x 3 -d 2 -H 10.10.1.1
```

The `-L` parameter is used by server to denote its port for RDMA request and also socket connection.
In our example it will be `10.10.1.1`

The `-p` is the port to be used to establish connection, which should be set to be the same on both server and client side.

### `ping_pong_cmplt_cnt` 

The functionalities and parameters are similar to `ping_pong binary`.
The only difference is the `ping_pong` use `ibv_poll_cq` to poll the completion queue.

The `ping_pong_cmplt_cnl` use completion channel and epoll.


Actually, they are interchangeable to each other. For example:

```bash
# server
./bin/ping_pong_cmplt_cnl -p 10001  -i 1 -x 3 -d 2 -L 10.10.1.1
# client
./bin/ping_pong -p 10001  -i 1 -x 3 -d 2 -H 10.10.1.1
```
