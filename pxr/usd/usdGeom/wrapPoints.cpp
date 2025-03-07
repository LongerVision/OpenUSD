//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdGeom/points.h"
#include "pxr/usd/usd/schemaBase.h"

#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/pyConversions.h"
#include "pxr/base/tf/pyContainerConversions.h"
#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/pyUtils.h"
#include "pxr/base/tf/wrapTypeHelpers.h"

#include "pxr/external/boost/python.hpp"

#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

#define WRAP_CUSTOM                                                     \
    template <class Cls> static void _CustomWrapCode(Cls &_class)

// fwd decl.
WRAP_CUSTOM;

        
static UsdAttribute
_CreateWidthsAttr(UsdGeomPoints &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateWidthsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->FloatArray), writeSparsely);
}
        
static UsdAttribute
_CreateIdsAttr(UsdGeomPoints &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateIdsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Int64Array), writeSparsely);
}

static std::string
_Repr(const UsdGeomPoints &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdGeom.Points(%s)",
        primRepr.c_str());
}

} // anonymous namespace

void wrapUsdGeomPoints()
{
    typedef UsdGeomPoints This;

    class_<This, bases<UsdGeomPointBased> >
        cls("Points");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("Define", &This::Define, (arg("stage"), arg("path")))
        .staticmethod("Define")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetWidthsAttr",
             &This::GetWidthsAttr)
        .def("CreateWidthsAttr",
             &_CreateWidthsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetIdsAttr",
             &This::GetIdsAttr)
        .def("CreateIdsAttr",
             &_CreateIdsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("__repr__", ::_Repr)
    ;

    _CustomWrapCode(cls);
}

// ===================================================================== //
// Feel free to add custom code below this line, it will be preserved by 
// the code generator.  The entry point for your custom code should look
// minimally like the following:
//
// WRAP_CUSTOM {
//     _class
//         .def("MyCustomMethod", ...)
//     ;
// }
//
// Of course any other ancillary or support code may be provided.
// 
// Just remember to wrap code in the appropriate delimiters:
// 'namespace {', '}'.
//
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

namespace {

static TfPyObjWrapper 
_ComputeExtent(object points, object widths) {
  
    // Convert from python objects to VtValue
    VtVec3fArray extent;
    VtValue pointsAsVtValue = UsdPythonToSdfType(points, 
        SdfValueTypeNames->Float3Array);
    VtValue widthsAsVtValue = UsdPythonToSdfType(widths, 
        SdfValueTypeNames->FloatArray);

    // Check Proper conversion to VtVec3fArray
    if (!pointsAsVtValue.IsHolding<VtVec3fArray>()) {
        TF_CODING_ERROR("Improper value for 'points'");
        return object();
    }

    if (!widthsAsVtValue.IsHolding<VtFloatArray>()) {
        TF_CODING_ERROR("Improper value for 'widths'");
        return object();
    }

    // Convert from VtValue to VtVec3fArray
    VtVec3fArray pointsArray = pointsAsVtValue.UncheckedGet<VtVec3fArray>();
    VtFloatArray widthsArray = widthsAsVtValue.UncheckedGet<VtFloatArray>();

    if (UsdGeomPoints::ComputeExtent(pointsArray, widthsArray, &extent)) {
        return UsdVtValueToPython(VtValue(extent));
    } else {
        return object();
    }
}

WRAP_CUSTOM {
    _class
        .def("GetWidthsInterpolation", &UsdGeomPoints::GetWidthsInterpolation)
        .def("SetWidthsInterpolation", &UsdGeomPoints::SetWidthsInterpolation,
             arg("interpolation"))

        .def("ComputeExtent",
            &_ComputeExtent, 
            (arg("points"), arg("widths")))
        .def("GetPointCount", &UsdGeomPoints::GetPointCount,
            arg("timeCode")=UsdTimeCode::Default()) 
        .staticmethod("ComputeExtent")

        ;
}

} // anonymous namespace
