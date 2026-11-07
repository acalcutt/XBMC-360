/* The XDK's winsock2.h is winsockx.h, with what curl uses that it doesn't have. Only curl's include path has this
   folder. */
#ifndef HEADER_CURL_XBOX360_WINSOCK2_H
#define HEADER_CURL_XBOX360_WINSOCK2_H
#include <xtl.h>
#include <winsockx.h>

#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 0x0008
#endif

/* No struct hostent or name lookup in the XDK: xbox360_stubs.c's gethostbyname_r uses XNetDnsLookup */
#ifndef _STRUCT_HOSTENT_DEFINED
#define _STRUCT_HOSTENT_DEFINED
struct hostent {
  char  *h_name;
  char **h_aliases;
  short  h_addrtype;
  short  h_length;
  char **h_addr_list;
};
#define h_addr h_addr_list[0]
#endif

int gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen, struct hostent **result,
                    int *h_errnop);

#endif
