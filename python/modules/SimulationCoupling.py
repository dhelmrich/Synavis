import sys
import time
import os
import subprocess
import signalling_server as ss

import socket   
hostname=socket.gethostname()   
IPAddr=socket.gethostbyname(hostname)   
print("Your Computer Name is:"+hostname)   
print("Your Computer IP Address is:"+IPAddr)   


# Whether or not to clean up the build first
clean_build = 0

# print current path
print(os.getcwd())
# safe path
cwd = os.getcwd()
if clean_build == 2 :
  # change path to the main project directory
  os.chdir("../../")
  # clean up the build
  subprocess.call("rm -rf unix/", shell=True)
  # create a new build
  subprocess.call("mkdir unix", shell=True)
  # change path to the directory where the library is located
  os.chdir("unix/")
  # make double sure that our configuration is up-to-date
  subprocess.call("cmake -H.. -B.", shell=True)
  # build the library
  subprocess.call("make -j4", shell=True)
  # change path back to the original path
  os.chdir(cwd)
elif clean_build == 1 :
  # change path to the directory where the library is located
  os.chdir("../../unix/")
  # build the library
  subprocess.call("make -j4", shell=True)
  # change path back to the original path
  os.chdir(cwd)


def OnData(data) :
  print("OnData: " + data)

sys.path.append("../../unix/")
import PyWebRTCBridge as rtc

ss.glog.info("Starting signalling server")
ss.client_port = 8889
ss.server_port = 8888
ss.target_ip = "0.0.0.0"
ts = ss.start_signalling()

ss.glog.info("Starting WebRTC bridge")
Client = rtc.DataConnector()
Client.SetConfig({"SignallingIP": IPAddr, "SignallingPort": 8889, "Role": "client"})
Client.SetCallback(OnData)
Client.StartSignalling()

while Client.IsRunning() :
  time.sleep(0.1)
