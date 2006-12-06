# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Fri Oct  6 21:37:30 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
# May be distributed under the terms of the GNU GPL.
#

include ./make_include
include ./MCONFIG

SUBDIRS=po \
	lib \
	getopt-1.0.3b \
	disk-utils \
	games \
	login-utils \
	misc-utils \
	mount \
	fdisk \
	clock \
	sys-utils \
	text-utils \
	kbd

.PHONEY: all install clean now
all:	defines.h
	@for subdir in $(SUBDIRS); do \
		(cd $$subdir && $(MAKE) $@) || exit 1; \
	done

defines.h make_include:
	./configure

now:
	touch defines.h

$(SUBDIRS): defines.h now
	cd $@ && $(MAKE)

install:
	@if [ "`whoami`" = "root" ]; then umask 022; fi
	@for subdir in $(SUBDIRS); do \
		(cd $$subdir && $(MAKE) $@) || exit 1; \
	done

clean:
	-rm -f *.o *~ omake conftest conftest.c core
	@for subdir in $(SUBDIRS); do \
		(cd $$subdir && $(MAKE) $@) || exit 1; \
	done

distclean: make_include clean
	cd po && make distclean
	-rm -f defines.h make_include

#
# dist:
#	(cd /tmp; \
#	rm -rf /tmp/util-linux-$(VERSION); \
#	cvs export -fNd util-linux-$(VERSION) -r HEAD util-linux; \
#	cd util-linux-$(VERSION); \
#	find -type d | xargs chmod 755; \
#	find -type f | xargs chmod 644; \
#	find -type d | xargs chown root:root; \
#	find -type f | xargs chown root:root; \
#	cd ..; \
#	GZIP=-9 tar cvvzf util-linux-$(VERSION).tar.gz util-linux-$(VERSION); \
#	cp -p util-linux-$(VERSION)/LSM util-linux-$(VERSION).lsm; \
#	cp -p util-linux-$(VERSION)/ANNOUNCE util-linux-$(VERSION).Announce; \
#	echo Done.)
#

