// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
#define PYBIND11_DETAILED_ERROR_MESSAGES
//#define _STL_CRT_SECURE_INVALID_PARAMETER(expr) _CRT_SECURE_INVALID_PARAMETER(expr)
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
#include <pybind11_json/pybind11_json.hpp>

#include "DataConnector.hpp"
#include "MediaReceiver.hpp"

#ifdef ADIOS2_AVAILABLE
#include "adios/AdiosConnector.hpp"
#endif

#ifdef BUILD_WITH_DECODING
#include "FrameDecodeAV.hpp"
#endif

namespace py = pybind11;

#include "Synavis.hpp"
#include "Seeker.hpp"
#include "Adapter.hpp"
#include "Provider.hpp"
#include "UnrealConnector.hpp"
#include <rtc/common.hpp>

namespace Synavis
{

  // Helper: convert rtc::binary to Python bytes without iterating
  static py::bytes BinaryToPyBytes(const rtc::binary &data)
  {
    if (data.empty()) return py::bytes();
    return py::bytes(reinterpret_cast<const char*>(data.data()), data.size());
  }

  // Helper: convert Python bytes to rtc::binary
  static rtc::binary PyBytesToBinary(const py::bytes &b)
  {
    std::string s = static_cast<std::string>(b);
    rtc::binary out;
    out.assign(reinterpret_cast<const std::byte*>(s.data()), reinterpret_cast<const std::byte*>(s.data() + s.size()));
    return out;
  }

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

#ifdef ADIOS2_AVAILABLE
  template < typename T = AdiosConnector > class PyAdiosConnector : public T
  {
  public:
    using T::T;
    using binary = T::binary;
  };
#endif

  class SynavisLogger
  {
    public:
      SynavisLogger(){

      }
      void log(std::string message)
      {
        lp(Synavis::ELogVerbosity::Silent) << message << std::endl;
      }
      void logjson(nlohmann::json message)
      {
        lp(Synavis::ELogVerbosity::Silent) << message.dump() << std::endl;
      }
      void setidentity(std::string identity)
      {
        lp = Synavis::Logger::Get()->LogStarter(identity);
      }
      void logFile(std::string filename)
      {
        Synavis::Logger::Get()->SetupLogfile(filename);
      }
      void rotateLogFile(std::string filename)
      {
        Synavis::Logger::Get()->SetupLogfileRotate(filename);
      }
    private:
    Synavis::Logger::LoggerInstance lp{Synavis::Logger::Get()->LogStarter("Python")};
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

    py::class_<SynavisLogger>(m, "Logger")
      .def(py::init<>())
      .def("log", &SynavisLogger::log, py::arg("Message"))
      .def("logjson", &SynavisLogger::logjson, py::arg("Message"))
      .def("setidentity", &SynavisLogger::setidentity, py::arg("Identity"))
      .def("logFile", &SynavisLogger::logFile, py::arg("Filename"))
      .def("rotateLogFile", &SynavisLogger::rotateLogFile, py::arg("Filename"))
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

    // SetLogVerbostiy in PyBind through lambda accessing the singleton
    m.def("SetGlobalLogVerbosity", [](ELogVerbosity verbostiy)
    {
      Logger::Get()->SetVerbosity(verbostiy);
    }, py::arg("verbostiy"));

    py::enum_<ECodec>(m, "Codec")
      .value("VP8", ECodec::VP8)
      .value("VP9", ECodec::VP9)
      .value("H264", ECodec::H264)
      .value("H265", ECodec::H265)
      .export_values()
    ;

#ifdef BUILD_WITH_DECODING
    // Expose decoded frame container so Python callbacks can accept it directly
    py::class_<Synavis::FrameContent>(m, "FrameContent")
      .def(py::init<>())
      .def_readwrite("Data", &Synavis::FrameContent::Data)
      .def_readwrite("Width", &Synavis::FrameContent::Width)
      .def_readwrite("Height", &Synavis::FrameContent::Height)
      .def_readwrite("Timestamp", &Synavis::FrameContent::Timestamp)
      .def_readwrite("PixFmt", &Synavis::FrameContent::PixFmt)
      .def_readwrite("Linesize", &Synavis::FrameContent::Linesize)
      .def_readwrite("Packed", &Synavis::FrameContent::Packed)
    ;

    m.def("RegisterAvLogCallback", &Synavis::RegisterAvLogCallback, py::arg("useSynavisLogging") = false);
#endif
    
