# RDMA lib


## install driver

The DOCA host package contains the ofed driver required for development

Use the `bash scripts/install_DOCA.sh` to install the DOCA host

## C library header

For the C library, we can include the `RDMA_c.h` as a all in one solution.

## CPP library header

For the CPP library, we can include the `RDMA_cpp.h` as a all in cone solution.

## build the library

This library provides two interfaces, one is for c named `libRDMA_lib` and the other is for cpp which is `libRDMA_lib_cpp`

*For Now the meson would only compile the libRDMA_lib*

*The Cmake would compile both*


## integrate with other code base

This project will be compiled as a static library to be linked with other codes.

The static binary will be `libRDMA_lib.a` under the project directory after compilation.

### manually intergration

After compiled this library with either cmake or ninja. One static libaray `libRDMA_lib.a` would be generated under the root of this repository.
One can manually link with this library with



```bash
-L/path/to/this/repo -lRDMA_lib -libverbs -I/path/to/RDMA_lib/include
```



Also add the `-I/path/to/RDMA_lib/include` to the cflags

### meson

```bash
meson setup build
ninja -C build/ -v
```

Compile this code base to get the static library, then 

```bash
ibverbs_dep = dependency('libibverbs', required: true)
incdir = include_directories('<path/to/RDMA_lib/include>')
rdma_dep = declare_dependency(
  include_directories: incdir,
  link_args: ['-L' + '<path/to/this/lib>', '-lRDMA_lib', ]
  )
```

Add the `incdir` to the include_directories of your target, then add the `rdma_dep` to the dependencies of your target.

Also this library requires the ibverbs library, so also add the `ibverbs_dep` to the dependencies of your target.

To use this library with a meson project, simply add `subdir('<path_to_this_lib>')` and then use the `libRDMA_lib_dep` in the dependencies of the compilation target.

### cmake

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
./build/ping_pong -p 10001  -i 1 -x 3 -d 2 -L 10.10.1.1
# server side
./build/ping_pong -p 10001  -i 1 -x 3 -d 2 -H 10.10.1.1
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
./build/ping_pong_cmplt_cnl -p 10001  -i 1 -x 3 -d 2 -L 10.10.1.1
# client
./build/ping_pong -p 10001  -i 1 -x 3 -d 2 -H 10.10.1.1
```

### rdma-bench


The `-t` parameter denotes the type of primitive to use.


In the one side mode, the `-t` should be set to be 4.

`-n` is the number of message.

`-s` is the size of the msg.

`-H` paremeter denotes the address of the server.

`-d` is the rdma device index.

`-x` is the gid index

`-i` is the ib port

`-p` is the port

does not change `-c`, this parameter should be set to be 4 in this experiment.

#### two side mode

In the two side mode, the `-t` type parameter should be set to be 1.

```bash
# client(DPU-1)
./build/rdma-bench -d 2 -x 1 -i 1 -p 8090 -t 1 -s 60000 -c 4 -n 100000 -H 192.168.10.48
# server(DPU-3)
./build/rdma-bench -d 2 -x 1 -i 1 -p 8090 -t 1 -s 60000 -c 4 -n 100000
```

#### one side mode with coordination

```bash
# client(DPU-1)
./build/rdma-bench -d 2 -x 1 -i 1 -p 8090 -t 4 -s 60000 -c 4 -n 100000 -H 192.168.10.48
# server(DPU-3)
./build/rdma-bench -d 2 -x 1 -i 1 -p 8090 -t 4 -s 60000 -c 4 -n 100000

```

#### one side mode with copy

the copy mode requires to add `-y` at the client
and remember to set the `-t` to be 4

```bash
# client(DPU-1)
./build/rdma-bench -d 2 -x 1 -i 1 -p 8090 -t 4 -s 60000 -c 4 -n 100000 -H 192.168.10.48 -y
# server(DPU-3)
./build/rdma-bench -d 2 -x 1 -i 1 -p 8090 -t 4 -s 60000 -c 4 -n 100000 -y
```

## install DOCA host

The OFED driver is installed as a package in the DOCA host package.

Follow the link of [DOCA host installation][https://developer.nvidia.com/doca-downloads]

### probing example

On the server side use

```bash
./build/probing -p 10001 -i 1 -x 3 -d 2 -L 10.10.1.1 -q 100 -t 100 -l 100 -o 1
```

On the client side
```bash
./build/probing -p 10001 -i 1 -x 3 -d 2 -H 10.10.1.1 -q 100
```

`-q` is the number of qp per host, `-t` is the numbers of iteration, `-l` is the sleep between iterations.
`-o` is the number of host

use `pidstat 1 -p $(pgrep probing)` to monitor the CPU usage of server

## trouble shooting

If post request on the unconnected QP, there would be error code 12
