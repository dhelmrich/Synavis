import asyncio
import subprocess
import websockets as ws
import json
import time
from colorama import init as colorama_init
from colorama import Fore, Back, Style
import threading
import sys
import ast
import os
import numpy as np

ws.debug = True



target_ip = None
server_port = 8888
client_port = 8080
sfu_port = 8889 # not used yet, but this is the port that the SFU listens on for signalling
global_options = {
#  "PixelStreaming": {
#    "AllowPixelStreamingCommands": True,
#    "DisableLatencyTest": True
#  },
#  "Encoder":{
#    "TargetBitrate": -1,
#    "MaxBitrate": 20000000,
#    "MinQP": 0,
#    "MaxQP": 51,
#    "RateControl": "CBR",
#    "FillerData": 0,
#    "MultiPass": "FULL"
#  },
#  "WebRTC":{
#    "DegradationPref": "MAINTAIN_QUALITY",
#    "FPS": 10,
#    "MinBitrate": 100000,
#    "MaxBitrate": 100000000,
#    "LowQP": 25,
#    "HighQP": 37
#    },
#    "ConfigOptions": { }
}

ifconfig = []
# read in ifconfig globally once
if sys.platform.startswith("linux") :
  ifconfig = subprocess.check_output(["ifconfig", "-a"], stderr=subprocess.STDOUT).decode("utf-8").split("\n")
  ifconfig = [line.strip() for line in ifconfig if len(line) > 0]
#endif

class NoLog() :
  def write(self, message) :
    pass
  #enddef
  def close(self) :
    pass
  #enddef
  def flush(self) :
    pass
#endclass

class Logger() :
  def __init__ (self):
    colorama_init()
    self.light_mode = False
    # also open a log file
    i = 0
    timestamp = time.strftime("%Y-%m-%d_%H-%M-%S", time.localtime())
    self.fname = "sig_"+timestamp+"_"+str(i)+".log"
    # check if the file exists
    while os.path.exists(self.fname) :
      i += 1
      self.fname = "sig_"+timestamp+"_"+str(i)+".log"
    self.log_file = open(self.fname, "w")
  #enddef
  # desctructor
  def __del__(self) :
    self.log_file.close()
  #enddef
  def set_light_mode(self, light_mode) :
    self.light_mode = light_mode
    if light_mode :
      self.info("Light mode enabled")
    else :
      self.info("Light mode disabled")
    #endif
  #enddef
  def log(self, message, color = Fore.WHITE) :
    print(color + message + Style.RESET_ALL)
    self.log_file.write(message + "\n")
  #enddef
  def info(self, message) :
    self.log(message, (Fore.BLACK if self.light_mode else Fore.WHITE))
  #enddef
  def warn(self, message) :
    self.log(message, (Fore.MAGENTA if self.light_mode else Fore.YELLOW))
  #enddef
  def error(self, message) :
    self.log(message, Fore.RED)
  #enddef
  def success(self, message) :
    self.log(message, Fore.GREEN)
  #enddef
  def set_log_(self, log) :
    self.log_file.close()
    self.log_file = log
  #enddef
  def set_log_file(self, fname) :
    self.log_file.close()
    # quick convenience: remove old log file
    if os.path.exists(self.fname) :
      os.remove(self.fname)
    #endif
    self.fname = fname
    self.log_file = open(fname, "w")
  def log_outgoing(self, message, receiver = None) :
    if receiver != None :
      self.log(str(receiver.role) + "[" + str(receiver.id) + "]" + " -> " + str(message), Fore.BLUE)
    else :
      self.log("-> " + message, Fore.BLUE)
  #enddef
  def log_incoming(self, message, sender = None) :
    if sender != None :
      self.log(str(sender.role) + "[" + str(sender.id) + "]" + " <- " + str(message), Fore.CYAN)
    else :
      self.log("<- " + message, Fore.CYAN)
  #enddef
  def log_many(self, level = "info", *messages) :
    cumulated = ""
    for message in messages :
      cumulated += message + " "
    #endfor
    if level == "info" :
      self.info(cumulated)
    elif level == "warning" :
      self.warn(cumulated)
    elif level == "error" :
      self.error(cumulated)
    elif level == "success" :
      self.success(cumulated)
    else :
      self.info(cumulated)
#endclass

glog = Logger()

connections = {}
active_server = None
cluster_mode = False