    py::class_<rtc::PeerConnection> (m, "PeerConnection")
    ;

    // Bind rtc::FrameInfo so Python callbacks can accept it directly
    py::class_<rtc::FrameInfo>(m, "FrameInfo")
      .def(py::init<uint32_t>())
      .def_readwrite("timestamp", &rtc::FrameInfo::timestamp)
      .def_readwrite("payloadType", &rtc::FrameInfo::payloadType)
    ;

    m.def("VerboseMode", &VerboseMode, py::arg("useSynavisLogging") = false);
    m.def("SilentMode", &SilentMode);
    m.def("ExitWithMessage", &ExitWithMessage, py::arg("Message"), py::arg("Code"));
#ifdef BUILD_WITH_DECODING
    m.def("RegisterAvLogCallback", &Synavis::RegisterAvLogCallback, py::arg("useSynavisLogging") = false);
#endif


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

    py::class_<WorkerThread, std::shared_ptr<WorkerThread>>(m, "WorkerThread")
      .def(py::init<>())
      .def("AddTask", &WorkerThread::AddTask)
      .def("Stop", &WorkerThread::Stop)
      .def("GetTaskCount", &WorkerThread::GetTaskCount)
    ;

    py::class_<DataConnector, PyDataConnector<>, std::shared_ptr<DataConnector>>(m, "DataConnector")
      .def(py::init<>())
      .def("Initialize", &DataConnector::Initialize)
      .def("SendData", &DataConnector::SendData, py::arg("Data"))
      .def("SendString", &DataConnector::SendString, py::arg("Message"))
      .def("SendJSON", &DataConnector::SendJSON, py::arg("Message"))
      .def("SetOnRemoteDescriptionCallback", &DataConnector::SetOnRemoteDescriptionCallback, py::arg("Callback"))
      .def("SetOnSignallingServerOnlineCallback", &DataConnector::SetOnSignallingServerOnlineCallback, py::arg("Callback"))
      .def("SetOnClosedCallback", &DataConnector::SetOnClosedCallback, py::arg("Callback"))
      .def("SetDataCallback", &DataConnector::SetDataCallback,py::arg("Callback"))
      .def("SetMessageCallback", &DataConnector::SetMessageCallback,py::arg("Callback"))
      .def("SetOnDataChannelAvailableCallback", &DataConnector::SetOnDataChannelAvailableCallback,py::arg("Callback"))
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
      .def("SetTimeOut", &DataConnector::SetTimeOut, py::arg("TimeOut"))
      .def("SetFailIfNotComplete", &DataConnector::SetFailIfNotComplete, py::arg("FailIfNotComplete"))
      .def("SetDontWaitForAnswer", &DataConnector::SetDontWaitForAnswer, py::arg("DontWaitForAnswer"))
      .def_readwrite("IP", &DataConnector::IP)
      .def_readwrite("PortRange", &DataConnector::IP)
      .def("LockUntilConnected", &DataConnector::LockUntilConnected, py::arg("additional_wait") = 0)
      .def("SendToSignallingServer", &DataConnector::SendToSignallingServer, py::arg("Message"))
      .def("GetDataChannelNames", &DataConnector::GetDataChannelNames)
      .def("SelectDataChannelByName", &DataConnector::SelectDataChannelByName, py::arg("Name"))
      .def("SelectDataChannelByIndex", &DataConnector::SelectDataChannelByIndex, py::arg("Index"))
    ;

    py::class_<MediaReceiver, PyMediaReceiver<>, std::shared_ptr<MediaReceiver>>(m, "MediaReceiver")
      .def(py::init<>())
      .def("Initialize", &MediaReceiver::Initialize)
      .def("SetFrameReceptionCallback", [](MediaReceiver &self, std::function<bool(py::bytes, rtc::FrameInfo)> cb) {
        self.SetFrameReceptionCallback([cb](rtc::binary data, rtc::FrameInfo info) -> bool {
          py::gil_scoped_acquire acquire;
          try {
            py::bytes b = BinaryToPyBytes(data);
            return cb(b, info);
          } catch (const py::error_already_set &e) {
            Synavis::Logger::Get()->LogStarter("PyBind")(ELogVerbosity::Error) << "SetFrameReceptionCallback python exception: " << e.what() << std::endl;
            return false;
          }
        });
      }, py::arg("Callback"))

