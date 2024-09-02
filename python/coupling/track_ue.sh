#!/bin/bash
# script that detects the newest unreal_$rank-*.log file and prints tail -f of it

# filter for newest log file
log_file=$(ls -t /p/project1/visforai/helmrich1/unreal_*-*.log | head -n 1)
echo "Following log file: $log_file"

# print tail -f of log file
tail -f $log_file
