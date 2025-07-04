set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Local GC meta-issue: https://github.com/dotnet/runtime/issues/8061

# https://github.com/dotnet/runtime/issues/8059
remove_definitions(-DSTRESS_HEAP)

# https://github.com/dotnet/runtime/issues/8062
remove_definitions(-DWRITE_BARRIER_CHECK)

set(GC_SOURCES
  gceventstatus.cpp
  gcconfig.cpp
  gccommon.cpp
  gcscan.cpp
  gcsvr.cpp
  gcwks.cpp
  handletable.cpp
  handletablecore.cpp
  handletablescan.cpp
  objecthandle.cpp
  softwarewritewatch.cpp
  gchandletable.cpp
  gceesvr.cpp
  gceewks.cpp
  gcload.cpp
  handletablecache.cpp
  satori/SatoriGC.cpp
  satori/SatoriHandlePartitioner.cpp
  satori/SatoriHeap.cpp
  satori/SatoriPage.cpp
  satori/SatoriRegion.cpp
  satori/SatoriObject.cpp
  satori/SatoriRegionQueue.cpp
  satori/SatoriAllocator.cpp
  satori/SatoriRecycler.cpp
  satori/SatoriTrimmer.cpp
  satori/SatoriFinalizationQueue.cpp
  satori/SatoriAllocationContext.cpp
  satori/SatoriUtil.cpp
  satori/SatoriLock.cpp
  satori/SatoriWorkList.cpp
  satori/SatoriGate.cpp
)

if(CLR_CMAKE_HOST_UNIX)
  add_subdirectory(unix)
  include(unix/configure.cmake)
else()
  add_subdirectory(windows)
  set (GC_SOURCES
    ${GC_SOURCES}
    windows/Native.rc)
endif(CLR_CMAKE_HOST_UNIX)

if (CLR_CMAKE_TARGET_ARCH_AMD64)
  add_subdirectory(vxsort)
endif (CLR_CMAKE_TARGET_ARCH_AMD64)

if (CLR_CMAKE_TARGET_WIN32)
  set(GC_HEADERS
    env/common.h
    env/etmdummy.h
    env/gcenv.base.h
    env/gcenv.ee.h
    env/gcenv.h
    env/gcenv.interlocked.h
    env/gcenv.interlocked.inl
    env/gcenv.object.h
    env/gcenv.os.h
    env/gcenv.structs.h
    env/gcenv.sync.h
    env/gcenv.windows.inl
    env/volatile.h
    gc.h
    gcconfig.h
    gcdesc.h
    gcenv.ee.standalone.inl
    gcenv.inl
    gcevent_serializers.h
    gcevents.h
    gceventstatus.h
    gchandletableimpl.h
    gcimpl.h
    gcinterface.dac.h
    gcinterface.ee.h
    gcinterface.h
    gcpriv.h
    gcrecord.h
    gcscan.h
    handletable.h
    handletable.inl
    handletablepriv.h
    objecthandle.h
    softwarewritewatch.h
    satori/SatoriGC.h
    satori/SatoriHandlePartitioner.h
    satori/SatoriHeap.h
    satori/SatoriPage.h
    satori/SatoriRegion.h
    satori/SatoriRegion.inl
    satori/SatoriObject.h
    satori/SatoriObject.inl
    satori/SatoriQueue.h
    satori/SatoriRegionQueue.h
    satori/SatoriAllocator.h
    satori/SatoriRecycler.h
    satori/SatoriTrimmer.h
    satori/SatoriFinalizationQueue.h
    satori/SatoriWorkChunk.h
    satori/SatoriWorkList.h
    satori/SatoriAllocationContext.h
    satori/SatoriUtil.h
    satori/SatoriLock.h
    satori/SatoriGate.h
  )
endif(CLR_CMAKE_TARGET_WIN32)

if(CLR_CMAKE_HOST_WIN32)
  set (GC_LINK_LIBRARIES
    ${STATIC_MT_CRT_LIB}
    ${STATIC_MT_VCRT_LIB}
    kernel32.lib
    Synchronization.lib
    advapi32.lib)
endif(CLR_CMAKE_HOST_WIN32)

set (GC_LINK_LIBRARIES ${GC_LINK_LIBRARIES} gc_pal)

if(CLR_CMAKE_TARGET_ARCH_AMD64)
    list(APPEND GC_LINK_LIBRARIES
        gc_vxsort
    )
    list(APPEND GC_SOURCES
      ${CLR_SRC_NATIVE_DIR}/minipal/cpufeatures.c
    )
    include(${CLR_SRC_NATIVE_DIR}/minipal/configure.cmake)
endif(CLR_CMAKE_TARGET_ARCH_AMD64)


list(APPEND GC_SOURCES ${GC_HEADERS})

convert_to_absolute_path(GC_SOURCES ${GC_SOURCES})

# clrgcexp is build with standalone+regions
if (CLR_CMAKE_TARGET_ARCH_ARM64 OR CLR_CMAKE_TARGET_ARCH_AMD64)
  add_library_clr(clrgcexp SHARED ${GC_SOURCES})
  add_dependencies(clrgcexp eventing_headers)
  target_link_libraries(clrgcexp PRIVATE ${GC_LINK_LIBRARIES})
  target_compile_definitions(clrgcexp PRIVATE -DUSE_REGIONS)
  install_clr(TARGETS clrgcexp DESTINATIONS . COMPONENT runtime)
endif (CLR_CMAKE_TARGET_ARCH_ARM64 OR CLR_CMAKE_TARGET_ARCH_AMD64)

# clrgc is build with standalone+segments
add_library_clr(clrgc SHARED ${GC_SOURCES})
add_dependencies(clrgc eventing_headers)
target_link_libraries(clrgc PRIVATE ${GC_LINK_LIBRARIES})
install_clr(TARGETS clrgc DESTINATIONS . COMPONENT runtime)

add_definitions(-DBUILD_AS_STANDALONE)
add_definitions(-DFEATURE_CONSERVATIVE_GC)

add_definitions(-DFX_VER_INTERNALNAME_STR=clrgc.dll)
add_definitions(-DVERIFY_HEAP)
if(CLR_CMAKE_HOST_APPLE)
  # The implementation of GCToOSInterface on Apple platforms makes use of non-POSIX
  # pthreads APIs, which by default are not included in the pthreads header
  # unless we define this macro.
  add_definitions(-D_DARWIN_C_SOURCE)
endif(CLR_CMAKE_HOST_APPLE)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/env)

install_clr(TARGETS clrgc DESTINATIONS . sharedFramework COMPONENT runtime)
if (CLR_CMAKE_TARGET_ARCH_ARM64 OR CLR_CMAKE_TARGET_ARCH_AMD64)
  install_clr(TARGETS clrgcexp DESTINATIONS . sharedFramework COMPONENT runtime)
endif (CLR_CMAKE_TARGET_ARCH_ARM64 OR CLR_CMAKE_TARGET_ARCH_AMD64)