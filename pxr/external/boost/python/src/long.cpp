//
// Copyright 2024 Pixar
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Copyright David Abrahams 2002.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
#include "pxr/external/boost/python/long.hpp"

namespace PXR_BOOST_NAMESPACE { namespace python { namespace detail {

new_reference long_base::call(object const& arg_)
{
    return (detail::new_reference)PyObject_CallFunction(
        (PyObject*)&PyLong_Type, const_cast<char*>("(O)"), 
        arg_.ptr());
}

new_reference long_base::call(object const& arg_, object const& base)
{
    return (detail::new_reference)PyObject_CallFunction(
        (PyObject*)&PyLong_Type, const_cast<char*>("(OO)"), 
        arg_.ptr(), base.ptr());
}

long_base::long_base()
    : object(
        detail::new_reference(
            PyObject_CallFunction((PyObject*)&PyLong_Type, const_cast<char*>("()")))
        )
{}

long_base::long_base(object_cref arg)
    : object(long_base::call(arg))
{}

long_base::long_base(object_cref arg, object_cref base)
    : object(long_base::call(arg, base))
{}


}}} // namespace PXR_BOOST_NAMESPACE::python