# a method that searches network interfaces and selects an infiniband interface if available
def get_infiniband_interface() :
  global ifconfig
  # check operating system first (only linux is supported)
  if not sys.platform.startswith("linux") :
    return None
  # search for an infiniband interface
  for interface in ifconfig :
    if "ib" in interface :
      return interface.split(":")[0]
    #endif
  #endfor
  return None

def get_loopback_interface() :
  global ifconfig
  # check operating system first (only linux is supported)
  if not sys.platform.startswith("linux") :
    return None
  # search for a loopback interface
  for interface in ifconfig :
    if "LOOPBACK" in interface and "RUNNING" in interface :
      return interface.split(":")[0]
    #endif
  #endfor
  return None

# a method that produces ice candidates for a given network interface
def get_interface_ip(interface) :
  global ifconfig
  ip = ""
  for i in range(len(ifconfig)) :
    if interface in ifconfig[i] and "RUNNING" in ifconfig[i] :
      ip = ifconfig[i+1].split()[1]
      break
    #endif
  #endfor
  return ip
#enddef

# a method that returns all connections that do not have any other connections assigned to them
# parameter role: "server" or "client"
def get_unassigned_connections(role : str = "client") :
  unassigned = []
  for connection in connections.values() :
    if len(connection.connected_ids) == 0 and connection.role == role :
      unassigned.append(connection)
    #endif
  #endfor
  return unassigned
#enddef

# a method that waits for a few moments and then notifies Unreal about unmatched clients
async def notify_unreal_about_unmatched_clients() :
  global connections, glog, active_server
  await asyncio.sleep(2)
  for client in get_unassigned_connections("client") :
    active_server.assign(client)
    await active_server.send(json.dumps({"type":"playerConnected", "playerId": str(client.id), "dataChannel": True, "sfu":False}))
  #endfor

class Connection() :
  def __init__(self, websocket) :
    self.websocket = websocket
    self.id = None
    self.role = "server" if websocket.local_address[1] == server_port else "client"
    self.assign_id()
    self.recent = True
    self.connected_ids = []
    self.expects_media = False
    self.supports_extmap = False
    self.supports_synchronous_data = False
    self.extmap = []
  #enddef
  async def send(self, message) :
    global glog
    glog.log_outgoing(message, self)
    await self.websocket.send(message)
  #enddef
  def assign(self, other) :
    # add the connections to each other's list of connected connections if they are not already in there
    if not other.id in self.connected_ids :
      self.connected_ids.append(other.id)
    if not self.id in other.connected_ids :
      other.connected_ids.append(self.id)
  #enddef
  def unassign(self, other) :
    self.connected_ids.remove(other.id)
    # only we have to remove the id, because the other connection will be removed anyway
  #enddef
  def is_recent(self) :
    if self.recent :
      self.recent = False
      return True
    else :
      return False
  def close(self) :
    self.websocket.close()
  #enddef
  """ a method that assigns a new unique id to the connection"""
  def assign_id(self) :
    self.id = 101
    while self.id in connections :
      self.id += 1
    #endwhile	
  #enddef
  def __str__(self) :
    return "Connection[" + str(self.id) + "]" + "(" + self.role + ")"
  #enddef
  def ParseSDP(self, spd: str) :
    extmap_startswith = "a=extmap"
    extmap_skiplength = len(extmap_startswith)
    lines = spd.splitlines(False)
    categories = []
    app_and_media = [i for i in range(len(lines)) if lines[i].startswith("m=")]
    medias = [i for i in range(len(lines)) if lines[i].startswith("m=video") or lines[i].startswith("m=audio")]
    self.extmap = [[lines[i] for i in range(j,len(lines)) if lines[i].startswith(extmap_startswith)] for j in medias]
    hpc_line = [line for line in self.extmap if "hpc-bridge" in line]
    if len(self.extmap) > 0 :
      self.supports_extmap = True
    if len(hpc_line) > 0 :
      self.supports_synchronous_data = True
    #endfor
  #enddef
  def __len__(self) :
    return len(self.connected_ids)
  #enddef    
#endclass

