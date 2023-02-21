// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
#define _STL_CRT_SECURE_INVALID_PARAMETER(expr) _CRT_SECURE_INVALID_PARAMETER(expr)
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include <pybind11/cast.h>
#include <pybind11/iostream.h>
#include <pybind11/stl_bind.h>
#include <pybind11_json.hpp>
#include <functional>
#include <numeric>
#include <iostream>
#include <string>
#include <vector>

#include "DataConnector.hpp"
#include "MediaReceiver.hpp"
namespace py = pybind11;

#include "UnrealReceiver.hpp"
#include "WebRTCBridge.hpp"
#include "Seeker.hpp"
#include "Adapter.hpp"
#include "Provider.hpp"
#include "UnrealConnector.hpp"

namespace WebRTCBridge{

  class PyReceiver : public UnrealReceiver
  {
  public:
    void UseConfig(std::string filename) override
    {
      PYBIND11_OVERLOAD(void, UnrealReceiver, UseConfig, filename);
    }

    void SetDataCallback(const std::function<void(std::vector<std::vector<unsigned char>>)>& DataCallback) override
    {
      PYBIND11_OVERLOAD(void, UnrealReceiver, SetDataCallback, DataCallback);
    }
  };

  template < typename T = Adapter > class PyAdapter : public T
  {
  public:
    using T::T;
    std::string GenerateSDP() override { PYBIND11_OVERRIDE(std::string, T, GenerateSDP, ); }
    std::string Offer() override { PYBIND11_OVERRIDE(std::string, T, Offer, ); }
    std::string Answer() override { PYBIND11_OVERRIDE(std::string, T, Answer, ); }
    std::string PushSDP(std::string SDP) override { PYBIND11_OVERRIDE(std::string, T, PushSDP, SDP); }

    void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) override { PYBIND11_OVERLOAD_PURE(void,T,OnGatheringStateChange,inState);}
    void OnTrack(std::shared_ptr<rtc::Track> inTrack) override { PYBIND11_OVERLOAD_PURE(void,T,OnTrack,inTrack);}
    void OnLocalDescription(rtc::Description inDescription) override { PYBIND11_OVERLOAD_PURE(void,T,OnLocalDescription,inDescription);}
    void OnLocalCandidate(rtc::Candidate inCandidate) override { PYBIND11_OVERLOAD_PURE(void,T,OnLocalCandidate,inCandidate);}
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override { PYBIND11_OVERLOAD_PURE(void,T,OnDataChannel,inChannel);}
    void OnRemoteInformation(T::json message) override { PYBIND11_OVERLOAD_PURE(void,T,OnRemoteInformation,message);}
    void OnChannelPackage(rtc::binary inPackage) override { PYBIND11_OVERLOAD_PURE(void,T,OnChannelPackage,inPackage);}
    void OnChannelMessage(std::string inMessage) override { PYBIND11_OVERLOAD_PURE(void,T,OnChannelMessage,inMessage);}
  };

  template < typename T = Connector > class PyConnector : public PyAdapter<T>
  {
  public:
    using T::json;
    //using PyAdapter<T>::PyAdapter;
    void OnRemoteInformation(T::json message) override { PYBIND11_OVERRIDE(void, T, OnRemoteInformation, message); }
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override { PYBIND11_OVERRIDE(void, T, OnDataChannel, inChannel);}
  };

  template < typename T = Bridge > class PyBridge : public T
  {
  public:
    using T::T;
    using T::json;
    void BridgeRun() override { PYBIND11_OVERRIDE(void, T, BridgeRun, ); }
    void Listen() override { PYBIND11_OVERRIDE(void, T, Listen, ); }
    bool CheckSignallingActive() override { PYBIND11_OVERRIDE(bool, T, CheckSignallingActive, ); }
    bool EstablishedConnection(bool Shallow) override { PYBIND11_OVERRIDE(bool, T, EstablishedConnection, Shallow); }
    void FindBridge() override { PYBIND11_OVERRIDE(void, T, FindBridge, ); }
    uint32_t SignalNewEndpoint() override { PYBIND11_OVERLOAD_PURE(uint32_t, T, SignalNewEndpoint, ); }
    void OnSignallingMessage(std::string Message) override { PYBIND11_OVERLOAD_PURE(void, T, OnSignallingMessage, Message); }
    // todo include pybind11_json for nlohmann::json binding
    // through cmake_fetchcontent https://github.com/pybind/pybind11_json
    //void RemoteMessage(json Message) override { PYBIND11_OVERLOAD_PURE(void, RemoteMessage, T, Message, ); }
    void RemoteMessage(T::json Message) override {}
    void OnSignallingData(rtc::binary Message) override { PYBIND11_OVERLOAD_PURE(void, T, OnSignallingData, Message); }
  };

  template < typename T = Provider > class PyProvider : public PyBridge<T>
  {
  public:
    using T::json;
    using PyBridge<T>::PyBridge;
    void OnSignallingMessage(std::string Message) override { PYBIND11_OVERRIDE(void, T, OnSignallingMessage, Message); }
    void RemoteMessage(T::json Message) override {  }
    void OnSignallingData(rtc::binary Message) override { PYBIND11_OVERRIDE(void, T, OnSignallingData, Message); }
    bool EstablishedConnection(bool Shallow) override { PYBIND11_OVERRIDE(bool, T, EstablishedConnection, Shallow); }
  };

  template < typename T = Seeker > class PySeeker : public PyBridge<T>
  {
  public:
    using T::json;
    using T::T;
    void OnRemoteInformation(T::json message) override { PYBIND11_OVERRIDE(void, T, OnRemoteInformation, message);  }
    void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) override { PYBIND11_OVERRIDE(void, T, OnGatheringStateChange, inState); };
    void OnTrack(std::shared_ptr<rtc::Track> inTrack) override { PYBIND11_OVERRIDE(void, T, OnTrack, inTrack); };
    void OnLocalDescription(rtc::Description inDescription) override { PYBIND11_OVERRIDE(void, T, OnLocalDescription, inDescription); };
    void OnLocalCandidate(rtc::Candidate inCandidate) override { PYBIND11_OVERRIDE(void, T, OnLocalCandidate, inCandidate); };
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override { PYBIND11_OVERRIDE(void, T, OnDataChannel, inChannel); };
  };

  template < typename T = UnrealConnector> class PyUnrealConnector : public PyAdapter<T>
  {
  public:
    void OnRemoteInformation(T::json message) override { PYBIND11_OVERRIDE(void, T, OnRemoteInformation, message); }
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override { PYBIND11_OVERRIDE(void, T, OnDataChannel, inChannel);}
  };

  template < typename T = DataConnector > class PyDataConnector : public T
  {
    using T::T;
    using T::json;
  };

  template < typename T = BridgeSocket > class PyBridgeSocket : public T
  {
    using T::T;
    using T::Peek;
    int Receive(bool invalidIsFailure = false) override
    {
      PYBIND11_OVERLOAD(int, BridgeSocket, Receive, invalidIsFailure);
    }
  };

  template < typename T = MediaReceiver > class PyMediaReceiver : public T
  {
    
  };

  PYBIND11_MODULE(PyWebRTCBridge, m)
  {
    py::class_<rtc::PeerConnection> (m, "PeerConnection")
    ;

    py::class_<BridgeSocket, PyBridgeSocket<>, std::shared_ptr<BridgeSocket>> (m, "BridgeSocket")
      .def(py::init<>())
      .def_property("Address", &BridgeSocket::GetAddress,&BridgeSocket::SetAddress)
      .def_property("Port",&BridgeSocket::GetSocketPort,&BridgeSocket::SetSocketPort)
      .def("Connect",&BridgeSocket::Connect)
      .def("Peek",&BridgeSocket::Peek)
      .def("ReinterpretInt",&BridgeSocket::Reinterpret<int>)
    ;

    py::class_<DataConnector, PyDataConnector<>, std::shared_ptr<DataConnector>>(m, "DataConnector")
      .def(py::init<>())
      .def("SendData", &DataConnector::SendData, py::arg("Data"))
      .def("SendMessage", &DataConnector::SendMessage, py::arg("Message"))
      .def("SetCallback", &DataConnector::SetCallback,py::arg("Callback"))
      .def("SetConfig", &DataConnector::SetConfig,py::arg("Config"))
      .def("StartSignalling", &DataConnector::StartSignalling)
      .def("IsRunning", &DataConnector::IsRunning)
      .def("SetTakeFirstStep",&DataConnector::SetTakeFirstStep,py::arg("SetTakeFirstStep"))
      .def("GetTakeFirstStep",&DataConnector::GetTakeFirstStep)
    ; 

    py::enum_<rtc::PeerConnection::GatheringState>(m, "GatheringState")
      .value("New", rtc::PeerConnection::GatheringState::New)
      .value("InProgress", rtc::PeerConnection::GatheringState::InProgress)
      .value("Complete", rtc::PeerConnection::GatheringState::Complete)
    ;

    py::class_<Bridge, PyBridge<Bridge>, std::shared_ptr<Bridge>>(m, "Bridge")
      .def("BridgeRun", &Bridge::BridgeRun)
      .def("Listen",&Bridge::Listen)
      .def("CheckSignallingActive", &Bridge::CheckSignallingActive)
      .def("EstablishedConnection", (bool(Bridge::*)(bool)) & PyBridge<Bridge>::EstablishedConnection)
      .def("FindBridge", &Bridge::FindBridge)
      .def("CreateTask",&Bridge::CreateTask)
    ;

    py::class_<Adapter, PyAdapter<Adapter>, std::shared_ptr<Adapter>>(m, "Adapter")
      .def("GenerateSDP", &Adapter::GenerateSDP)
      .def("Offer", (std::string(Adapter::*)(void)) & PyAdapter<Adapter>::Offer)
      .def("Answer", &Adapter::Answer)
      .def("PushSDP",(std::string(Adapter::*)(std::string)) & PyAdapter<Adapter>::PushSDP)
    ;

    py::class_<UnrealReceiver, PyReceiver, std::shared_ptr<UnrealReceiver>>(m, "UnrealReceiver")
      .def(py::init<>())
      .def("RegisterWithSignalling", &UnrealReceiver::RegisterWithSignalling)
      .def("UseConfig", (void(UnrealReceiver::*)(std::string)) & PyReceiver::UseConfig, py::arg("filename"))
      .def("SetDataCallback", (void (UnrealReceiver::*)(const std::function<void(std::vector<std::vector<unsigned char>>)>&)) & PyReceiver::SetDataCallback, py::arg("DataCallback"))
      //.def("SetDataCallback",[](const std::function<void(std::vector<std::byte>)>&){})
      .def("RunForever", &UnrealReceiver::RunForever)
      .def("EmptyCache",&UnrealReceiver::EmptyCache)
      .def("SessionDescriptionProtocol",&UnrealReceiver::SessionDescriptionProtocol)
    ;

    // python binding for Provider class, along with its methods
    py::class_<Provider, PyProvider<Provider>, std::shared_ptr<Provider>>(m, "Provider")
      .def(py::init<>())
      .def("UseConfig", (void(Provider::*)(std::string)) & PyProvider<>::UseConfig, py::arg("filename"))
      .def("EstablishedConnection", (bool(Provider::*)(bool)) & PyProvider<>::EstablishedConnection, py::arg("Shallow") = true)
      .def("FindBridge", &Provider::FindBridge)
      .def("OnSignallingMessage", (void(Provider::*)(std::string)) & PyProvider<>::OnSignallingMessage, py::arg("Message"))
    ;

  }

}
