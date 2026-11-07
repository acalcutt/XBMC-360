/* Only if_nametoindex, for IPv6 zone names, which the console doesn't have: xbox360_stubs.c's finds none. Only
   curl's include path has this folder. */
#ifndef HEADER_CURL_XBOX360_IPHLPAPI_H
#define HEADER_CURL_XBOX360_IPHLPAPI_H
unsigned int if_nametoindex(const char *ifname);
#endif
