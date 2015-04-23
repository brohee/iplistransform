/*
 * Copyright (c) 2004-2015 Bruno Rohée <bruno@rohee.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define _XOPEN_SOURCE 600

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#define GATHER_STATS /* comment this line if you don't want to */

void convert(FILE *, FILE *);
void outputRange(const char *, FILE *);
char *rangeToNetworks(const char *, const char *);
int maskLength(u_int32_t);
u_int32_t maskFromLength(int);
size_t formatNetwork(char *, u_int32_t, int);

#ifdef GATHER_STATS
/* some variables to gather statistics */
int lineCount = 0;
int singleAddressCount = 0;
int networkAddressCount = 0;
int complexRangeCount = 0;
#endif

/*
 * methlabs.org used to provide at http://methlabs.org/sync/ some address lists
 * in the following multi line format
 *
 * Some random junk:aaa.bbb.ccc.ddd-eee.fff.ggg.hhh
 *
 * where aaa.bbb.ccc.ddd-eee.fff.ggg.hhh represents a continuous range of
 * IPv4 addresses. Those ranges are not always a CIDR network so we may have
 * to split them  in a set of netmaskable entities. Also both ends of those
 * ranges may be identical, thus describing a single address.
 *
 * We convert that to a list of ipaddress/netmask, one per line,
 * suitable to use with OpenBSD's pfctl and doubtless other packages.
 *
 * This tool is written in a very portable way so should compile
 * and work on any system having an C89 compiler (and a strdup()
 * implementation).
 *
 * Comments may be sent to me at <bruno@rohee.org>.
 */
int main(int argc, char **argv) {
  FILE *in;
  FILE *out;

  if(argc != 3) {
    fprintf(stderr, "usage: %s infile outfile\n", argv[0]);
    return EXIT_FAILURE;
  }
  
  in = fopen(argv[1], "ro");
  out = fopen(argv[2], "w");

  convert(in, out);
  
  fclose(in);
  fclose(out);

#ifdef GATHER_STATS
  printf("Parsed %d lines, of which %d defined a single IP address,\n"
         "%d defined a CIDR network and %d defined a more complex range.\n",
         lineCount, singleAddressCount,
         networkAddressCount, complexRangeCount);
#endif

  return EXIT_SUCCESS;
}

void convert(FILE *in, FILE *out) {
  int currentChar = '\0';
  char savedLine[1024];
  char buf[sizeof "xxx.xxx.xxx.xxx-xxx.xxx.xxx.xxx"]; /* sized for the biggest
							 range we can get */
  int lineBufCount = 0;
  int bufCount = 0;

  int isReadingGarbage = 1;

  while((currentChar = fgetc(in)) != -1) {
    assert(lineBufCount >= 0);
    assert(lineBufCount < sizeof(savedLine));
    savedLine[lineBufCount++] = currentChar;
    if(isReadingGarbage) { /* we are in the non address part of the file */
      if(currentChar == '\r' || currentChar == '\n') {
	/* we met an end of line in a non interresting line, just forget it */
	/* FIXME upper comment is wrong */

	lineBufCount = 0;

#ifdef GATHER_STATS
	lineCount++;
#endif
      } else if(currentChar == ':') {
	isReadingGarbage = 0; /* switch to non garbage (address) mode */
	bufCount = 0;
      }
    } else {
      if(currentChar == '\r' || currentChar == '\n') {
#ifdef GATHER_STATS
	lineCount++;
#endif
	assert(lineBufCount > 1);
	savedLine[lineBufCount - 1] = '\0';
	lineBufCount = 0;
	buf[bufCount] = '\0'; /* terminate range string */
	isReadingGarbage = 1; /* reset to garbage reading */

	assert(savedLine[0] != '\r' && savedLine[0] != '\n');

	/* do not print empty line created by windows line ending */
	if(savedLine[0] != '\0')  
	  fprintf(out, "# %s\n", savedLine);

	outputRange(buf, out); /* output the range we just read */
	/* following condition needed because of some garbage sometime */
      } else if (!isdigit(currentChar) && currentChar != '-' &&
                 currentChar != '.') {
        isReadingGarbage = 1; /* reset to garbage reading */
      } else { /* looks like we're reading a range */
	buf[bufCount++] = currentChar;

	assert(bufCount <= sizeof buf);
      }
    }
  }
}

void outputRange(const char * buf, FILE * out) {
  char *secondHalf;
  char firstHalf[sizeof "xxx.xxx.xxx.xxx"];
  size_t firstHalfLen;

  secondHalf = strchr(buf, '-') + 1; /* find the beginning of end of range */

  if(!secondHalf)
    return; /* could not locate the '-', that's unexpected, so we skip */

  firstHalfLen = secondHalf - buf - 1;

  assert(firstHalfLen <= (sizeof firstHalf - 1));

  strncpy(firstHalf, buf, firstHalfLen); /* copy the first part
					    of the range */

  firstHalf[firstHalfLen] = '\0'; /* terminate the string */

  if(!strcmp(firstHalf, secondHalf)) { /* we've got identical bounds for
					  the range */
    fprintf(out, "%s\n", firstHalf);
#ifdef GATHER_STATS
    singleAddressCount++;
#endif
  }
  else {
    char *nets = rangeToNetworks(firstHalf, secondHalf);
    fprintf(out, "%s", nets);
    free(nets);
  }
}