      .def("SetOnTrackOpenCallback", &MediaReceiver::SetOnTrackOpenCallback,py::arg("Callback"))
      .def("SetOnRemoteDescriptionCallback", &MediaReceiver::SetOnRemoteDescriptionCallback, py::arg("Callback"))
      .def("SetOnDataChannelAvailableCallback", &MediaReceiver::SetOnDataChannelAvailableCallback,py::arg("Callback"))
      .def("SetOnClosedCallback", &DataConnector::SetOnClosedCallback, py::arg("Callback"))
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
      .def("SetTimeOut", &MediaReceiver::SetTimeOut, py::arg("TimeOut"))
      .def("SetFailIfNotComplete", &MediaReceiver::SetFailIfNotComplete, py::arg("FailIfNotComplete"))
      .def("SetDontWaitForAnswer", &MediaReceiver::SetDontWaitForAnswer, py::arg("DontWaitForAnswer"))
      .def_readwrite("IP", &MediaReceiver::IP)
      .def_readwrite("PortRange", &MediaReceiver::IP)
      .def("LockUntilConnected", &MediaReceiver::LockUntilConnected, py::arg("additional_wait") = 0)
      .def("SetOnTrackCloseCallback", &MediaReceiver::SetOnTrackCloseCallback, py::arg("Callback"))
      .def("GetDataChannelNames", &MediaReceiver::GetDataChannelNames)
      .def("SelectDataChannelByName", &MediaReceiver::SelectDataChannelByName, py::arg("Name"))
      .def("SelectDataChannelByIndex", &MediaReceiver::SelectDataChannelByIndex, py::arg("Index"))
      .def("NumRemoteMedia", &MediaReceiver::NumRemoteMedia)
      .def("RemoteMediaDescription", &MediaReceiver::RemoteMediaDescription, py::arg("id"))
    ;

#ifdef ADIOS2_AVAILABLE
    py::class_<AdiosConnector, PyAdiosConnector<>, std::shared_ptr<AdiosConnector>>(m, "AdiosConnector")
      .def(py::init<>())
      .def("Initialize", &AdiosConnector::Initialize)
      .def("StartStreaming", &AdiosConnector::StartStreaming)
      .def("StopStreaming", &AdiosConnector::StopStreaming)
      .def("StartReader", &AdiosConnector::StartReader)
      .def("StopReader", &AdiosConnector::StopReader)
      .def("ReadStep", &AdiosConnector::ReadStep)
      .def("IsRunning", &AdiosConnector::IsRunning)
      .def("GetState", &AdiosConnector::GetState)
      .def("SetEngineType", &AdiosConnector::SetEngineType, py::arg("EngineType"))
      .def("SetIOName", &AdiosConnector::SetIOName, py::arg("IOName"))
      .def("SetVariableName", &AdiosConnector::SetVariableName, py::arg("VariableName"))
      .def("SetMode", &AdiosConnector::SetMode, py::arg("Mode"))
      .def("SetFilenamePrefix", &AdiosConnector::SetFilenamePrefix, py::arg("FilenamePrefix"))
      .def("SetPort", &AdiosConnector::SetPort, py::arg("Port"))
      .def("SetNetworkInterface", &AdiosConnector::SetNetworkInterface, py::arg("Interface"))
      .def("GetPort", &AdiosConnector::GetPort)
      .def("GetNetworkInterface", &AdiosConnector::GetNetworkInterface)
      .def("SendData", &AdiosConnector::SendData, py::arg("Data"))
      .def("SendString", &AdiosConnector::SendString, py::arg("Message"))
      .def("SendJSON", &AdiosConnector::SendJSON, py::arg("Message"))
      .def("SendBuffer", &AdiosConnector::SendBuffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendFloat64Buffer", &AdiosConnector::SendFloat64Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendFloat32Buffer", &AdiosConnector::SendFloat32Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SendInt32Buffer", &AdiosConnector::SendInt32Buffer, py::arg("Buffer"), py::arg("Name"), py::arg("Format") = "raw")
      .def("SetDataCallback", &AdiosConnector::SetDataCallback, py::arg("Callback"))
      .def("SetMessageCallback", &AdiosConnector::SetMessageCallback, py::arg("Callback"))
      .def("SetReadCallback", [](AdiosConnector &self, std::function<void(py::object, const std::string&)> callback) {
        self.SetReadCallback([callback](const std::variant<AdiosConnector::binary, std::string>& data, const std::string& name) {
          py::gil_scoped_acquire acquire;
          try {
            if (std::holds_alternative<AdiosConnector::binary>(data)) {
              const auto& bin = std::get<AdiosConnector::binary>(data);
              py::bytes py_data(reinterpret_cast<const char*>(bin.data()), bin.size());
              callback(py_data, name);
            } else {
              const auto& str = std::get<std::string>(data);
              callback(py::str(str), name);
            }
          } catch (const py::error_already_set &e) {
            Synavis::Logger::Get()->LogStarter("PyBind")(ELogVerbosity::Error) << "SetReadCallback python exception: " << e.what() << std::endl;
          }
        });
      }, py::arg("Callback"))
      .def("SetOnConnectedCallback", &AdiosConnector::SetOnConnectedCallback, py::arg("Callback"))
      .def("SetOnFailedCallback", &AdiosConnector::SetOnFailedCallback, py::arg("Callback"))
      .def("SetOnClosedCallback", &AdiosConnector::SetOnClosedCallback, py::arg("Callback"))
      .def("LockUntilConnected", &AdiosConnector::LockUntilConnected, py::arg("additional_wait") = 0)
      .def("PrintConfiguration", &AdiosConnector::PrintConfiguration)
      .def("AddVariable", &AdiosConnector::AddVariable, py::arg("Name"), py::arg("DataType"))
      .def("WriteStep", &AdiosConnector::WriteStep)
      .def("Flush", &AdiosConnector::Flush)
    ;
#endif

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

#ifdef BUILD_WITH_DECODING

    // expose OutputMode enum for FrameDecode configuration
    py::enum_<EOutputMode>(m, "OutputMode")
      .value("Unchanged", EOutputMode::Unchanged)
      .value("PackedRGB", EOutputMode::PackedRGB)
      .export_values()
    ;

    py::class_<FrameDecode, std::shared_ptr<FrameDecode>>(m, "FrameDecode")
      .def(py::init([](ECodec codec){ return std::make_shared<FrameDecode>(codec, nullptr); }), py::arg("codec"))
      .def("CreateAcceptor", [](FrameDecode &self, py::function cb){
        // Wrap a python callable that expects a FrameContent into a C++ acceptor
        auto fn = [cb](Synavis::FrameContent frame){
          py::gil_scoped_acquire acquire;
          try {
            cb(frame);
          } catch (const py::error_already_set &e) {
            Synavis::Logger::Get()->LogStarter("PyBind")(ELogVerbosity::Error) << "CreateAcceptor python exception: " << e.what() << std::endl;
            throw;
          }
        };
        // Get the C++ acceptor (takes rtc::binary, rtc::FrameInfo)
        auto acceptor = self.CreateAcceptor(std::function<void(Synavis::FrameContent)>(fn));
        // Return a Python-callable that accepts bytes or sequences and calls the C++ acceptor directly
        return py::cpp_function([acceptor](py::object data, rtc::FrameInfo info) -> bool {
          py::gil_scoped_acquire acquire;
          rtc::binary bin;
          if (py::isinstance<py::bytes>(data) || py::isinstance<py::bytearray>(data)) {
            py::bytes b = py::bytes(data);
            bin = PyBytesToBinary(b);
          } else if (py::isinstance<py::sequence>(data)) {
            auto seq = py::reinterpret_borrow<py::sequence>(data);
            bin.clear();
            for (auto item : seq) {
              int v = item.cast<int>();
              bin.push_back(static_cast<std::byte>(v));
            }
          } else {
            throw py::type_error("CreateAcceptor wrapper: expected bytes or sequence for data");
          }
          return acceptor(bin, info);
        });
      })
      .def("SetFrameCallback", &FrameDecode::SetFrameCallback)
      .def("ParseDescription", &FrameDecode::ParseDescription, py::arg("desc"))
      .def("SetAcceptOnlyKeyframes", &FrameDecode::SetAcceptOnlyKeyframes, py::arg("b"))
      .def("GetAcceptOnlyKeyframes", &FrameDecode::GetAcceptOnlyKeyframes)
      .def_readwrite("OutputMode", &FrameDecode::OutputMode)
    ;
#endif

    py::class_<CommandLineParser, std::shared_ptr<CommandLineParser>>(m, "CommandLineParser")
      .def(py::init<const std::vector<std::string>&>())
      .def("HasArgument", &CommandLineParser::HasArgument, py::arg("Argument"))
      .def("GetArgument", &CommandLineParser::GetArgument, py::arg("Argument"))
    ;
  }

}

