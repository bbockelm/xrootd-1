/******************************************************************************/
/*                                                                            */
/*                        X r d N e t U t i l s . c c                         */
/*                                                                            */
/* (c) 2013 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*                            All Rights Reserved                             */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*              DE-AC02-76-SFO0515 with the Department of Energy              */
/*                                                                            */
/* This file is part of the XRootD software suite.                            */
/*                                                                            */
/* XRootD is free software: you can redistribute it and/or modify it under    */
/* the terms of the GNU Lesser General Public License as published by the     */
/* Free Software Foundation, either version 3 of the License, or (at your     */
/* option) any later version.                                                 */
/*                                                                            */
/* XRootD is distributed in the hope that it will be useful, but WITHOUT      */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public       */
/* License for more details.                                                  */
/*                                                                            */
/* You should have received a copy of the GNU Lesser General Public License   */
/* along with XRootD in a file called COPYING.LESSER (LGPL license) and file  */
/* COPYING (GPL license).  If not, see <http://www.gnu.org/licenses/>.        */
/*                                                                            */
/* The copyright holder's institutional names and contributor's names may not */
/* be used to endorse or promote products derived from this software without  */
/* specific prior written permission of the institution or contributor.       */
/******************************************************************************/

#include <ctype.h>
#include <inttypes.h>
#include <netdb.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "XrdNet/XrdNetAddr.hh"
#include "XrdNet/XrdNetUtils.hh"

/******************************************************************************/
/*                                D e c o d e                                 */
/******************************************************************************/
  
int XrdNetUtils::Decode(struct sockaddr_storage *sadr,
                        const char *buff, int blen)
{
   static const int ipv4Sz = sizeof(struct in_addr)*2+4;
   static const int ipv6Sz = sizeof(struct in6_addr)*2+4;
   union {struct sockaddr_in      *v4;
          struct sockaddr_in6     *v6;
          struct sockaddr_storage *data;
         }                         IP;
   char *dest, bval[sizeof(struct in6_addr)+2];
   int isv6, n, i = 0, Odd = 0;

// Determine if this will be IPV4 or IPV6 (only ones allowed)
//
        if (blen == ipv6Sz) isv6 = 1;
   else if (blen == ipv4Sz) isv6 = 0;
   else return -1;

// Convert the whole string to a temporary
//
   while(blen--)
        {     if (*buff >= '0' && *buff <= '9') n = *buff-48;
         else if (*buff >= 'a' && *buff <= 'f') n = *buff-87;
         else if (*buff >= 'A' && *buff <= 'F') n = *buff-55;
         else return -1;
         if (Odd) bval[i++] |= n;
            else  bval[i  ]  = n << 4;
         buff++; Odd = ~Odd;
        }

// Clear our address
//
   IP.data = sadr;
   memset(sadr, 0, sizeof(struct sockaddr_storage));

// Copy out the data, as needed
//
   if (isv6)
      {IP.v6->sin6_family = AF_INET6;
       memcpy(&(IP.v6->sin6_port),  bval, 2);
       memcpy(&(IP.v6->sin6_addr), &bval[2], sizeof(struct in6_addr));
      } else {
       IP.v4->sin_family  = AF_INET;
       memcpy(&(IP.v4->sin_port),  bval, 2);
       memcpy(&(IP.v4->sin_addr), &bval[2], sizeof(struct in_addr));
      }

// Return the converted port (it's the same for v4/v6)
//
   return static_cast<int>(ntohs(IP.v6->sin6_port));
}

/******************************************************************************/
/*                                E n c o d e                                 */
/******************************************************************************/
  
int XrdNetUtils::Encode(const struct sockaddr *sadr,
                        char *buff, int blen, int port)
{
   static const char *hv = "0123456789abcdef";
   union {const struct sockaddr_in  *v4;
          const struct sockaddr_in6 *v6;
          const struct sockaddr     *addr;
         }                           IP;
   char *src, bval[sizeof(struct in6_addr)+2];
   int asz, i, j = 0;

// Compute the size we need for the buffer (note we only support IP4/6)
//
    IP.addr = sadr;
        if (sadr->sa_family == AF_INET6)
           {src = (char *)&(IP.v6->sin6_addr); asz = sizeof(struct in6_addr);}
   else if (sadr->sa_family == AF_INET)
           {src = (char *)&(IP.v4->sin_addr);  asz = sizeof(struct in_addr); }
   else return 0;
   if (blen < (asz*2)+5) return -((asz*2)+5);

// Get the port value in the first two bytes followed by the address.
//
   if (port < 0) memcpy(bval, &(IP.v6->sin6_port), 2);
      else {short sPort = htons(static_cast<short>(port));
            memcpy(bval, &sPort, 2);
           }
   memcpy(&bval[2], src, asz);
   asz += 2;

// Now convert to hex
//
   for (i = 0; i < asz; i++)
       {buff[j++] = hv[(bval[i] >> 4) & 0x0f];
        buff[j++] = hv[ bval[i]       & 0x0f];
       }
   buff[j] = '\0';

// All done
//
   return asz*2;
}

/******************************************************************************/
/*                                 M a t c h                                  */
/******************************************************************************/
  
