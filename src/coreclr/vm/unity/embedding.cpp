// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "common.h"
#include "embedding.h"
#include "formattype.h"
#include "typestring.h"
#include <coreclrhost.h>

namespace
{
void* s_runtimeHost;
unsigned int s_domainId;
using FatalError = void (__cdecl *)(PEXCEPTION_POINTERS);
using RemapPath = size_t (__cdecl *)(const char*, char*, size_t);
Volatile<FatalError> s_fatalError;
Volatile<RemapPath> s_remapPath;

// Unity passes a managed Assembly GCHandle, including the managed pin tag.
IMDInternalImport* GetImageMetadata(uintptr_t image)
{
    OBJECTHANDLE handle = reinterpret_cast<OBJECTHANDLE>(image & ~uintptr_t(1));
    ASSEMBLYREF assembly = (ASSEMBLYREF)ObjectFromHandle(handle);
    return assembly->GetAssembly()->GetMDImport();
}
}

extern "C" DLLEXPORT void __cdecl coreclr_initialize_domain(void* runtimeHost, unsigned int domainId)
{
    GetAppDomain()->SetForceTrivialWaitOperations();
    s_runtimeHost = runtimeHost;
    s_domainId = domainId;
}

extern "C" DLLEXPORT void* __cdecl unity_coreclr_create_delegate(const char* assembly, const char* type, const char* method)
{
    void* entry;
    return SUCCEEDED(coreclr_create_delegate(s_runtimeHost, s_domainId, assembly, type, method, &entry)) ? entry : nullptr;
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_set_on_fatal_error(FatalError callback)
{
    s_fatalError = callback;
}

void UnityEmbedding::NotifyFatalError(PEXCEPTION_POINTERS exception)
{
    FatalError callback = s_fatalError;
    if (callback != nullptr)
        callback(exception);
}

extern "C" DLLEXPORT void __cdecl mono_unity_register_path_remapper(RemapPath callback)
{
    s_remapPath = callback;
}

FCIMPL3(INT32, UnityEmbedding::InvokeNativeRemapper, BYTE* path, BYTE* buffer, INT32 size)
{
    FCALL_CONTRACT;
    RemapPath callback = s_remapPath;
    if (callback == nullptr)
        return 0;
    size_t bytes = callback(reinterpret_cast<const char*>(path), reinterpret_cast<char*>(buffer), size);
    return static_cast<INT32>(min(bytes, static_cast<size_t>(INT32_MAX)));
}
FCIMPLEND

extern "C" DLLEXPORT Thread* __cdecl mono_thread_attach(void*)
{
    return SetupThreadNoThrow();
}

extern "C" DLLEXPORT Thread* __cdecl mono_thread_current()
{
    return GetThreadNULLOk();
}

extern "C" DLLEXPORT void __cdecl mono_thread_detach(Thread* thread)
{
    thread->DetachThread(FALSE);
}

extern "C" DLLEXPORT int __cdecl coreclr_unity_array_element_size(MethodTable* array)
{
    return array->GetArrayElementTypeHandle().GetSize();
}

extern "C" DLLEXPORT int __cdecl coreclr_unity_class_array_element_size(MethodTable* element)
{
    return element->IsValueType() ? element->GetNumInstanceFieldBytes() : sizeof(void*);
}

extern "C" DLLEXPORT int __cdecl coreclr_unity_class_is_blittable(void* handle)
{
    TypeHandle type = TypeHandle::FromPtr(handle);
    if (type.IsTypeDesc())
        return type.IsPointer() || type.IsFnPtrType();
    MethodTable* mt = type.AsMethodTable();
    return mt->IsEnum() || (mt->IsValueType() && mt->IsBlittable());
}

extern "C" DLLEXPORT const char* __cdecl mono_class_get_name(void* handle)
{
    TypeHandle type = TypeHandle::FromPtr(handle);
    if (!type.IsTypeDesc() && !type.IsArray())
    {
        LPCUTF8 namespaze;
        return type.AsMethodTable()->GetFullyQualifiedNameInfo(&namespaze);
    }
    StackSString name;
    if (type.IsTypeDesc())
    {
        TypeString::AppendType(name, type, TypeString::FormatNamespace);
    }
    else
    {
        name.SetUTF8(mono_class_get_name(type.GetArrayElementTypeHandle().AsPtr()));
        name.Append(W('['));
        for (unsigned i = 1; i < type.GetRank(); ++i)
            name.Append(W(','));
        name.Append(W(']'));
    }
    static thread_local char formatted[512];
    strncpy_s(formatted, sizeof(formatted), name.GetUTF8(), _TRUNCATE);
    return formatted;
}

extern "C" DLLEXPORT const char* __cdecl mono_class_get_namespace(void* handle)
{
    TypeHandle type = TypeHandle::FromPtr(handle);
    if (type.IsTypeDesc())
        return "";
    LPCUTF8 namespaze;
    type.AsMethodTable()->GetFullyQualifiedNameInfo(&namespaze);
    return namespaze;
}

extern "C" DLLEXPORT void* __cdecl mono_class_get_type(void* type)
{
    return type;
}

extern "C" DLLEXPORT uint32_t __cdecl mono_class_get_type_token(MethodTable* mt)
{
    return mt->GetCl();
}

extern "C" DLLEXPORT int __cdecl mono_class_get_userdata_offset()
{
    return MethodTable::GetUnityUserDataOffset();
}

extern "C" DLLEXPORT void __cdecl mono_class_set_userdata(MethodTable* mt, void* data)
{
    mt->SetUnityUserData(data);
}

extern "C" DLLEXPORT int __cdecl mono_class_instance_size(MethodTable* mt)
{
    return sizeof(Object) + mt->GetNumInstanceFieldBytes();
}

extern "C" DLLEXPORT int __cdecl mono_unity_class_has_failure(MethodTable* mt)
{
    return mt->IsInitError();
}

extern "C" DLLEXPORT const char* __cdecl mono_field_get_name(FieldDesc* field)
{
    return field->GetName();
}

extern "C" DLLEXPORT int __cdecl mono_field_get_offset(FieldDesc* field)
{
    return field->IsStatic() ? 0 : sizeof(Object) + field->GetOffset();
}

extern "C" DLLEXPORT MethodTable* __cdecl mono_method_get_class(MethodDesc* method)
{
    return method->GetClass()->GetMethodTable();
}

extern "C" DLLEXPORT MethodDesc* __cdecl mono_method_signature(MethodDesc* method)
{
    return method;
}

extern "C" DLLEXPORT int __cdecl mono_type_get_type(void* handle)
{
    TypeHandle type = TypeHandle::FromPtr(handle);
    while (type.IsByRef())
        type = type.GetTypeParam();
    if (type.IsString())
        return ELEMENT_TYPE_STRING;
    if (type.IsObjectType())
        return ELEMENT_TYPE_OBJECT;
    if (type.HasInstantiation())
        return ELEMENT_TYPE_GENERICINST;
    return type.GetVerifierCorElementType();
}

extern "C" DLLEXPORT Object* __cdecl mono_type_get_object(void*, void* type)
{
    GCX_COOP();
    return OBJECTREFToObject(TypeHandle::FromPtr(type).GetManagedClassObject());
}

extern "C" DLLEXPORT Object* __cdecl mono_gchandle_get_target_v2(uintptr_t handle)
{
    GCX_COOP();
    return OBJECTREFToObject(ObjectFromHandle(reinterpret_cast<OBJECTHANDLE>(handle)));
}

extern "C" DLLEXPORT int __cdecl mono_image_get_table_rows(uintptr_t image, int table)
{
    GCX_COOP();
    return GetImageMetadata(image)->GetCountWithTokenKind(table);
}

// The name pointer is the first field of the caller-owned MonoAssemblyName.
extern "C" DLLEXPORT void __cdecl mono_assembly_get_assemblyref(uintptr_t image, int row, const char** name)
{
    GCX_COOP();
    GetImageMetadata(image)->GetAssemblyRefProps(TokenFromRid(row, mdtAssemblyRef), nullptr, nullptr,
        name, nullptr, nullptr, nullptr, nullptr);
}

extern "C" DLLEXPORT void __cdecl coreclr_image_get_custom_attribute_data(uintptr_t image, int index,
    mdToken* type, mdToken* parent)
{
    GCX_COOP();
    IMDInternalImport* metadata = GetImageMetadata(image);
    mdToken token = TokenFromRid(index + 1, mdtCustomAttribute);
    *type = *parent = mdTokenNil;
    metadata->GetCustomAttributeProps(token, type);
    metadata->GetParentToken(token, parent);
}

extern "C" DLLEXPORT bool __cdecl coreclr_unity_get_stackframe_info_from_ip(void* ip,
    void (__cdecl *report)(const char*, const char*, void*), void* context)
{
    EECodeInfo code(reinterpret_cast<PCODE>(ip));
    MethodDesc* method = code.IsValid() ? code.GetMethodDesc() : nullptr;
    if (method == nullptr)
        return false;
    SString owner, name, qualified;
    method->GetMethodInfoNoSig(owner, name);
    qualified.AppendPrintf("%s::%s", owner.GetUTF8(), name.GetUTF8());
    PCCOR_SIGNATURE signature;
    ULONG length;
    method->GetSig(&signature, &length);
    CQuickBytes text;
    PrettyPrintSig(signature, length, qualified.GetUTF8(), &text, method->GetMDImport(), nullptr);
    report(method->GetModule()->GetAssembly()->GetSimpleName(), static_cast<const char*>(text.Ptr()), context);
    return true;
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_gc_stress_pause(bool pause)
{
    if (GCStress<cfg_any>::IsEnabled())
    {
        if (pause)
            GCStressPolicy::GlobalDisable();
        else
            GCStressPolicy::GlobalEnable();
    }
}
