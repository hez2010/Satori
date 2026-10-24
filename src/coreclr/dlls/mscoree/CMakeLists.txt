include_directories("../../inc")

if(FEATURE_GDBJIT)
   add_definitions(-DFEATURE_GDBJIT)
endif(FEATURE_GDBJIT)

set(CLR_SOURCES
    mscoree.cpp
    exports.cpp
)

if(CLR_CMAKE_TARGET_WIN32)
list(APPEND CLR_SOURCES
    ${CLR_SRC_NATIVE_DIR}/libs/Common/delayloadhook_windows.cpp
    Native.rc
)

set (DEF_SOURCES
  mscorwks_ntdef.src
)
else()
set (DEF_SOURCES
  mscorwks_unixexports.src
)
if(FEATURE_GDBJIT)
  list(APPEND DEF_SOURCES
    gdbjit_unixexports.src
  )
endif(FEATURE_GDBJIT)
endif(CLR_CMAKE_TARGET_WIN32)

if(FEATURE_UNITY_EMBEDDING_INTERFACE)
  list(APPEND DEF_SOURCES unityexports.src)
endif()
convert_to_absolute_path(DEF_SOURCES ${DEF_SOURCES})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DEF_SOURCES})
set(coreclr_exports "")
foreach(source IN LISTS DEF_SOURCES)
  file(READ "${source}" symbols)
  string(APPEND coreclr_exports "${symbols}\n")
endforeach()
set(DEF_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/coreclr_exports.src")
file(CONFIGURE OUTPUT "${DEF_SOURCES}" CONTENT "${coreclr_exports}" @ONLY)
convert_to_absolute_path(CLR_SOURCES ${CLR_SOURCES})
add_subdirectory(coreclr)
