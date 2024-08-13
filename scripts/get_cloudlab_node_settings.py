# Description: extract suitable RDMA device index, port number and gid
import re
import subprocess

# Run the command and capture the output
try:
    result = subprocess.run(['show_gids'], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    data = result.stdout
except subprocess.CalledProcessError as e:
    print(f"Error executing show_gids: {e}")
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
            # Extract the second and third columns
            ib_port = fields[1]
            gid_index = fields[2]
            # Print the extracted values
            print(f"device_index: {device_index}\nib_port: {ib_port}\ngid_index: {gid_index}")


