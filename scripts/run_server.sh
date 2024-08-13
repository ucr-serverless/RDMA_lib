# sudo ./bin/rdma-bench -l 0 --file-prefix=server --proc-type=primary --no-telemetry --no-pci -- ./cfg/test.config 10000 is_server 3 3 1
sudo ./bin/rdma-bench  --sock_port 10000 --dev_index 3 --sgid_index 3 --ib_port 1 --benchmark_type 1 --msg_size 4 --num_concurr_msgs 1
