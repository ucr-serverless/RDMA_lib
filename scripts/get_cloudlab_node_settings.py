# Description: extract suitable RDMA device index, port number and gid
import re
import subprocess


def get_rate_limit(device_name):
    command = "ibv_devinfo -v"
    try:
        result = subprocess.run(command, shell=True, check=True, capture_output=True, text=True)
        data = result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error executing {command}: {e}")
        exit(1)

    line_pattern = re.compile(r'hca_id')

    speed_pattern = re.compile(r'(\d+)\.\d+\s*Gbps')
    lines = data.strip().split('\n')
    found_device = False
    for line in lines:
        match = line_pattern.search(line)
        if match and device_name in line:
            found_device = True
        if found_device and "active_speed" in line:
            speed_match = speed_pattern.search(line)
            if speed_match:
                rate_limit = speed_match.group(1)
                return rate_limit
    return None



# get the RDMA device device generation, like connect-x 3
def get_device_version():
    command = "lspci | grep 'Mellanox'"
    try:
        result = subprocess.run(command, shell=True, check=True, capture_output=True, text=True)
        data = result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error executing {command}: {e}")
        exit(1)

    lines = data.strip().split('\n')

    line_pattern = re.compile(r'\[(.*?)\]')

    for line in lines:
        match = line_pattern.search(line)
        if match:
            device_version = match.group(1)
            return device_version
    return None

# return the tuple (device_index, device_name, ib_port, gid_index)
def get_device_settings():
    command = "show_gids"
# Run the command and capture the output
    try:
        result = subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        data = result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error executing {command}: {e}")
        exit(1)

    lines = data.strip().split('\n')

# Regular expression to match the data plane IP and RoCE v2
    line_pattern = re.compile(r'10\.10\..*v2')
# Regular expression to get the device index
    mlx_pattern = re.compile(r'mlx[0-9]_([0-9]+)')

# Process each line
    for line in lines:
        # Check if the line matches the 10.10.*v2 pattern
        if line_pattern.search(line):
            # Split the line into fields
            fields = line.split()
            # Match the pattern in the first column
            match = mlx_pattern.match(fields[0])
            if match:
                # Extract the second number in the first column
                device_index = match.group(1)
            else:
                # the connect-x 3 devices
                device_index = 0 
                # Extract the second and third columns
            ib_port = fields[1]
            gid_index = fields[2]
            device_name = fields[0]
                # Print the extracted values
            return (device_index, device_name, ib_port, gid_index)
    return None

if __name__ == "__main__":
    result = get_device_settings()
    if result:
        device_index, device_name, ib_port, gid_index = result
        print(f"device_name: {device_name}\ndevice_index: {device_index}\nib_port: {ib_port}\nsgid_index: {gid_index}")
        rate_limit = get_rate_limit(device_name)
        if rate_limit:
            print(f"rate limit: {rate_limit}")
    else:
        print("dont't get useful information")
    result = get_device_version()
    if result:
        print(f"device_version: {result}")

