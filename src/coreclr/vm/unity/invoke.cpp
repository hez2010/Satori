// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "common.h"

namespace
{
thread_local intptr_t t_invokeDepth;

class InvokeDepth
{
public:
    InvokeDepth() { ++t_invokeDepth; }
    ~InvokeDepth() { --t_invokeDepth; }
};

struct ByRefObject
{
    Object** destination;
    unsigned rootIndex;
};

OBJECTREF Invoke(MethodDesc* method, OBJECTREF* receiver, void** parameters, OBJECTREF* exception)
{
    MetaSig sig(method);
    unsigned count = sig.NumFixedArgs();
    CQuickArray<void*> addresses;
    CQuickArray<Object*> objects;
    CQuickArray<ByRefObject> byRefs;
    void** args = addresses.AllocThrows(count);
    Object** roots = objects.AllocThrows(count);
    ByRefObject* writeBack = byRefs.AllocThrows(count);
    memset(roots, 0, count * sizeof(*roots));
    unsigned byRefCount = 0;
    ValueClassInfo* valueRoots = nullptr;

    // Value arguments and byrefs point to caller-owned storage. Reference
    // arguments are object pointers, whereas reflection takes addresses of slots.
    for (unsigned i = 0; i < count; ++i)
    {
        CorElementType kind = sig.NextArg();
        args[i] = parameters[i];
        if (CorTypeInfo::IsObjRef(kind))
        {
            roots[i] = static_cast<Object*>(parameters[i]);
            args[i] = &roots[i];
        }
        else if (kind == ELEMENT_TYPE_PTR || kind == ELEMENT_TYPE_FNPTR)
        {
            args[i] = &parameters[i];
        }
        else if (kind == ELEMENT_TYPE_BYREF || kind == ELEMENT_TYPE_VALUETYPE)
        {
            TypeHandle type = sig.GetLastTypeHandleNT();
            if (type.IsNull())
                COMPlusThrow(kTypeLoadException);
            if (kind == ELEMENT_TYPE_BYREF)
                type = type.GetTypeParam();
            if (kind == ELEMENT_TYPE_BYREF && CorTypeInfo::IsObjRef(type.GetSignatureCorElementType()))
            {
                // Managed slots are traced by their containing object. Keep their
                // address live and let the managed call write through its byref.
                if (GCHeapUtilities::GetGCHeap()->IsHeapPointer(parameters[i]))
                    continue;
                Object** slot = static_cast<Object**>(parameters[i]);
                unsigned j = 0;
                while (j < byRefCount && writeBack[j].destination != slot)
                    ++j;
                if (j == byRefCount)
                {
                    roots[i] = *slot;
                    writeBack[byRefCount++] = {slot, i};
                }
                args[i] = &roots[writeBack[j].rootIndex];
            }
            else if (type.IsValueType() && type.AsMethodTable()->ContainsGCPointers() &&
                     !GCHeapUtilities::GetGCHeap()->IsHeapPointer(parameters[i]))
            {
                valueRoots = new (_alloca(sizeof(ValueClassInfo))) ValueClassInfo(parameters[i], type.AsMethodTable(), valueRoots);
            }
        }
    }

    OBJECTREF result = nullptr;
    GCFrame objectArguments(reinterpret_cast<OBJECTREF*>(roots), count, FALSE);
    GCFrame argumentAddresses(reinterpret_cast<OBJECTREF*>(args), count, TRUE);
    ProtectValueClassFrame values(GetThread(), valueRoots);
    EX_TRY
    {
        MethodDescCallSite invoke(CoreLibBinder::GetMethod(METHOD__UNITY_INVOCATION__INVOKE));
        ARG_SLOT arguments[] = { PtrToArgSlot(method), ObjToArgSlot(*receiver), PtrToArgSlot(args) };
        InvokeDepth depth;
        result = invoke.Call_RetOBJECTREF(arguments);
    }
    EX_CATCH_THROWABLE(exception);
    for (unsigned j = 0; j < byRefCount; ++j)
        *writeBack[j].destination = roots[writeBack[j].rootIndex];
    values.Pop(GetThread());
    argumentAddresses.Pop();
    objectArguments.Pop();
    return result;
}
}

extern "C" DLLEXPORT intptr_t __cdecl mono_method_get_last_managed()
{
    return t_invokeDepth;
}

extern "C" DLLEXPORT Object* __cdecl mono_runtime_invoke(MethodDesc* method, Object* target, void** arguments, Object** error)
{
    GCX_COOP();
    struct { OBJECTREF receiver, result, exception; } gc = {ObjectToOBJECTREF(target), nullptr, nullptr};
    GCPROTECT_BEGIN(gc);
    EX_TRY
    {
        gc.result = Invoke(method, &gc.receiver, arguments, &gc.exception);
    }
    EX_CATCH_THROWABLE(&gc.exception);
    if (error != nullptr)
        *error = OBJECTREFToObject(gc.exception);
    GCPROTECT_END();
    return OBJECTREFToObject(gc.result);
}
