//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_BASE_TF_PY_IDENTITY_H
#define PXR_BASE_TF_PY_IDENTITY_H

#include "pxr/pxr.h"

#include "pxr/base/tf/api.h"
#include "pxr/base/tf/pyLock.h"
#include "pxr/base/tf/pyUtils.h"

#include "pxr/base/arch/demangle.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/refPtr.h"
#include "pxr/base/tf/safeTypeCompare.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/weakPtr.h"

#include "pxr/external/boost/python/handle.hpp"

#include "pxr/base/tf/hashmap.h"

// Specializations for pxr_boost::python::pointee and get_pointer for TfRefPtr and
// TfWeakPtr.
namespace PXR_BOOST_NAMESPACE { namespace python {

// TfWeakPtrFacade
template <template <class> class X, class Y>
struct pointee< PXR_NS::TfWeakPtrFacade<X, Y> > {
    typedef Y type;
};

// TfRefPtr
template <typename T>
struct pointee< PXR_NS::TfRefPtr<T> > {
    typedef T type;
};

}}

PXR_NAMESPACE_OPEN_SCOPE

struct Tf_PyIdentityHelper
{
    // Set the identity of ptr (which derives from TfPtrBase) to be the
    // python object \a obj.  
    TF_API
    static void Set(void const *id, PyObject *obj);

    // Return a new reference to the python object associated with ptr.  If
    // there is none, return 0.
    TF_API
    static PyObject *Get(void const *id);

    TF_API
    static void Erase(void const *id);

    // Acquire a reference to the python object associated with ptrBase
    // if not already acquired.
    TF_API
    static void Acquire(void const *id);

    // Release a reference to the python object associated with ptrBase
    // if we own a reference.
    TF_API
    static void Release(void const *id);
    
};

template <class Ptr>
void Tf_PyReleasePythonIdentity(Ptr const &ptr, PyObject *obj)
{
    Tf_PySetPythonIdentity(ptr, obj);
    Tf_PyIdentityHelper::Release(ptr.GetUniqueIdentifier());
}

void Tf_PyOwnershipRefBaseUniqueChanged(TfRefBase const *refBase,
                                        bool isNowUnique);

struct Tf_PyOwnershipPtrMap
{
    typedef TfHashMap<TfRefBase const *, void const *, TfHash>
    _CacheType;
    TF_API
    static void Insert(TfRefBase *refBase, void const *uniqueId);
    TF_API
    static void const *Lookup(TfRefBase const *refBase);
    TF_API
    static void Erase(TfRefBase *refBase);
  private:
    static _CacheType _cache;
};


// Doxygen generates files whose names are mangled typenames.  This is fine
// except when the filenames get longer than 256 characters.  This is one case
// of that, so we'll just disable doxygen.  There's no actual doxygen doc here,
// so this is fine.  If/when this gets solved for real, we can remove this
// (6/06)
#ifndef doxygen


template <class Ptr, typename Enable = void>
struct Tf_PyOwnershipHelper {
    template <typename U>
    static void Add(U const &, const void *, PyObject *) {}
    template <typename U>
    static void Remove(U const &, PyObject *) {}
};

template <typename Ptr>
struct Tf_PyOwnershipHelper<Ptr,
    std::enable_if_t<
        std::is_same<TfRefPtr<typename Ptr::DataType>, Ptr>::value &&
        std::is_base_of<TfRefBase, typename Ptr::DataType>::value>>
{
    static void Add(Ptr ptr, const void *uniqueId, PyObject *self) {

        TfPyLock pyLock;

        // Create a capsule to hold on to a heap-allocated instance of
        // Ptr. We'll set this as an attribute on the Python object so
        // it keeps the C++ object alive.
        pxr_boost::python::handle<> capsule(
            PyCapsule_New(
                new Ptr(ptr), "refptr",
                +[](PyObject* capsule) {
                    void* heldPtr = PyCapsule_GetPointer(capsule, "refptr");
                    delete static_cast<Ptr*>(heldPtr);
                }));

        int ret = PyObject_SetAttrString(self, "__owner", capsule.get());
        if (ret == -1) {
            // CODE_COVERAGE_OFF
            TF_WARN("Could not set __owner attribute on python object!");
            PyErr_Clear();
            return;
            // CODE_COVERAGE_ON
        }
        TfRefBase *refBase =
            static_cast<TfRefBase *>(get_pointer(ptr));
        Tf_PyOwnershipPtrMap::Insert(refBase, uniqueId);
    }
    
    static void Remove(Ptr ptr, PyObject *obj) {
        TfPyLock pyLock;

        if (!ptr) {
            // CODE_COVERAGE_OFF Can only happen if there's a bug.
            TF_CODING_ERROR("Removing ownership from null/expired ptr!");
            return;
            // CODE_COVERAGE_ON
        }
        
        if (PyObject_HasAttrString(obj, "__owner")) {
            // We are guaranteed that ptr is not unique at this point,
            // as __owner has a reference and ptr is itself a
            // reference.  This also guarantees us that the object owns
            // a reference to its python object, so we don't need to
            // explicitly acquire a reference here.
            TF_AXIOM(!ptr->IsUnique());
            // Remove this object from the cache of refbase to uniqueId
            // that we use for python-owned things.
            Tf_PyOwnershipPtrMap::Erase(get_pointer(ptr));
            // Remove the __owner attribute.
            if (PyObject_DelAttrString(obj, "__owner") == -1) {
                // CODE_COVERAGE_OFF It's hard to make this occur.
                TF_WARN("Undeletable __owner attribute on python object!");
                PyErr_Clear();
                // CODE_COVERAGE_ON
            }
        }
    }
};

#endif // doxygen -- see comment above.


template <typename Ptr>
struct Tf_PyIsRefPtr {
    static const bool value = false;
};

template <typename T>
struct Tf_PyIsRefPtr<TfRefPtr<T> > {
    static const bool value = true;
};


template <class Ptr>
std::enable_if_t<Tf_PyIsRefPtr<Ptr>::value>
Tf_PySetPythonIdentity(Ptr const &, PyObject *)
{
}

template <class Ptr>
std::enable_if_t<!Tf_PyIsRefPtr<Ptr>::value>
Tf_PySetPythonIdentity(Ptr const &ptr, PyObject *obj)
{
    if (ptr.GetUniqueIdentifier()) {
        Tf_PyIdentityHelper::Set(ptr.GetUniqueIdentifier(), obj);
        // Make sure we hear about it when this weak base dies so we can remove
        // it from the map.
        ptr.EnableExtraNotification();
    }
}

template <class Ptr>
PyObject *Tf_PyGetPythonIdentity(Ptr const &ptr)
{
    PyObject *ret = Tf_PyIdentityHelper::Get(ptr.GetUniqueIdentifier());
    return ret;
}

template <class Ptr>
void Tf_PyRemovePythonOwnership(Ptr const &t, PyObject *obj)
{
    Tf_PyOwnershipHelper<Ptr>::Remove(t, obj);
}

template <class Ptr>
void Tf_PyAddPythonOwnership(Ptr const &t, const void *uniqueId, PyObject *obj)
{
    Tf_PyOwnershipHelper<Ptr>::Add(t, uniqueId, obj);
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_BASE_TF_PY_IDENTITY_H
