/*
 * What libcurl needs that the Xbox 360 XDK doesn't have
 */

#include "curl_setup.h"

/* gethostbyname_r() (glibc's six arguments) through XNet's DNS
   (XNetDnsLookup), which needs XNET_STARTUP_BYPASS_SECURITY. Based on
   Neptune's NptBsdSockets.cpp. The answer goes in the caller's buffer, so
   names looked up on different threads at once (curl resolves on the thread
   that asks, USE_SYNC_DNS) don't share anything. Returns 0 with *result set,
   or an error with *result NULL. */
int gethostbyname_r(const char *name, struct hostent *ret, char *buf,
                    size_t buflen, struct hostent **result, int *h_errnop)
{
  WSAEVENT hEvent;
  XNDNS   *pDns = NULL;
  INT      err;
  size_t   namelen;
  char   **addr_list;
  char    *addr;
  char    *hostname;

  *result = NULL;
  *h_errnop = 0;

  if(!name || !name[0])
    return EINVAL;

  /* In buf: the address list (two pointers, so aligned first), the address
     and the name */
  namelen = strlen(name);
  if(buflen < 2 * sizeof(char *) + 4 + namelen + 1)
    return ERANGE;
  addr_list = (char **)buf;
  addr = buf + 2 * sizeof(char *);
  hostname = addr + 4;

  hEvent = WSACreateEvent();
  if(hEvent == WSA_INVALID_EVENT)
    return ENOMEM;

  err = XNetDnsLookup(name, hEvent, &pDns);
  if(err != 0 || !pDns) {
    WSACloseEvent(hEvent);
    return ENOENT;
  }

  WaitForSingleObject(hEvent, INFINITE);

  if(pDns->iStatus != 0 || pDns->cina == 0) {
    *h_errnop = pDns->iStatus;
    XNetDnsRelease(pDns);
    WSACloseEvent(hEvent);
    return ENOENT;
  }

  memcpy(addr, &pDns->aina[0], 4);
  memcpy(hostname, name, namelen + 1);
  addr_list[0] = addr;
  addr_list[1] = NULL;

  ret->h_name      = hostname;
  ret->h_aliases   = NULL;
  ret->h_addrtype  = AF_INET;
  ret->h_length    = 4;
  ret->h_addr_list = addr_list;

  XNetDnsRelease(pDns);
  WSACloseEvent(hEvent);

  *result = ret;
  return 0;
}

/* No network interfaces by name on the console (only IPv6 zone names use
   this, and there's no IPv6) */
unsigned int if_nametoindex(const char *ifname)
{
  (void)ifname;
  return 0;
}
