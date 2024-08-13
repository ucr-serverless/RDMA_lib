# sudo ./bin/rdma-bench -l 0 --file-prefix=client --proc-type=primary --no-telemetry --no-pci -- ./cfg/test.config 10000 is_client 3 3 1
./bin/rdma-bench  --sock_port 10000 --server_ip 10.10.1.1 --dev_index 3 --sgid_index 3 --ib_port 1 --benchmark_type 6 --msg_size 4 --num_concurr_msgs 64 --warm_up_iter 5000 --total_iter 200000 --signal_freq 40