/*
 * Converts a range to a set of networks.
 *
 * Return a malloc'd string that must be freed
 * by the caller. All networks are in CIDR addr/netmasklength
 * notation separated by \n, with an \n after the last one.
 */
char *rangeToNetworks(const char *begin, const char *end) {
  /* worst case is 62 networks in a range, but then two of them would
     be single hosts so this buffer is adequately sized */
  char * result = malloc(sizeof "xxx.xxx.xxx.xxx/yy" * 62);
  u_int32_t beginAddr = 0;
  u_int32_t endAddr = 0;
  u_int32_t mask;
  int maskLen;
  char * addr1 = strdup(begin);
  char * addr2 = strdup(end);
  char * temp, * temp2;
  char buf[sizeof "xxx.xxx.xxx.xxx/yy\n"];

  /* check memory allocation went well */  
  assert(addr1); assert(addr2); assert(result);

  result[0] = '\0';

  /* tokenize both strings and build integer representations for addresses */

  /* begin of range */
  temp = strchr(addr1, '.');
  *temp++ = '\0';

  beginAddr = atoi(addr1) << 24;

  temp2 = temp;
  temp = strchr(temp, '.');
  *temp++ = '\0';

  beginAddr |= atoi(temp2) << 16;

  temp2 = temp;
  temp = strchr(temp, '.');
  *temp++ = '\0';

  beginAddr |= atoi(temp2) << 8;
  beginAddr |= atoi(temp);
  
  /* end of range */
  temp = strchr(addr2, '.');
  *temp++ = '\0';

  endAddr = atoi(addr2) << 24;

  temp2 = temp;
  temp = strchr(temp, '.');
  *temp++ = '\0';

  endAddr |= atoi(temp2) << 16;

  temp2 = temp;
  temp = strchr(temp, '.');
  *temp++ = '\0';

  endAddr |= atoi(temp2) << 8;
  endAddr |= atoi(temp);

  /* if the range is in fact a network this will give us the real netmask */
  mask = ~(endAddr - beginAddr);

  /* here we check if the netmask is a real CIDR one */
  maskLen = maskLength(mask);
  
  if(maskLen == -1) { /* we have to split the range */
    u_int32_t tmpAddr = beginAddr;
    int writtenChars = 0;
    int toBeWritten;

    maskLen = 1; /* If we're here 0 did not work obviously */

    /*
     * split the range in a set of the biggest network we can find
     * until we reached the end of it.
     */
    while(tmpAddr < endAddr) {
      while((maskLen < 32) && ((tmpAddr | ~maskFromLength(maskLen)) > endAddr))
	maskLen++;

      tmpAddr |= ~maskFromLength(maskLen);

      toBeWritten = formatNetwork(buf, beginAddr, maskLen);

      assert(writtenChars + toBeWritten < sizeof "xxx.xxx.xxx.xxx" * 62 + 1); 

      /*
      printf("buf(%p)='%s'\n", buf, buf);
      printf("result(%p)='%s'\n", result, result);
      printf("writtenChars=%d\n", writtenChars); 
      */

      strcat(result + writtenChars, buf);
      writtenChars += toBeWritten;
      beginAddr = ++tmpAddr;
      maskLen = 1;
    }

    assert(writtenChars < sizeof "xxx.xxx.xxx.xxx" * 62 + 1);

#ifdef GATHER_STATS
    complexRangeCount++;
#endif
  } else { /* the range was in fact a CIDR network */
    sprintf(buf, "%s/%d\n", begin, maskLen);
    strcat(result, buf);

#ifdef GATHER_STATS
    networkAddressCount++;
#endif
  }

  free(addr1);
  free(addr2);

  return result;
}

/*
 * Return the length of a CIDR netmask or -1 if the 
 * given netmask is not CIDR.
 */
int maskLength(u_int32_t mask) {
  int i;
  int foundAZero = 0;
  int len = 0;

  for (i = 31; i >= 0; i--) {
    if (((1 << i) & mask) == (1 << i)) {
      if (foundAZero)
	return -1; /* discontinuous netmask */
      
      len++;
    } else
      foundAZero = 1;
  }
  
  assert(len >= 0 && len <= 32);

  return len;
}

/*
 * Return a netmask given its length.
 */
u_int32_t maskFromLength(int length) {
  u_int32_t mask = 0;
  int tmp = length;

  assert(length >= 0 && length <= 32);

  while(tmp--)
    mask |= 1 << tmp;

  mask <<= 32 - length;

  return mask;
}

/*
 * Put a formated string representing the network
 * described by addr and maskLen in buf. 
 *
 * Returns the length of the string contained in buf after formatting.
 */
size_t formatNetwork(char *buf, u_int32_t addr, int maskLen) {
  return sprintf(buf, "%d.%d.%d.%d/%d\n", addr >> 24,
                                          (addr >> 16) & 0xFF,
	                                  (addr >> 8) & 0xFF,
                                          addr & 0xFF,
                                          maskLen);
}
