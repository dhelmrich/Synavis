// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
#define PYBIND11_DETAILED_ERROR_MESSAGES
#define _STL_CRT_SECURE_INVALID_PARAMETER(expr) _CRT_SECURE_INVALID_PARAMETER(expr)
#include <functional>
#include <numeric>
#include <iostream>
#include <string>
#include <vector>
#include <json.hpp>
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include <pybind11/cast.h>
#include <pybind11/iostream.h>
#include <pybind11/stl_bind.h>
#include <pybind11_json.hpp>

#include "DataConnector.hpp"
#include "MediaReceiver.hpp"
namespace py = pybind11;

#include "UnrealReceiver.hpp"
#include "Synavis.hpp"
#include "Seeker.hpp"
#include "Adapter.hpp"
#include "Provider.hpp"
#include "UnrealConnector.hpp"
#include <rtc/common.hpp>

namespace Synavis
{

  class PyReceiver : public UnrealReceiver
  {
  public:
    void UseConfig(std::string filename) override
    {
      PYBIND11_OVERLOAD(void, UnrealReceiver, UseConfig, filename);
    }

    void SetDataCallback(std::function<void(std::vector<std::vector<unsigned char>>)> DataCallback) override
    {
      PYBIND11_OVERLOAD(void, UnrealReceiver, SetDataCallback, DataCallback);
    }
  };

