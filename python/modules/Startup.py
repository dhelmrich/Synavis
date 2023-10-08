import os
import sys
from signalling_server import get_infiniband_interface, get_interface_ip

# Path: modules\Startup.py
# get the ip address of the infiniband interface
infiniband_interface = get_infiniband_interface()
infiniband_ip = get_interface_ip(infiniband_interface)


