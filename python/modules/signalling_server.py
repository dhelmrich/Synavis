from json import JSONDecoder, JSONEncoder
from urllib import request
from flask import Flask, render_template, request, session, abort, url_for, Response
import eventlet
import eventlet.wsgi
import socketio
#from flask_socketio import SocketIO, send, emit, join_room, leave_room
import json
from enum import Enum

sio = socketio.Server()
#app = Flask(__name__)
app = socketio.WSGIApp(sio)
#app.config['transports'] = 'websocket'
#socketio = SocketIO(app, logger=True, engineio_logger=True, debug=True)

@sio.on('*')
def catch_all(event, sid, data):
  print("hehehe")

#@app.route('/')
def starter():
  #return success
  print("index was called: ")
  print("Requested URL was ", request.base_url)
  print("Origin ", request.origin)
  return {}

#@app.route('/main')
def main():
  #return success
  print("index was called: ")
  print("Requested URL was ", request.base_url)
  print("Origin ", request.origin)
  return {}

#@app.errorhandler(404)
def page_not_found(error):
  print("error was called: ", error)
  print("Requested URL was ", request.base_url)
  print("Origin ", request.origin)
  exit(-1)
  return '{"type":"fail"}', 404

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
  extmap_startswith = "a=extmap"
  extmap_skiplength = len(extmap_startswith)
  lines = spd.splitlines(False)
  categories = []
  medias = [i for i in range(len(lines)) if lines[i].startswith("m=")]
  maplines = [[lines[i] for i in range(j,len(lines)) if lines[i].startswith(extmap_startswith) and "hpc-bridge" in lines[i]] for j in medias]
  print(maplines)
  for stream in medias :
    if len(maplines[stream]) > 0 :
      categories.append[(lines[medias.split(0)[2:]], int(maplines.split()[0][extmap_skiplength:]))]
  return categories
#enddef

@sio.event
def event_response(sid, env) :
  print("hehe")
  return '', 101

@sio.on('join')
def on_join(data):
  print("A endpoint joined")
  room = request.sid
  join_room(room)
  return '', 201
#enddef


@sio.on('leave')
def on_leave(data):
  print("An endpoint disconnected")
  username = data['username']
  room = data['room']
  leave_room(room)
  send(username + ' has left the room.', to=room)
#enddef

Sessions = {}

PlayerSessions = []
StreamerSessions = []
Tracks = []

@sio.on('connection')
def test_connect(auth):
  print("connected")
  sio.emit('my response', {'data': 'Connected'})
  id = request.sid
  orig = request.origin
  return '', 201
#enddef

@sio.on('disconnect')
def test_disconnect():
    print('Client disconnected')
#enddef

@sio.on('message', namespace='/')
def messgage(sid, data):
  print("message")
  sio.emit('message', data=data)

@sio.on('json')
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
      data = json()
      data["type"] = "extmap"
      data["extmap"] = JSONEncoder(metamap)
      for player in PlayerSessions :
        sio.emit(data.dump(), room = player)
    #endif
  #endif
  return '', 201
#enddef

def test(env, start_response) :
  print("Settings response for continue status codes")
  env['wsgi.input'].set_hundred_continue_response_headers(
    [('Hundred-Continue-Header-1', 'H1'),
      ('Hundred-Continue-Header-k', 'Hk')])
  print("Defining response")
  start_response('101 Continue', [('Content-Type', 'text/json')])
  print("Returning with status code")
  return "{'type':'success'}"


if __name__ == '__main__' :
  #from waitress import serve
  #serve(app, host="0.0.0.0", port=5000)
  #mware = socketio.WSGIApp(sio, app)
  eventlet.wsgi.server(eventlet.listen(('', 8888)), test)
  #socketio.run(app, debug=True, host="127.0.0.1",port=8888)
  #with open("../../sdp.txt","r") as f:
  #  sdp = f.read()
  #  ParseSDP(sdp)
#endif main
  
