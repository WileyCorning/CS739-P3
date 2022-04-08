#!/bin/bash

# on instance-5: <this script> 34.102.79.216
# on instance-6: <this script> 34.125.29.150

# Delete data file if it exists
[ -f fs_1 ] && rm fs_1

echo "Starting server (primary)"
src/cmake/build/server/server 5678 primary --backup-address $1:5678 fs_1

while true; do
    echo "Starting server (primary) with recover"
    src/cmake/build/server/server 5678 primary --backup-address $1:5678 fs_1 --recover
done
