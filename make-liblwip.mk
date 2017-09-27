

#
# Copyright (c) 2001, 2002 Swedish Institute of Computer Science.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
# SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.
#
# This file is part of the lwIP TCP/IP stack.
#
# Author: Adam Dunkels <adam@sics.se>
#

CONTRIBDIR=ext/lwip-contrib
LWIPARCH=$(CONTRIBDIR)/ports/unix

#Set this to where you have the lwip core module checked out from CVS
#default assumes it's a dir named lwip at the same level as the contrib module
LWIPDIR=ext/lwip/src

CCDEP=clang++
# Automagically pick clang or gcc, with preference for clang
# This is only done if we have not overridden these with an environment or CLI variable
ifeq ($(origin CCX),default)
	CCX=$(shell if [ -e /usr/bin/clang++ ]; then echo clang++; else echo g++; fi)
endif

LWIPINCLUDES:=-I$(LWIPDIR)/include -I$(LWIPARCH) -I$(LWIPDIR) -I. -Iext -Iinclude
CFLAGS=-Wno-format -Wno-deprecated -O3 -g -Wall -fPIC
CFLAGS+=-DLWIP_IPV4 -DLWIP_IPV6=0 -DIPv4 -DLWIP_DEBUG=1 $(LWIPINCLUDES)

UNIXLIB=liblwip.a

all: $(UNIXLIB)
.PHONY: all

include $(LWIPDIR)/Filelists.mk

# ARCHFILES: Architecture specific files.
ARCHFILES=$(wildcard $(LWIPARCH)/port/*.c $(LWIPARCH)/*.c $(LWIPARCH)tapif.c $(LWIPARCH)/netif/list.c $(LWIPARCH)/netif/tcpdump.c)

LWIPNOAPPSFILES+=$(ARCHFILES)
LWIPNOAPPSFILESW=$(wildcard $(LWIPNOAPPSFILES))
LWIPNOAPPSOBJS=$(notdir $(LWIPNOAPPSFILESW:.c=.o))

CCX=clang++

%.o:
	$(CCX) $(CFLAGS) -c $(<:.o=.c)

clean:
	rm -f *.o $(LWIPNOAPPSOBJS) *.s .depend* *.core core

depend dep: .depend

include .depend

$(UNIXLIB): $(LWIPNOAPPSOBJS)
	$(CCX) $(CFLAGS) -g -nostartfiles -shared -o obj/$@ $^

.depend: $(LWIPNOAPPSFILES)
	$(CCX) $(CFLAGS) -MM $^ > .depend || rm -f .depend