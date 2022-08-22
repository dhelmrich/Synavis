// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
#define _STL_CRT_SECURE_INVALID_PARAMETER(expr) _CRT_SECURE_INVALID_PARAMETER(expr)
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include <pybind11/cast.h>
#include <pybind11/iostream.h>
#include <pybind11/stl_bind.h>
#include <functional>
#include <numeric>
#include <iostream>
#include <string>
#include <vector>
namespace py = pybind11;

#include "UnrealReceiver.hpp"

namespace UR{


class PyReceiver : public UnrealReceiver
{
  public:
  using UnrealReceiver::UnrealReceiver;
  void UseConfig(std::string filename) override
  {
     PYBIND11_OVERLOAD(void, UnrealReceiver, UseConfig, filename);
  }

  void SetDataCallback(const std::function<void(std::vector<std::vector<unsigned char>>)>& DataCallback) override
  {
     PYBIND11_OVERLOAD(void, UnrealReceiver, SetDataCallback, DataCallback); 
  }
};

PYBIND11_MODULE(PyUnrealReceiver, m)
{
  py::class_<UnrealReceiver, PyReceiver, std::shared_ptr<UnrealReceiver>>(m, "PyUnrealReceiver")
    .def(py::init<>())
    .def("RegisterWithSignalling", &UnrealReceiver::RegisterWithSignalling)
    .def("UseConfig", (void(UnrealReceiver::*)(std::string)) & PyReceiver::UseConfig, py::arg("filename"))
    .def("SetDataCallback", (void (UnrealReceiver::*)(const std::function<void(std::vector<std::vector<unsigned char>>)>&)) & PyReceiver::SetDataCallback, py::arg("DataCallback"))
    //.def("SetDataCallback",[](const std::function<void(std::vector<std::byte>)>&){})
    .def("RunForever", &UnrealReceiver::RunForever)
    .def("EmptyCache",&UnrealReceiver::EmptyCache)
    .def("SessionDescriptionProtocol",&UnrealReceiver::SessionDescriptionProtocol);
}

}