async def handle(connection, message) :
  global connections, glog
  content = {}
  id = -1
  try :
    content = json.loads(message)
  except :
    glog.error("Invalid JSON: " + message)
    return
  glog.log_incoming(content, connection)
  if "type" in content :
    if content["type"] == "ping" :
      glog.log_outgoing("pong")
      content["type"] = "pong"
      # setting the time to the current time in seconds
      content["time"] = int(time.time())
      await connection.send(json.dumps(content))
    #endif
    elif "sdp" in content :
      sdp = content["sdp"]
      connection.ParseSDP(sdp)
      # clean up the json object so that unreal doesn't complain
      data = {"type": content["type"], "sdp": sdp}
      if connection.role == "server" :
        for player_id in connection.connected_ids :
          await connections[player_id].send(json.dumps(data))
      elif connection.role == "client" :
        if len(connection.connected_ids) == 0 :
          glog.log_many("warning", "No server connected for ", str(connection), " we will try again when a server connects")
          await connection.send(json.dumps({"type": "control", "message": "No server connected"}))
        for server_id in connection.connected_ids :
          data["playerId"] = connection.id
          await connections[server_id].send(json.dumps(data))
        #endfor
      #endif
    elif "candidate" in content or "iceCandidate" in content :
      data = {"type": content["type"], "candidate": content["candidate"]}
      if connection.role == "server" :
        for player_id in connection.connected_ids :
          await connections[player_id].send(json.dumps(data))
      elif connection.role == "client" :
        for server_id in connection.connected_ids :
          data["playerId"] = connection.id
          await connections[server_id].send(json.dumps(data))
        #endfor
      #endif
    elif "request" in content :
      if content["request"] == "id" :
        await connection.send(json.dumps({"type": "id", "id": connection.id}))
      elif content["request"] == "role" :
        await connection.send(json.dumps({"type": "role", "role": connection.role}))
      elif content["request"] == "connections" :
        await connection.send(json.dumps({"type": "connections", "connections": zip([id for id in connections], [connections[id].role for id in connections])}))
      elif content["request"] == "broadcast" :
        glog.info("Client " + str(connection.id) + " requested a broadcast")
    #endif
  #endif
#enddef

async def connection(websocket, path) :
  global connections, glog, active_server, active_client, global_options
  glog.log_many("info", "{" , str([id for id in connections]) , "}")
  connection = next((connections[id] for id in connections if connections[id].websocket == websocket), Connection(websocket))
  if connection.is_recent() :
    glog.success("open connection " + str(connection))
    connections[connection.id] = connection
  if connection.role == "client" :
    await connection.send(json.dumps({"type": "id", "id": connection.id}))
    servers = [id for id in connections if connections[id].role == "server"]
    least_used = None
    usage_min = np.argmin([len(connections[id]) for id in servers])
    if len(servers) > 0 and usage_min < len(servers) :
      least_used = connections[servers[usage_min]]
    #endif
    if least_used != None :
      connection.assign(least_used)
      least_used.assign(connection)
      # send an unreal-like player connect message
      await least_used.send(json.dumps({"type":"playerConnected", "playerId": str(connection.id), "dataChannel": True, "sfu": False}))
    #endif
  elif connection.role == "server" :
    # deactivate ping frames
    connection.websocket.ping_interval = None
    if active_server == None :
      # run the notify method non-blocking so that we can continue to accept new connections
      asyncio.ensure_future(notify_unreal_about_unmatched_clients())
    glog.info("New active server: " + str(connection))
    active_server = connection
  #endif
  # wait for a tiny moment and then send config
  #await asyncio.sleep(0.1)
  await connection.send(json.dumps({"type": "config", "peerConnectionOptions": global_options}))
  while True :
    try :
      message = await asyncio.wait_for(websocket.recv(), timeout = 1.0)
      await handle(connection, message)
    except asyncio.exceptions.TimeoutError as t :
      continue
    except ws.WebSocketException as e :
      # determine unix timestamp of when the connection was closed
      connection.close_time = int(time.time())
      if e.code in [1000,1001] :
        glog.warn("Websocket closed: " + str(e) + " " + str(connection) + " at time " + str(connection.close_time))
      else :
        glog.error("Websocket error: " + str(e) + " " + str(connection) + " at time " + str(connection.close_time))
      break
  connections.pop(connection.id)
  # send an unreal-like player disconnect message
  if connection.role == "client" :
    for server_id in connection.connected_ids :
      try :
        if connections[server_id].websocket.open :
          await connections[server_id].send(json.dumps({"type":"playerDisconnected", "playerId": str(connection.id)}))
      finally:
        connections[server_id].unassign(connection)
  elif connection.role == "server" :
    for client_id in connection.connected_ids :
      try :
        if connections[client_id].websocket.open :
          await connections[client_id].send(json.dumps({"type":"serverDisconnected", "serverId": connection.id}))
      finally:
        connections[client_id].unassign(connection)
    if active_server == connection :
      active_server = None
  glog.info("closed connection " + str(connection))
  del connection
