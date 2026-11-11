if(USE_SYSTEM_RTMIDI)
  message(STATUS "RPCS3: using shared RtMidi")
  pkg_check_modules(RtMidi REQUIRED IMPORTED_TARGET rtmidi>=6.0.0)
  add_library(rtmidi INTERFACE)
  target_link_libraries(rtmidi INTERFACE PkgConfig::RtMidi)
  target_include_directories(rtmidi SYSTEM INTERFACE PkgConfig::RtMidi)
else()
  option(RTMIDI_API_JACK "Compile with JACK support." OFF)
  option(RTMIDI_BUILD_TESTING "Build test programs" OFF)
  set(RTMIDI_TARGETNAME_UNINSTALL "uninstall-rpcs3-rtmidi")

  # RtMidi's Android backend reaches for JNI_GetCreatedJavaVMs, which the JVM
  # in an Android app provides and which a plain shared library has no way to
  # link against: the core builds all the way through and then fails at the
  # last step with that one symbol undefined, out of androidGetThreadEnv().
  # There is no MIDI device for a core to reach for anyway.
  #
  # RTMIDI_API_AMIDI, which is what this looks like it should be turned off
  # with, is declared by RtMidi and then never read - the backend is keyed off
  # ANDROID alone. So the variable it does read is the one that has to go, for
  # the length of that add_subdirectory and no longer. With no API selected
  # RtMidi compiles its dummy backend, which is what a core wants.
  if(ANDROID)
    set(RPCS3_RTMIDI_SAVED_ANDROID "${ANDROID}")
    set(ANDROID FALSE)
  endif()
  add_subdirectory(rtmidi EXCLUDE_FROM_ALL)
  if(DEFINED RPCS3_RTMIDI_SAVED_ANDROID)
    set(ANDROID "${RPCS3_RTMIDI_SAVED_ANDROID}")
    unset(RPCS3_RTMIDI_SAVED_ANDROID)
  endif()

  # RtMidi is linked into the core, not a DLL of its own, but its CMake
  # builds it with RTMIDI_EXPORT, which makes its classes dllexport: a
  # Windows core then exports them along with its retro_* functions.
  get_target_property(RPCS3_RTMIDI_DEFS rtmidi COMPILE_DEFINITIONS)
  list(REMOVE_ITEM RPCS3_RTMIDI_DEFS RTMIDI_EXPORT)
  set_target_properties(rtmidi PROPERTIES COMPILE_DEFINITIONS "${RPCS3_RTMIDI_DEFS}")
  unset(RPCS3_RTMIDI_DEFS)
endif()
