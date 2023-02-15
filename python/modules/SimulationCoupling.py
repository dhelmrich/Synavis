import sys
import time
import os
import subprocess
import signalling_server as ss

# print current path
print(os.getcwd())
# safe path
cwd = os.getcwd()
# change path to the directory where the library is located
os.chdir("../../unix/")
# build the library
subprocess.call("make -j8", shell=True)
# change path back to the original path
os.chdir(cwd)


sys.path.append("../../unix/")
import PyWebRTCBridge as rtc


ss.client_port = 8889
ss.server_port = 8888
ss.start_signalling()

Client = rtc.DataConnector()



