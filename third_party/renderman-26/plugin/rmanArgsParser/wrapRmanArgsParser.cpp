//
// Copyright 2019 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/usd/ndr/node.h"
#include "pxr/usd/ndr/nodeDiscoveryResult.h"
#include "pxr/usd/sdr/shaderNode.h"
#include "rmanArgsParser/rmanArgsParser.h"

#include "pxr/external/boost/python.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

// Expose the unique_ptr returned from `Parse()` as a raw ptr. The Python side
// will be responsible for managing this object.
SdrShaderNodePtr
_Parse(RmanArgsParserPlugin& self, const NdrNodeDiscoveryResult& discoveryResult)
{
    return dynamic_cast<SdrShaderNodePtr>(
        self.Parse(discoveryResult).release()
    );
}

// Note that this parser is only wrapped for testing purposes. In real-world
// scenarios, it should not be used directly.
void wrapRmanArgsParser()
{
    typedef RmanArgsParserPlugin This;

    return_value_policy<copy_const_reference> copyRefPolicy;

    class_<This, noncopyable>("RmanArgsParser")
        .def("Parse", &_Parse, return_value_policy<manage_new_object>())
        .def("GetDiscoveryTypes", &This::GetDiscoveryTypes, copyRefPolicy)
        .def("GetSourceType", &This::GetSourceType, copyRefPolicy)
        ;
}
