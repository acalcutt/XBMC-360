#ifndef HEADER_CURL_CONFIG_XBOX360_H
#define HEADER_CURL_CONFIG_XBOX360_H
/***************************************************************************
 * libcurl's configuration for the Xbox 360 XDK (the VS2010 compiler for a
 * big-endian PowerPC), made by hand from curl 8.15.0's config-win32.h.
 * libcurl.vcxproj defines HAVE_CONFIG_H so curl_setup.h includes this one.
 *
 * The XDK has xtl.h and winsockx.h where Windows has windows.h and
 * winsock2.h: this folder's windows.h, winsock2.h and ws2tcpip.h stand in.
 ***************************************************************************/

/* ---------------------------------------------------------------- */
/*            What 360 Riffle uses: HTTP and HTTPS only             */
/* ---------------------------------------------------------------- */

#define CURL_DISABLE_DICT 1
#define CURL_DISABLE_FILE 1
#define CURL_DISABLE_FTP 1
#define CURL_DISABLE_GOPHER 1
#define CURL_DISABLE_IMAP 1
#define CURL_DISABLE_IPFS 1
#define CURL_DISABLE_LDAP 1
#define CURL_DISABLE_LDAPS 1
#define CURL_DISABLE_MQTT 1
#define CURL_DISABLE_POP3 1
#define CURL_DISABLE_RTSP 1
#define CURL_DISABLE_SMB 1
#define CURL_DISABLE_SMTP 1
#define CURL_DISABLE_TELNET 1
#define CURL_DISABLE_TFTP 1
#define CURL_DISABLE_WEBSOCKETS 1

/* Nor what keeps files or reads the environment or needs other libraries */
#define CURL_DISABLE_ALTSVC 1
#define CURL_DISABLE_AWS 1
#define CURL_DISABLE_CA_SEARCH 1
#define CURL_DISABLE_COOKIES 1
#define CURL_DISABLE_DOH 1
#define CURL_DISABLE_FORM_API 1
#define CURL_DISABLE_HSTS 1
#define CURL_DISABLE_KERBEROS_AUTH 1
#define CURL_DISABLE_MIME 1
#define CURL_DISABLE_NEGOTIATE_AUTH 1
#define CURL_DISABLE_NETRC 1
#define CURL_DISABLE_NTLM 1
#define CURL_DISABLE_PROXY 1

/* XNet has no loopback for socketpair(), which only wakes curl_multi_poll */
#define CURL_DISABLE_SOCKETPAIR 1

/* HTTPS through mbedTLS 2.28, with the config mbedtls.vcxproj builds it with */
#define USE_MBEDTLS 1
#define MBEDTLS_CONFIG_FILE "mbedtls/config-xbox360.h"

/* Names are looked up on the thread that asks (no resolver threads), with xbox360_stubs.c's gethostbyname_r through
   XNetDnsLookup, into curl's own buffer */
#define USE_SYNC_DNS 1
#define HAVE_GETHOSTBYNAME_R 1
#define HAVE_GETHOSTBYNAME_R_6 1

#define CURL_OS "ppc-xbox360"

/* ---------------------------------------------------------------- */
/*                          HEADER FILES                            */
/* ---------------------------------------------------------------- */

#define HAVE_STDINT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_FCNTL_H 1
#define HAVE_IO_H 1
#define STDC_HEADERS 1

/* Not in the XDK: arpa/inet.h, netdb.h, netinet/in.h, sys/socket.h, sys/select.h, sys/time.h, unistd.h, stdbool.h,
   inttypes.h, locale (setlocale), utime */

/* ---------------------------------------------------------------- */
/*                            FUNCTIONS                             */
/* ---------------------------------------------------------------- */

#define HAVE_CLOSESOCKET 1
#define HAVE_GETPEERNAME 1
#define HAVE_GETSOCKNAME 1
#define HAVE_IOCTLSOCKET 1
#define HAVE_IOCTLSOCKET_FIONBIO 1
#define HAVE_SELECT 1
#define HAVE_SOCKET 1
#define HAVE_STRDUP 1

#define HAVE_RECV 1
#define RECV_TYPE_ARG1 SOCKET
#define RECV_TYPE_ARG2 char *
#define RECV_TYPE_ARG3 int
#define RECV_TYPE_ARG4 int
#define RECV_TYPE_RETV int

#define HAVE_SEND 1
#define SEND_TYPE_ARG1 SOCKET
#define SEND_QUAL_ARG2 const
#define SEND_TYPE_ARG2 char *
#define SEND_TYPE_ARG3 int
#define SEND_TYPE_ARG4 int
#define SEND_TYPE_RETV int

/* Said to be there so curl doesn't load iphlpapi.dll for it (the console can't): xbox360_stubs.c's finds no
   interfaces by name */
#define HAVE_IF_NAMETOINDEX 1

/* Not in the XDK: gethostname, getaddrinfo, inet_pton/inet_ntop (curl has its own), getservbyname, signal,
   gettimeofday, setmode, utime */

/* ---------------------------------------------------------------- */
/*                         TYPES AND SIZES                          */
/* ---------------------------------------------------------------- */

#ifndef _SSIZE_T_DEFINED
#  define _SSIZE_T_DEFINED
#  define ssize_t int
#endif

#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_SIZE_T 4
#define SIZEOF_CURL_OFF_T 8
#define SIZEOF_OFF_T 4
#define SIZEOF_TIME_T 8   /* VS2010's time_t is 64-bit, the XDK's too */
#define HAVE_LONGLONG 1

#define HAVE_STRUCT_TIMEVAL 1

/* The Xbox 360 is a big-endian PowerPC */
#define WORDS_BIGENDIAN 1

#endif /* HEADER_CURL_CONFIG_XBOX360_H */
