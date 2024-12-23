# RDMA lib

To use the lib in makefile, add the following options during linkage

```
-L/path/to/this/repo -lRDMA_lib -libverbs
```

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

### get_cloudlab_node_settings.py

```bash
python3 ./scripts/get_cloudlab_node_settings.py
```
You will get the information needed for RDMA connection establishment, like device index, ib port number and gid index.

## examples

### ping_pong

Compile the library with `make all`

Change the `-d`, `-x`, `-i` of command line parameter according to previous section.

`-d` is the device index, `-x` is the sgid index, `-i` is the ib port.
This example involves two nodes, one server and one client.

First, we should check the RDMA parameter settings, and use the IP bound to the RDMA setting we choose as the server local IP(-L option).

The client will use this IP in its (-H option) to denote the server destination.

If we use the example in picture ![](./figures/gid_instruction.png)

```bash
# client side
./bin/ping_pong -L 10.10.1.1 -port 10001  -i 1 -x 3 -d 2
# server side
./bin/ping_pong -p 10001  -i 1 -x 3 -d 2 -H 10.10.1.1
```

The `-L` parameter is used by server to denote its port for RDMA request and also socket connection.

The `-p` and the `-H` parameter can be changed to run separately on different machine.


The client will establish socket connection to the server first.
Then client and server will establish RC RDMA connection.