  template < typename T = Adapter > class PyAdapter : public T
  {
  public:
    using T::T;
    using json = nlohmann::json;
    std::string GenerateSDP() override { PYBIND11_OVERRIDE(std::string, T, GenerateSDP, ); }
    std::string Offer() override { PYBIND11_OVERRIDE(std::string, T, Offer, ); }
    std::string Answer() override { PYBIND11_OVERRIDE(std::string, T, Answer, ); }
    std::string PushSDP(std::string SDP) override { PYBIND11_OVERRIDE(std::string, T, PushSDP, SDP); }

    void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) override { PYBIND11_OVERLOAD_PURE(void,T,OnGatheringStateChange,inState);}
    void OnTrack(std::shared_ptr<rtc::Track> inTrack) override { PYBIND11_OVERLOAD_PURE(void,T,OnTrack,inTrack);}
    void OnLocalDescription(rtc::Description inDescription) override { PYBIND11_OVERLOAD_PURE(void,T,OnLocalDescription,inDescription);}
    void OnLocalCandidate(rtc::Candidate inCandidate) override { PYBIND11_OVERLOAD_PURE(void,T,OnLocalCandidate,inCandidate);}
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override { PYBIND11_OVERLOAD_PURE(void,T,OnDataChannel,inChannel);}
    void OnRemoteInformation(json message) override { PYBIND11_OVERLOAD_PURE(void,T,OnRemoteInformation,message);}
    void OnChannelPackage(rtc::binary inPackage) override { PYBIND11_OVERLOAD_PURE(void,T,OnChannelPackage,inPackage);}
    void OnChannelMessage(std::string inMessage) override { PYBIND11_OVERLOAD_PURE(void,T,OnChannelMessage,inMessage);}
  };

  template < typename T = Connector > class PyConnector : public PyAdapter<T>
  {
  public:
    using json = nlohmann::json;
    //using PyAdapter<T>::PyAdapter;
    void OnRemoteInformation(json message) override { PYBIND11_OVERRIDE(void, T, OnRemoteInformation, message); }
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override { PYBIND11_OVERRIDE(void, T, OnDataChannel, inChannel);}
  };

  template < typename T = Bridge > class PyBridge : public T
  {
  public:
    using T::T;
    using json = nlohmann::json;
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
    void RemoteMessage(json Message) override {}
    void OnSignallingData(rtc::binary Message) override { PYBIND11_OVERLOAD_PURE(void, T, OnSignallingData, Message); }
  };

  template < typename T = Provider > class PyProvider : public PyBridge<T>
  {
  public:
    using json = nlohmann::json;
    using PyBridge<T>::PyBridge;
    void OnSignallingMessage(std::string Message) override { PYBIND11_OVERRIDE(void, T, OnSignallingMessage, Message); }
    void RemoteMessage(json Message) override {  }
    void OnSignallingData(rtc::binary Message) override { PYBIND11_OVERRIDE(void, T, OnSignallingData, Message); }
    bool EstablishedConnection(bool Shallow) override { PYBIND11_OVERRIDE(bool, T, EstablishedConnection, Shallow); }
  };

  template < typename T = Seeker > class PySeeker : public PyBridge<T>
  {
  public:
    using json = nlohmann::json;
    using T::T;
    void OnRemoteInformation(json message) override { PYBIND11_OVERRIDE(void, T, OnRemoteInformation, message);  }
    void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) override { PYBIND11_OVERRIDE(void, T, OnGatheringStateChange, inState); };
    void OnTrack(std::shared_ptr<rtc::Track> inTrack) override { PYBIND11_OVERRIDE(void, T, OnTrack, inTrack); };
    void OnLocalDescription(rtc::Description inDescription) override { PYBIND11_OVERRIDE(void, T, OnLocalDescription, inDescription); };
    void OnLocalCandidate(rtc::Candidate inCandidate) override { PYBIND11_OVERRIDE(void, T, OnLocalCandidate, inCandidate); };
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override { PYBIND11_OVERRIDE(void, T, OnDataChannel, inChannel); };
  };

  template < typename T = UnrealConnector> class PyUnrealConnector : public PyAdapter<T>
  {
  public:
    using json = nlohmann::json;
    void OnRemoteInformation(json message) override { PYBIND11_OVERRIDE(void, T, OnRemoteInformation, message); }
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override { PYBIND11_OVERRIDE(void, T, OnDataChannel, inChannel);}
  };

  template < typename T = DataConnector > class PyDataConnector : public T
  {
    using T::T;
    using json = nlohmann::json;
    
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

  template < typename T = MediaReceiver > class PyMediaReceiver : public PyDataConnector<T>
  {

  };

  PYBIND11_MODULE(PySynavis, m)
  {
    py::enum_<EConnectionState>(m, "EConnectionState")
      .value("STARTUP", EConnectionState::STARTUP)
      .value("SIGNUP", EConnectionState::SIGNUP)
      .value("CONNECTED", EConnectionState::CONNECTED)
      .value("VIDEO", EConnectionState::VIDEO)
      .value("CLOSED", EConnectionState::CLOSED)
      .value("RTCERROR", EConnectionState::RTCERROR)
      .export_values()
    ;

    py::enum_<ELogVerbosity>(m, "LogVerbosity")
      .value("LogSilent", ELogVerbosity::Silent)
      .value("LogError", ELogVerbosity::Error)
      .value("LogWarning", ELogVerbosity::Warning)
      .value("LogInfo", ELogVerbosity::Info)
      .value("LogDebug", ELogVerbosity::Debug)
      .value("LogVerbose", ELogVerbosity::Verbose)
      .export_values()
    ;

    py::enum_<ECodec>(m, "Codec")
      .value("VP8", ECodec::VP8)
      .value("VP9", ECodec::VP9)
      .value("H264", ECodec::H264)
      .value("H265", ECodec::H265)
      .export_values()
    ;

    
    py::class_<UnrealReceiver, PyReceiver, std::shared_ptr<UnrealReceiver>>(m, "UnrealReceiver")
      .def(py::init<>())
      .def("RegisterWithSignalling", &UnrealReceiver::RegisterWithSignalling)
      .def("UseConfig", static_cast<void(UnrealReceiver::*)(std::string)>(&PyReceiver::UseConfig), py::arg("filename"))

    .def("SetDataCallback", static_cast<void (UnrealReceiver::*)(
        std::function<void(std::vector<std::vector<unsigned char>>)>)>
        (&PyReceiver::SetDataCallback), py::arg("DataCallback"))

      .def("RunForever", &UnrealReceiver::RunForever)
      .def("EmptyCache", &UnrealReceiver::EmptyCache)
      .def("SessionDescriptionProtocol", &UnrealReceiver::SessionDescriptionProtocol)
    ;
    
    py::class_<rtc::PeerConnection> (m, "PeerConnection")
    ;

    m.def("VerboseMode", &VerboseMode);

    py::class_<rtc::Configuration>(m, "PeerConnectionConfig")
        .def(py::init<>())
        .def_readwrite("IceServers", &rtc::Configuration::iceServers)
        .def_readwrite("portRangeBegin", &rtc::Configuration::portRangeBegin)
        .def_readwrite("portRangeEnd", &rtc::Configuration::portRangeEnd)
        .def_readwrite("maxMessageSize", &rtc::Configuration::maxMessageSize)
        .def_readwrite("iceTransportPolicy", &rtc::Configuration::iceTransportPolicy)
        .def_readwrite("proxyServer", &rtc::Configuration::proxyServer)
        .def_readwrite("disableAutoNegotiation", &rtc::Configuration::disableAutoNegotiation)
      ;

    py::class_ < rtc::IceServer >(m, "PeerConnectionIceServer")
      .def(py::init < std::string > ()) // uri
      .def(py::init<std::string, uint16_t>()) // uri, port
       .def(py::init<std::string, std::string>()) // hostname, service
      .def_readwrite("hostname", &rtc::IceServer::hostname)
      .def_readwrite("port" , &rtc::IceServer::port)
       .def_readwrite("username", &rtc::IceServer::username)
       .def_readwrite("password", &rtc::IceServer::password)
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
      .def("Initialize", &DataConnector::Initialize)
      .def("SendData", &DataConnector::SendData, py::arg("Data"))
      .def("SendString", &DataConnector::SendString, py::arg("Message"))
      .def("SendJSON", &DataConnector::SendJSON, py::arg("Message"))
      .def("SetOnRemoteDescriptionCallback", &DataConnector::SetOnRemoteDescriptionCallback, py::arg("Callback"))
      .def("SetDataCallback", &DataConnector::SetDataCallback,py::arg("Callback"))
      .def("SetMessageCallback", &DataConnector::SetMessageCallback,py::arg("Callback"))
      .def("SetConfig", &DataConnector::SetConfig,py::arg("Config"))
      .def("SetConfigFile", &DataConnector::SetConfigFile,py::arg("ConfigFile"))
      .def("StartSignalling", &DataConnector::StartSignalling)
      .def("IsRunning", &DataConnector::IsRunning)
      .def("SetTakeFirstStep",&DataConnector::SetTakeFirstStep,py::arg("SetTakeFirstStep"))
      .def("GetTakeFirstStep",&DataConnector::GetTakeFirstStep)
      .def("SetBlock", &DataConnector::SetBlock,py::arg("Block"))
      .def("IsBlocking",&DataConnector::IsBlocking)
      .def("GetState", &DataConnector::GetState)
      .def("SendBuffer", &DataConnector::SendBuffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendFloat64Buffer", &DataConnector::SendFloat64Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendInt32Buffer", &DataConnector::SendInt32Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendFloat32Buffer", &DataConnector::SendFloat32Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendGeometry", &DataConnector::SendGeometry, py::arg("Vertices"), py::arg("Indices"), py::arg("Name"), py::arg("Normals"),  py::arg("UVs"), py::arg("Tangents"), py::arg("AutoMessage"))
      .def("SetLogVerbosity", &DataConnector::SetLogVerbosity, py::arg("Verbosity"))
      .def("SetRetryOnErrorResponse", &DataConnector::SetRetryOnErrorResponse, py::arg("Retry"))
      .def("WriteSDPsToFile", &DataConnector::WriteSDPsToFile, py::arg("Filename"))
      .def_readwrite("IP", &DataConnector::IP)
      .def_readwrite("PortRange", &DataConnector::IP)
    ;

    py::class_<MediaReceiver, PyMediaReceiver<>, std::shared_ptr<MediaReceiver>>(m, "MediaReceiver")
      .def(py::init<>())
      .def("Initialize", &MediaReceiver::Initialize)
      .def("SetFrameReceptionCallback", &MediaReceiver::SetFrameReceptionCallback,py::arg("Callback"))
      .def("SetOnTrackOpenCallback", &MediaReceiver::SetOnTrackOpenCallback,py::arg("Callback"))
      .def("SetOnRemoteDescriptionCallback", &MediaReceiver::SetOnRemoteDescriptionCallback, py::arg("Callback"))
      .def("SendData", &MediaReceiver::SendData, py::arg("Data"))
      .def("SendString", &MediaReceiver::SendString, py::arg("Message"))
      .def("SendJSON", &MediaReceiver::SendJSON, py::arg("Message"))
      .def("SetDataCallback", &MediaReceiver::SetDataCallback, py::arg("Callback"))
      .def("SetMessageCallback", &MediaReceiver::SetMessageCallback, py::arg("Callback"))
      .def("SetConfig", &MediaReceiver::SetConfig, py::arg("Config"))
      .def("SetConfigFile", &MediaReceiver::SetConfigFile, py::arg("ConfigFile"))
      .def("StartSignalling", &MediaReceiver::StartSignalling)
      .def("IsRunning", &MediaReceiver::IsRunning)
      .def("SetTakeFirstStep", &MediaReceiver::SetTakeFirstStep, py::arg("SetTakeFirstStep"))
      .def("GetTakeFirstStep", &MediaReceiver::GetTakeFirstStep)
      .def("SetBlock", &MediaReceiver::SetBlock, py::arg("Block"))
      .def("IsBlocking", &MediaReceiver::IsBlocking)
      .def("GetState", &MediaReceiver::GetState)
      .def("SendBuffer", &MediaReceiver::SendBuffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendFloat64Buffer", &MediaReceiver::SendFloat64Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendInt32Buffer", &MediaReceiver::SendInt32Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendFloat32Buffer", &MediaReceiver::SendFloat32Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendGeometry", &MediaReceiver::SendGeometry, py::arg("Vertices"), py::arg("Indices"), py::arg("Name"), py::arg("Normals"),  py::arg("UVs"), py::arg("Tangents"), py::arg("AutoMessage"))
      .def("SetLogVerbosity", &MediaReceiver::SetLogVerbosity, py::arg("Verbosity"))
      .def("SetRetryOnErrorResponse", &MediaReceiver::SetRetryOnErrorResponse, py::arg("Retry"))
      .def("RequestKeyFrame", &MediaReceiver::RequestKeyFrame)
      .def("WriteSDPsToFile", &MediaReceiver::WriteSDPsToFile, py::arg("Filename"))
      .def("SetCodec", &MediaReceiver::SetCodec, py::arg("Codec"))
      .def_readwrite("IP", &MediaReceiver::IP)
      .def_readwrite("PortRange", &MediaReceiver::IP)
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

