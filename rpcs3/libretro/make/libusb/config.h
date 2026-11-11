/* libusb's config.h, as its CMake (3rdparty/libusb/config.cmake) writes it,
 * for the Makefile build: one file for Linux, macOS and Windows. */
#ifndef LIBUSB_CONFIG_H
#define LIBUSB_CONFIG_H

#if defined(_MSC_VER)
#define PRINTF_FORMAT(a, b)
#define DEFAULT_VISIBILITY
#else
#define PRINTF_FORMAT(a, b) __attribute__ ((__format__ (__printf__, a, b)))
#define DEFAULT_VISIBILITY __attribute__((visibility("default")))
#endif

#define ENABLE_LOGGING

#define LIBUSB_MAJOR 1
#define LIBUSB_MINOR 0
#define LIBUSB_MICRO 0

#define PACKAGE libusb
#define PACKAGE_BUGREPORT libusb-devel@lists.sourceforge.net
#define PACKAGE_STRING libusb 1.0.26
#define PACKAGE_URL http://www.libusb.org
#define PACKAGE_VERSION 1.0.26
#define PACKAGE_TARNAME libusb

#define VERSION 1.0.26

#if defined(_WIN32)
#define OS_WINDOWS
#define HAVE_STRUCT_TIMESPEC
#elif defined(__APPLE__)
#define OS_DARWIN
#define THREADS_POSIX
#define HAVE_POLL_H
#define HAVE_SYS_TIME_H
#define POLL_NFDS_TYPE nfds_t
#else
#define _GNU_SOURCE 1
#define OS_LINUX
#define THREADS_POSIX
#define USBI_TIMERFD_AVAILABLE
#define HAVE_POLL_H
#define HAVE_SYS_TIME_H
#define POLL_NFDS_TYPE nfds_t
#endif

#endif /* LIBUSB_CONFIG_H */
