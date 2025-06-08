#!/bin/bash
wget https://www.mellanox.com/downloads/DOCA/DOCA_v2.10.0/host/doca-host_2.10.0-093000-25.01-ubuntu2204_amd64.deb
sudo dpkg -i doca-host_2.10.0-093000-25.01-ubuntu2204_amd64.deb
sudo apt-get update
sudo apt-get -y install doca-all
