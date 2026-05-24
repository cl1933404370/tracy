#ifndef TRACY_HCOM_API_H
#define TRACY_HCOM_API_H

// TracyHcomApi.h
// DLL export/import macros for the Hcom bundle.
// When compiling the bundle as a shared library define TRACYHCOM_EXPORTS.
// When consuming it as a prebuilt DLL define TRACYHCOM_IMPORTS.
// For direct source compilation (the preferred Route A approach) leave both
// undefined – TRACYHCOM_API expands to nothing.

#if defined _WIN32
#  if defined TRACYHCOM_EXPORTS
#    define TRACYHCOM_API __declspec(dllexport)
#  elif defined TRACYHCOM_IMPORTS
#    define TRACYHCOM_API __declspec(dllimport)
#  else
#    define TRACYHCOM_API
#  endif
#else
#  if defined TRACYHCOM_EXPORTS || defined TRACYHCOM_IMPORTS
#    define TRACYHCOM_API __attribute__((visibility("default")))
#  else
#    define TRACYHCOM_API
#  endif
#endif

#endif // TRACY_HCOM_API_H