#endeif

async def main() :
  global connections, glog, cluster_mode, server_port, client_port, target_ip, global_options
  # flag for whether a custom IP was provided
  custom_ip = False
  # checking if we have command line arguments
  if len(sys.argv) > 1 :
    # checking if we have lightmode enabled
    if "--lightmode" in sys.argv :
      glog.set_light_mode(True)
    elif "--help" in sys.argv :
      print("Usage: python3 signalling.py", end = " ")
      print("[--lightmode]", end = " ")
      print("[--cluster]")
      print("[--loopback]", end = " ")
      print("[--server-port <port>]", end = " ")
      print("[--client-port <port>]", end = " ")
      print("[--target-ip <ip>]")
      print("[--log-file <file>]", end = " ")
      print("[--no-log]")
      print("\n")
      print("  --lightmode: disables colored output")
      print("  --cluster: enables cluster mode")
      print("  --loopback: enables loopback interface mode (default is infiniband)")
      print("  --server-port <port>: sets the port for the server")
      print("  --client-port <port>: sets the port for the client")
      print("  --target-ip <ip>: sets the target ip for the client")
      print("  --log-file <file>: sets the log file")
      print("  --no-log: disables logging")
      return
    elif any(p in a for a in sys.argv for p in ["--server-port", "-s"]) :
      # get the port number
      server_port = int(sys.argv[sys.argv.index("--server-port") + 1])
    elif any(p in a for a in sys.argv for p in ["--client-port", "-c"]) :
      client_port = int(sys.argv[sys.argv.index("--client-port") + 1])
    elif any(p in a for a in sys.argv for p in ["--target-ip", "-t"]) :
      custom_ip = True
      target_ip = sys.argv[sys.argv.index("--target-ip") + 1]
    elif any(p in a for a in sys.argv for p in ["--log-file", "-f"]) :
      glog.set_log_file(sys.argv[sys.argv.index("--log-file") + 1])
    elif any(p in a for a in sys.argv for p in ["--cluster", "-j"]) :
      # cluster mode, communicate that certain ICE candidates are preffered to UE
      glog.info("Cluster mode enabled")
      cluster_mode = True
    elif "--no-log" in sys.argv :
      glog.set_log_(NoLog())
    elif "--loopback" in sys.argv :
      lo = get_loopback_interface()
      if lo != None :
        target_ip = get_interface_ip(lo)
        if target_ip != None :
          glog.info("Using loopback IP: " + target_ip)
        else :
          glog.warn("Detected loopback interface but could not get IP")
        # endif
  # endif
  # if no custom IP was provided, check for infiniband IP
  if not custom_ip :
    ib = get_infiniband_interface()
    if ib != None :
      target_ip = get_interface_ip("ib")
      if target_ip != None :
        glog.info("Using infiniband IP: " + target_ip)
      else :
        glog.warn("Detected infiniband interface but could not get IP")
      # endif
    else :
      glog.info("No infiniband interface detected")
    # endif
  #endif
  if target_ip == None :
    target_ip = "0.0.0.0"
    glog.warn("IP address was not provided through interface search or command line arguments")
    glog.warn("Using wildcard: " + target_ip)
  # start the signalling server
  # print where we expect the client/server connections
  glog.info("Starting signalling server..."
    + "\n  Server: " + target_ip + ":" + str(server_port)
    + "\n  Client: " + target_ip + ":" + str(client_port))
  

  async with ws.serve(connection, target_ip, server_port, compression = None) as ServerConnection, \
   ws.serve(connection, target_ip, client_port, compression = None) as ClientConnection:
    ServerConnection.ping_interval = None
    ClientConnection.ping_interval = None
    await asyncio.Future()

def start_signalling(External : bool = False) :
  global glog
  global server_port
  global connections
  global client_port
  global active_server
  glog.info("Starting server...")
  # run main in a new thread so that it doesn't block the main thread
  if External :
    # start unreal signalling instead
    # run node in command line
    # node ./unreal_signalling.js
    # run node in python
    output = open("unreal_signalling.log", "w")
    subprocess.Popen(["node", "../../../PixelStreamingInfrastructure/SignallingWebServer/cirrus.js"],stdout=output,stderr=output)
  else :
    return threading.Thread(target = asyncio.run, args = (main(),)).start()

if __name__ == "__main__" :
  asyncio.run(main())