bool XrdNetUtils::Match(const char *HostName, const char *HostPat)
{
   const char *mval;
   int i, j, k;

// First check if this will match right away
//
   if (!strcmp(HostPat, HostName)) return true;

// Check for an asterisk do prefix/suffix match
//
   if ((mval = index(HostPat, (int)'*')))
      { i = mval - HostPat; mval++;
       k = strlen(HostName); j = strlen(mval);
       if ((i+j) > k
       || strncmp(HostName,      HostPat,i)
       || strncmp((HostName+k-j),mval,j)) return false;
       return 1;
      }

// Now check for host expansion
//
    i = strlen(HostPat);
    if (i && HostPat[i-1] != '+')
       {XrdNetAddr InetAddr[16];
        char hBuff[264];
        if (i >= sizeof(hBuff)) return false;
        strncpy(hBuff, HostPat, i-1);
        hBuff[i] = 0;
        if (!InetAddr[0].Set(hBuff, i, sizeof(InetAddr))) return false;
        while(i--) if ((mval = InetAddr[i].Name()) && !strcmp(mval, HostName))
                      return true;
       }

// No matches
//
   return false;
}
  
/******************************************************************************/
/*                                 P a r s e                                  */
/******************************************************************************/
  
bool XrdNetUtils::Parse(char *hSpec, char **hName, char **hNend,
                                     char **hPort, char **hPend)
{
   char *asep;

// Parse the specification
//
   if (*hSpec == '[')
      {if (!(*hNend = index(hSpec+1, ']'))) return false;
       *hName = hSpec+1; asep = (*hNend)+1;
      } else {
       *hName = hSpec;
       if (!(*hNend = index(hSpec, ':')))
          {*hNend = hSpec + strlen(hSpec);
           asep = *hNend;
          }
      }

// See if we have a port to parse
//
   if (*asep == ':')
      {*hPort = ++asep;
       while(isdigit(*asep)) asep++;
       if (*hPort == asep-1) return false;
       *hPend = asep;
      } else *hPort = *hPend = *hNend;

// All done
//
   return true;
}

/******************************************************************************/
/*                                  P o r t                                   */
/******************************************************************************/

int XrdNetUtils::Port(int fd, char **eText)
{
   union {struct sockaddr_storage data;
          struct sockaddr         addr;
         }                        Inet;
   struct sockaddr_in *ip = (struct sockaddr_in *)&Inet.addr;
   socklen_t slen = (socklen_t)sizeof(Inet);
   int rc;

   if ((rc = getsockname(fd, &Inet.addr, &slen)))
      {rc = errno;
       if (eText) setET(eText, errno);
       return -rc;
      }

   return static_cast<int>(ntohs(ip->sin_port));
}

/******************************************************************************/
/*                               P r o t o I D                                */
/******************************************************************************/

#define NET_IPPROTO_TCP 6

int XrdNetUtils::ProtoID(const char *pname)
{
#ifdef HAVE_PROTOR
    struct protoent pp;
    char buff[1024];
#else
    static XrdSysMutex protomutex;
    struct protoent *pp;
    int    protoid;
#endif

// Note that POSIX did include getprotobyname_r() in the last minute. Many
// platforms do not document this variant but just about all include it.
//
#ifdef __solaris__
    if (!getprotobyname_r(pname, &pp, buff, sizeof(buff))) 
       return NET_IPPROTO_TCP;
    return pp.p_proto;
#elif !defined(HAVE_PROTOR)
    protomutex.Lock();
    if (!(pp = getprotobyname(pname))) protoid = NET_IPPROTO_TCP;
       else protoid = pp->p_proto;
    protomutex.UnLock();
    return protoid;
#else
    struct protoent *ppp;
    if (getprotobyname_r(pname, &pp, buff, sizeof(buff), &ppp))
       return NET_IPPROTO_TCP;
    return pp.p_proto;
#endif
}

/******************************************************************************/
/*                              S e r v P o r t                               */
/******************************************************************************/
  
int XrdNetUtils::ServPort(const char *sName, bool isUDP, const char **eText)
{
   struct addrinfo *rP = 0, myHints = {0};
   int rc, portnum = 0;

// Fill out the hints
//
   myHints.ai_socktype = (isUDP ? SOCK_DGRAM : SOCK_STREAM);

// Try to find the port number
//
   rc = getaddrinfo(0, sName, &myHints, &rP);
   if (rc || !rP)
      {if (eText) *eText = (rc ? gai_strerror(rc) : "service not found");
       if (rP) freeaddrinfo(rP);
       return 0;
      }

// Return the port number
//
   portnum = int(ntohs(((struct sockaddr_in *)(rP->ai_addr))->sin_port));
   freeaddrinfo(rP);
   if (!portnum && eText) *eText = "service has no port";
   return portnum;
}
 
/******************************************************************************/
/* Private:                        s e t E T                                  */
/******************************************************************************/
  
int XrdNetUtils::setET(char **errtxt, int rc)
{
    if (rc) *errtxt = strerror(rc);
       else *errtxt = (char *)"unexpected error";
    return 0;
}
