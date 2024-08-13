#!/bin/bash
# Description: remove system installed tcpdump and libpcap, install tcpdump for RDMA sniffer on cloudlab with Ubuntu 20.04
echo "the rdma tcpdump is only avaliable for connectx-4 or older!!"
sudo apt remove tcpdump libpcap0.8
cd /mydata
git clone https://github.com/the-tcpdump-group/tcpdump.git
git clone https://github.com/the-tcpdump-group/libpcap.git
cd libpcap
./autogen.sh
./configure
make
sudo make install
cd ../tcpdump
./autogen.sh
./configure
make
sudo make install

echo "verify it with `sudo tcpdump -i <rdma_device>`"
