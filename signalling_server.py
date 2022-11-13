from json import JSONDecoder
from urllib import request
from flask import Flask, render_template, request, session, abort, url_for
from flask_socketio import SocketIO, send, emit, join_room, leave_room
import json
from enum import Enum

app = Flask(__name__)
socketio = SocketIO(app)

class EndpointType(Enum) :
  Streamer = 1
  Client = 2
  Matchmaker = 3
  TURN = 4
  Bridge = 5

class Endpoint :
  def __init__(self, id, name) :
    print("Created new Endpoint! ", id, " -> ", name)
    self.id = id
    self.name = name
  #end construct
#class Endpoint

class Media :
  def __init__(self, name) :
    self.name = name
  #enddef construct
#end class

def ParseSDP(spd: str) :
  lines = spd.splitlines(False)
  categories = []
  medias = [i for i in range(len(lines)) if lines[i].startswith("m=")]
  maplines = [[lines[i] for i in range(j,len(lines)) if lines[i].startswith("a=extmap") and "hpc-bridge" in lines[i]] for j in medias]

  print(maplines)
#enddef

@socketio.on('join')
def on_join(data):
  room = request.sid
  join_room(room)
#enddef


@socketio.on('leave')
def on_leave(data):
  username = data['username']
  room = data['room']
  leave_room(room)
  send(username + ' has left the room.', to=room)
#enddef

# Flask constructor takes the name of 
# current module (__name__) as argument.
app = Flask(__name__)
socketio = SocketIO(app)

Sessions = {}

PlayerSessions = []
StreamerSessions = []
Tracks = []

@socketio.on('connect')
def test_connect(auth):
  emit('my response', {'data': 'Connected'})
  id = request.sid
  orig = request.origin
#enddef

@socketio.on('disconnect')
def test_disconnect():
    print('Client disconnected')
#enddef

@socketio.on('json')
def signalling(json):
  content = json.decoder.JSONDecoder().decode(data)
  print("-> Signalling: ", content)
  if "type" in content :
    if content["type"] == "ping" :
      print("-> Server: Ping")
    #endif
  #endif
  id_entry = next(("id" in entry.toLowercase() for entry in content), -1)
  if id_entry != -1 :
    id = content[id_entry]
    if "sdp" in content :
      sdp = content["sdp"]
      metamap = ParseSDP(sdp)
    #endif
  #endif
#enddef

  
if __name__ == '__main__' :
  #socketio.run(app)
  with open("../../sdp.txt","r") as f:
    sdp = f.read()
    ParseSDP(sdp)
#endif main
  
