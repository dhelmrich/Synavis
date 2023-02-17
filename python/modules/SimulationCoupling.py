import sys
import time
import os
import subprocess
import signalling_server as ss

# check python version
if sys.version_info[1] != 9 :
  print("Python version 3.9 is required")
  exit()

# print current path
print(os.getcwd())
# safe path
cwd = os.getcwd()
# change path to the directory where the library is located
os.chdir("../../unix/")
# build the library
subprocess.call("make -j4", shell=True)
# change path back to the original path
os.chdir(cwd)


sys.path.append("../../unix/")
import PyWebRTCBridge as rtc

ss.glog.info("Starting signalling server")
ss.client_port = 8889
ss.server_port = 8888
ts = ss.start_signalling()

ss.glog.info("Starting WebRTC bridge")
Client = rtc.DataConnector()
Client.SetConfig({"SignallingIP": "127.0.0.1", "SignallingPort": 8889, "Role": "client"})
Client.StartSignalling()

ts.wait()


