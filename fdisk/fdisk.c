/* fdisk.c -- Partition table manipulator for Linux.
 *
 * Copyright (C) 1992  A. V. Le Blanc (LeBlanc@mcc.ac.uk)
 *
 * This program is free software.  You can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation: either version 1 or
 * (at your option) any later version.
 *
 * Before Linux version 0.95c, this program requires a kernel patch.
 *
 * Modified, Tue Feb  2 18:46:49 1993, faith@cs.unc.edu to better support SCSI.
 * Modified, Sat Feb 27 18:11:27 1993, faith@cs.unc.edu: added extfs support.
 * Modified, Sat Mar  6 10:14:12 1993, faith@cs.unc.edu: added more comments.
 * Modified, Sat Mar  6 12:25:45 1993, faith@cs.unc.edu:
 *	Added patches from Michael Bischoff (i1041905@ws.rz.tu-bs.de
 *	or mbi@mo.math.nat.tu-bs.de) to fix the following problems:
 *	1) Incorrect mapping of head/sector/cylinder to absolute sector
 *	2) Odd sector count causes one sector to be lost
 * Modified, Sat Mar  6 12:25:52 1993, faith@cs.unc.edu: improved verification.
 * Modified, Sat Apr 17 15:00:00 1993, LeBlanc@mcc.ac.uk: add -s, fix -l.
 * Modified, Sat Apr 24 10:00:00 1993, LeBlanc@mcc.ac.uk: fix overlap bug.
 * Modified, Wed May  5 21:30:00 1993, LeBlanc@mcc.ac.uk: errors to stderr.
 * Modified, Mon Mar 21 20:00:00 1994, LeBlanc@mcc.ac.uk:
 *	more stderr for messages, avoid division by 0, and
 *	give reboot message only if ioctl(fd, BLKRRPART) fails.
 * Modified, Mon Apr 25 01:01:05 1994, martin@cs.unc.edu:
 *    1) Added support for DOS, OS/2, ... compatibility.  We should be able
 *       use this fdisk to partition our drives for other operating systems.
 *    2) Added a print the raw data in the partition table command.
 * Modified, Wed Jun 22 21:05:30 1994, faith@cs.unc.edu:
 *    Added/changed a few partition type names to conform to cfdisk.
 *    (suggested by Sujal, smpatel@wam.umd.edu)
 * Modified 3/5/95 leisner@sdsp.mc.xerox.com -- on -l only open
 *    devices RDONLY (instead of RDWR).  This allows you to
 *    have the disks as rw-r----- with group disk (and if you
 *    want is safe to setguid fdisk to disk).
 * Modified Sat Mar 11 10:02 1995 with more partition types, faith@cs.unc.edu
 * Modified, Thu May  4 01:11:45 1995, esr@snark.thyrsus.com:
 *	It's user-interface cleanup time.
 *	Actual error messages for out-of-bounds values (what a concept!).
 *	Enable read-only access to partition table for learners.
 *	Smart defaults for most numeric prompts.
 * Fixed a bug preventing a partition from crossing cylinder 8064, aeb, 950801.
 * Read partition table twice to avoid kernel bug
 *     (from Daniel Quinlan <quinlan@yggdrasil.com>), Tue Sep 26 10:25:28 1995
 * Modified, Sat Jul  1 23:43:16 MET DST 1995, fasten@cs.bonn.edu:
 *      editor for NetBSD/i386 (and Linux/Alpha?) disklabels.
 * Tue Sep 26 17:07:54 1995: More patches from aeb.  Fix segfaults, all >4GB.
 *   Don't destroy random data if extd partition starts past 4GB, aeb, 950818.
 *   Don't segfault on bad partition created by previous fdisk.
 * Fixed for using variable blocksizes (needed by magnet-optical drive-patch)
 *   (from orschaer@cip.informatik.uni-erlangen.de)
 * Modified, Fri Jul 14 11:13:35 MET DST 1996, jj@sunsite.mff.cuni.cz:
 *      editor for Sun disklabels.
 * Modified, Wed Jul  3 10:14:17 MET DST 1996, jj@sunsite.mff.cuni.cz:
 *      support for Sun floppies 
 * Modified, Thu Jul 24 16:42:33 MET DST 1997, fasten@shw.com:
 *   LINUX_EXTENDED support
 * Added Windows 95 partition types, aeb.
 * Fixed a bug described by johnf@whitsunday.net.au, aeb, 980408.
 *   [There are lots of other bugs - nobody should use this program]
 *   [cfdisk on the other hand is nice and correct]
 * Try to avoid reading a CD-ROM.
 * Do not print Begin column -- it confuses too many people -- aeb, 980610.
 * Modified, Sat Oct  3 14:40:17 MET DST 1998, ANeuper@GUUG.de
 *	Support SGI's partitioning -- an, 980930.
 * Do the verify using LBA, not CHS, limits -- aeb, 981206.
 * Corrected single-cylinder partition creating a little, now that
 * ankry@mif.pg.gda.pl pointed out a bug; there are more bugs -- aeb, 990214.
 * 
 * Sat Mar 20 09:31:05 EST 1999 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *	Internationalization
 *
 * Corrected deleting last logical partition -- aeb, 990430.
 * Removed all assumptions on file names -- aeb, 990709
 *   [modprobe gave ugly error messages, and the number of devices to probe
 *    increased all the time: hda, sda, eda, rd/c0d0, ida/c0d0, ...
 *    Also partition naming was very ugly.]
 *
 * Corrected a bug where creating an extended hda3, say, then a logical hda5
 * that does not start at the beginning of hda3, then a logical hda6 that does
 * start at the beginning of hda3 would wipe out the partition table describing
 * hda5.  [Patch by Klaus G. Wagner" <kgw@suse.de>] -- aeb, 990711
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <setjmp.h>
#include <errno.h>
#include <getopt.h>
#include "nls.h"

#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/hdreg.h>       /* for HDIO_GETGEO */
#include <linux/fs.h>          /* for BLKRRPART, BLKGETSIZE */

#include "common.h"
#include "fdisk.h"

#include "fdisksunlabel.h"
#include "fdisksgilabel.h"
#include "fdiskaixlabel.h"

#include "../defines.h"
#ifdef HAVE_blkpg_h
#include <linux/blkpg.h>
#endif

#define hex_val(c)	({ \
				char _c = (c); \
				isdigit(_c) ? _c - '0' : \
				tolower(_c) + 10 - 'a'; \
			})


#define LINE_LENGTH	80
#define offset(b, n)	((struct partition *)((b) + 0x1be + \
				(n) * sizeof(struct partition)))
#define sector(s)	((s) & 0x3f)
#define cylinder(s, c)	((c) | (((s) & 0xc0) << 2))

#define hsc2sector(h,s,c) (sector(s) - 1 + sectors * \
				((h) + heads * cylinder(s,c)))
#define set_hsc(h,s,c,sector) { \
				s = sector % sectors + 1;	\
				sector /= sectors;	\
				h = sector % heads;	\
				sector /= heads;	\
				c = sector & 0xff;	\
				s |= (sector >> 2) & 0xc0;	\
			}

/* A valid partition table sector ends in 0x55 0xaa */
unsigned int
part_table_flag(char *b) {
	return ((uint) b[510]) + (((uint) b[511]) << 8);
}

int
valid_part_table_flag(unsigned char *b) {
	return (b[510] == 0x55 && b[511] == 0xaa);
}

void
write_part_table_flag(char *b) {
	b[510] = 0x55;
	b[511] = 0xaa;
}

/* start_sect and nr_sects are stored little endian on all machines */
/* moreover, they are not aligned correctly */
void
store4_little_endian(unsigned char *cp, unsigned int val) {
	cp[0] = (val & 0xff);
	cp[1] = ((val >> 8) & 0xff);
	cp[2] = ((val >> 16) & 0xff);
	cp[3] = ((val >> 24) & 0xff);
}

unsigned int
read4_little_endian(unsigned char *cp) {
	return (uint)(cp[0]) + ((uint)(cp[1]) << 8)
		+ ((uint)(cp[2]) << 16) + ((uint)(cp[3]) << 24);
}

void
set_start_sect(struct partition *p, unsigned int start_sect) {
	store4_little_endian(p->start4, start_sect);
}

unsigned int
get_start_sect(struct partition *p) {
	return read4_little_endian(p->start4);
}

void
set_nr_sects(struct partition *p, unsigned int nr_sects) {
	store4_little_endian(p->size4, nr_sects);
}

unsigned int
get_nr_sects(struct partition *p) {
	return read4_little_endian(p->size4);
}

/* normally O_RDWR, -l option gives O_RDONLY */
static int type_open = O_RDWR;

char	*disk_device,			/* must be specified */
	*line_ptr,			/* interactive input */
	line_buffer[LINE_LENGTH],
	changed[MAXIMUM_PARTS],		/* marks changed buffers */
	buffer[MAX_SECTOR_SIZE],	/* first four partitions */
	*buffers[MAXIMUM_PARTS]		/* pointers to buffers */
		= {buffer, buffer, buffer, buffer};

int	fd,				/* the disk */
	ext_index,			/* the prime extended partition */
	listing = 0,			/* no aborts for fdisk -l */
	nowarn = 0,			/* no warnings for fdisk -l/-s */
	dos_compatible_flag = ~0,
	partitions = 4;			/* maximum partition + 1 */

uint	heads,
	sectors,
	cylinders,
	sector_size = DEFAULT_SECTOR_SIZE,
	sector_offset = 1,
	units_per_sector = 1,
	display_in_cyl_units = 1,
	extended_offset = 0,		/* offset of link pointers */
	offsets[MAXIMUM_PARTS] = {0, 0, 0, 0};

int     sun_label = 0;                  /* looking at sun disklabel */
int	sgi_label = 0;			/* looking at sgi disklabel */
int	aix_label = 0;			/* looking at aix disklabel */

struct	partition *part_table[MAXIMUM_PARTS]	/* partitions */
		= {offset(buffer, 0), offset(buffer, 1),
		offset(buffer, 2), offset(buffer, 3)},
	*ext_pointers[MAXIMUM_PARTS]		/* link pointers */
		= {NULL, NULL, NULL, NULL};

jmp_buf listingbuf;

void fatal(enum failure why)
{
	char	error[LINE_LENGTH],
		*message = error;

	if (listing) {
		close(fd);
		longjmp(listingbuf, 1);
	}

	switch (why) {
		case usage: message = _(
"Usage: fdisk [-b SSZ] [-u] DISK     Change partition table\n"
"       fdisk -l [-b SSZ] [-u] DISK  List partition table(s)\n"
"       fdisk -s PARTITION           Give partition size(s) in blocks\n"
"       fdisk -v                     Give fdisk version\n"
"Here DISK is something like /dev/hdb or /dev/sda\n"
"and PARTITION is something like /dev/hda7\n"
"-u: give Start and End in sector (instead of cylinder) units\n"
"-b 2048: (for certain MO drives) use 2048-byte sectors\n");
			break;
		case usage2:
		  /* msg in cases where fdisk used to probe */
			message = _(
"Usage: fdisk [-l] [-b SSZ] [-u] device\n"
"E.g.: fdisk /dev/hda  (for the first IDE disk)\n"
"  or: fdisk /dev/sdc  (for the third SCSI disk)\n"
"  or: fdisk /dev/eda  (for the first PS/2 ESDI drive)\n"
"  or: fdisk /dev/rd/c0d0  or: fdisk /dev/ida/c0d0  (for RAID devices)\n"
"  ...\n");
			break;
		case unable_to_open:
			sprintf(error, _("Unable to open %s\n"), disk_device);
			break;
		case unable_to_read:
			sprintf(error, _("Unable to read %s\n"), disk_device);
			break;
		case unable_to_seek:
			sprintf(error, _("Unable to seek on %s\n"),disk_device);
			break;
		case unable_to_write:
			sprintf(error, _("Unable to write %s\n"), disk_device);
			break;
		case ioctl_error:
			sprintf(error, _("BLKGETSIZE ioctl failed on %s\n"),
				disk_device);
			break;
		case out_of_memory:
			message = _("Unable to allocate any more memory\n");
			break;
		default: message = _("Fatal error\n");
	}

	fputc('\n', stderr);
	fputs(message, stderr);
	exit(1);
}

void menu(void)
{
	if (sun_label) {
	   puts(_("Command action"));
	   puts(_("   a   toggle a read only flag")); 		/* sun */
	   puts(_("   b   edit bsd disklabel"));
	   puts(_("   c   toggle the mountable flag"));		/* sun */
	   puts(_("   d   delete a partition"));
	   puts(_("   l   list known partition types"));
	   puts(_("   m   print this menu"));
	   puts(_("   n   add a new partition"));
	   puts(_("   o   create a new empty DOS partition table"));
	   puts(_("   p   print the partition table"));
	   puts(_("   q   quit without saving changes"));
	   puts(_("   s   create a new empty Sun disklabel"));	/* sun */
	   puts(_("   t   change a partition's system id"));
	   puts(_("   u   change display/entry units"));
	   puts(_("   v   verify the partition table"));
	   puts(_("   w   write table to disk and exit"));
	   puts(_("   x   extra functionality (experts only)"));
	}
	else if(sgi_label) {
	   puts(_("Command action"));
	   puts(_("   a   select bootable partition"));    /* sgi flavour */
	   puts(_("   b   edit bootfile entry"));          /* sgi */
	   puts(_("   c   select sgi swap partition"));    /* sgi flavour */
	   puts(_("   d   delete a partition"));
	   puts(_("   l   list known partition types"));
	   puts(_("   m   print this menu"));
	   puts(_("   n   add a new partition"));
	   puts(_("   o   create a new empty DOS partition table"));
	   puts(_("   p   print the partition table"));
	   puts(_("   q   quit without saving changes"));
	   puts(_("   s   create a new empty Sun disklabel"));	/* sun */
	   puts(_("   t   change a partition's system id"));
	   puts(_("   u   change display/entry units"));
	   puts(_("   v   verify the partition table"));
	   puts(_("   w   write table to disk and exit"));
	}
	else if(aix_label) {
	   puts(_("Command action"));
	   puts(_("   m   print this menu"));
	   puts(_("   o   create a new empty DOS partition table"));
	   puts(_("   q   quit without saving changes"));
	   puts(_("   s   create a new empty Sun disklabel"));	/* sun */
	}
	else {
	   puts(_("Command action"));
	   puts(_("   a   toggle a bootable flag"));
	   puts(_("   b   edit bsd disklabel"));
	   puts(_("   c   toggle the dos compatibility flag"));
	   puts(_("   d   delete a partition"));
	   puts(_("   l   list known partition types"));
	   puts(_("   m   print this menu"));
	   puts(_("   n   add a new partition"));
	   puts(_("   o   create a new empty DOS partition table"));
	   puts(_("   p   print the partition table"));
	   puts(_("   q   quit without saving changes"));
	   puts(_("   s   create a new empty Sun disklabel"));	/* sun */
	   puts(_("   t   change a partition's system id"));
	   puts(_("   u   change display/entry units"));
	   puts(_("   v   verify the partition table"));
	   puts(_("   w   write table to disk and exit"));
	   puts(_("   x   extra functionality (experts only)"));
	}
}

void xmenu(void)
{
	if (sun_label) {
	   puts(_("Command action"));
	   puts(_("   a   change number of alternate cylinders"));       /*sun*/
	   puts(_("   c   change number of cylinders"));
	   puts(_("   d   print the raw data in the partition table"));
	   puts(_("   e   change number of extra sectors per cylinder"));/*sun*/
	   puts(_("   h   change number of heads"));
	   puts(_("   i   change interleave factor"));			/*sun*/
	   puts(_("   o   change rotation speed (rpm)"));		/*sun*/
	   puts(_("   m   print this menu"));
	   puts(_("   p   print the partition table"));
	   puts(_("   q   quit without saving changes"));
	   puts(_("   r   return to main menu"));
	   puts(_("   s   change number of sectors/track"));
	   puts(_("   v   verify the partition table"));
	   puts(_("   w   write table to disk and exit"));
	   puts(_("   y   change number of physical cylinders"));	/*sun*/
	}
	else {
	   puts(_("Command action"));
	   puts(_("   b   move beginning of data in a partition")); /* !sun */
	   puts(_("   c   change number of cylinders"));
	   puts(_("   d   print the raw data in the partition table"));
	   puts(_("   e   list extended partitions"));		/* !sun */
	   puts(_("   g   create an IRIX partition table"));	/* sgi */
	   puts(_("   h   change number of heads"));
	   puts(_("   m   print this menu"));
	   puts(_("   p   print the partition table"));
	   puts(_("   q   quit without saving changes"));
	   puts(_("   r   return to main menu"));
	   puts(_("   s   change number of sectors/track"));
	   puts(_("   v   verify the partition table"));
	   puts(_("   w   write table to disk and exit"));
	 }
}

int
get_sysid(int i) {
	return (
		sun_label ? sunlabel->infos[i].id :
		sgi_label ? sgi_get_sysid(i) : part_table[i]->sys_ind);
}

struct systypes *
get_sys_types(void) {
	return (
		sun_label ? sun_sys_types :
		sgi_label ? sgi_sys_types : i386_sys_types);
}

char *partition_type(unsigned char type)
{
	int i;
	struct systypes *types = get_sys_types();

	for (i=0; types[i].name; i++)
		if (types[i].type == type)
			return _(types[i].name);

	return NULL;
}

void list_types(struct systypes *sys)
{
	uint last[4], done = 0, next = 0, size;
	int i;

	for (i = 0; sys[i].name; i++);
	size = i;

	for (i = 3; i >= 0; i--)
		last[3 - i] = done += (size + i - done) / (i + 1);
	i = done = 0;

	do {
		printf("%c%2x  %-15.15s", i ? ' ' : '\n',
		        sys[next].type, _(sys[next].name));
 		next = last[i++] + done;
		if (i > 3 || next >= last[i]) {
			i = 0;
			next = ++done;
		}
	} while (done < last[0]);
	putchar('\n');
}

void clear_partition(struct partition *p)
{
	p->boot_ind = 0;
	p->head = 0;
	p->sector = 0;
	p->cyl = 0;
	p->sys_ind = 0;
	p->end_head = 0;
	p->end_sector = 0;
	p->end_cyl = 0;
	set_start_sect(p,0);
	set_nr_sects(p,0);
}

void set_partition(int i, struct partition *p, uint start, uint stop,
	int sysid, uint offset)
{
	p->boot_ind = 0;
	p->sys_ind = sysid;
	set_start_sect(p, start - offset);
	set_nr_sects(p, stop - start + 1);
	if (dos_compatible_flag && (start/(sectors*heads) > 1023))
		start = heads*sectors*1024 - 1;
	set_hsc(p->head, p->sector, p->cyl, start);
	if (dos_compatible_flag && (stop/(sectors*heads) > 1023))
		stop = heads*sectors*1024 - 1;
	set_hsc(p->end_head, p->end_sector, p->end_cyl, stop);
	changed[i] = 1;
}

int test_c(char **m, char *mesg)
{
	int val = 0;
	if (!*m)
		fprintf(stderr, _("You must set"));
	else {
		fprintf(stderr, " %s", *m);
		val = 1;
	}
	*m = mesg;
	return val;
}

int warn_geometry(void)
{
	char *m = NULL;
	int prev = 0;
	if (!heads)
		prev = test_c(&m, _("heads"));
	if (!sectors)
		prev = test_c(&m, _("sectors"));
	if (!cylinders)
		prev = test_c(&m, _("cylinders"));
	if (!m)
		return 0;
	fprintf(stderr,
		_("%s%s.\nYou can do this from the extra functions menu.\n"),
		prev ? _(" and ") : " ", m);
	return 1;
}

void update_units(void)
{
	int cyl_units = heads * sectors;

	if (display_in_cyl_units && cyl_units)
		units_per_sector = cyl_units;
	else
		units_per_sector = 1; 	/* in sectors */
}

void warn_cylinders(void)
{
	if (!sun_label && !sgi_label && cylinders > 1024 && !nowarn)
		fprintf(stderr, "\n\
The number of cylinders for this disk is set to %d.\n\
There is nothing wrong with that, but this is larger than 1024,\n\
and could in certain setups cause problems with:\n\
1) software that runs at boot time (e.g., LILO)\n\
2) booting and partitioning software from other OSs\n\
   (e.g., DOS FDISK, OS/2 FDISK)\n",
			cylinders);
}

void read_extended(struct partition *p)
{
	int i;
	struct partition *q;

	ext_pointers[ext_index] = part_table[ext_index];
	if (!get_start_sect(p))
		fprintf(stderr, _("Bad offset in primary extended partition\n"));
	else while (IS_EXTENDED (p->sys_ind)) {
		if (partitions >= MAXIMUM_PARTS) {
			/* This is not a Linux restriction, but
			   this program uses arrays of size MAXIMUM_PARTS.
			   Do not try to `improve' this test. */
			fprintf(stderr,
				_("Warning: deleting partitions after %d\n"),
				partitions);
			clear_partition(ext_pointers[partitions - 1]);
			changed[partitions - 1] = 1;
			return;
		}
		offsets[partitions] = extended_offset + get_start_sect(p);
		if (!extended_offset)
			extended_offset = get_start_sect(p);
		if (ext2_llseek(fd, (ext2_loff_t)offsets[partitions]
			       * sector_size, SEEK_SET) < 0)
			fatal(unable_to_seek);
		if (!(buffers[partitions] = (char *) malloc(sector_size)))
			fatal(out_of_memory);
		if (sector_size != read(fd, buffers[partitions], sector_size))
			fatal(unable_to_read);
		part_table[partitions] = ext_pointers[partitions] = NULL;
		q = p = offset(buffers[partitions], 0);
		for (i = 0; i < 4; i++, p++) {
			if (IS_EXTENDED (p->sys_ind)) {
				if (ext_pointers[partitions])
					fprintf(stderr, _("Warning: extra link "
						"pointer in partition table "
						"%d\n"), partitions + 1);
				else
					ext_pointers[partitions] = p;
			} else if (p->sys_ind) {
				if (part_table[partitions])
					fprintf(stderr,
						_("Warning: ignoring extra data "
						"in partition table %d\n"),
						partitions + 1);
				else
					part_table[partitions] = p;
			}
		}
		if (!part_table[partitions]) {
			if (q != ext_pointers[partitions])
				part_table[partitions] = q;
			else part_table[partitions] = q + 1;
		}
		if (!ext_pointers[partitions]) {
			if (q != part_table[partitions])
				ext_pointers[partitions] = q;
			else ext_pointers[partitions] = q + 1;
		}
		p = ext_pointers[partitions++];
	}
}

void create_doslabel(void)
{
	int i;

	fprintf(stderr,
	_("Building a new DOS disklabel. Changes will remain in memory only,\n"
	  "until you decide to write them. After that, of course, the previous\n"
	  "content won't be recoverable.\n\n"));

	sun_nolabel();  /* otherwise always recognised as sun */
	sgi_nolabel();  /* otherwise always recognised as sgi */

	write_part_table_flag(buffer);
	for (i = 0; i < 4; i++)
	    clear_partition(part_table[i]);
	for (i = 1; i < MAXIMUM_PARTS; i++)
	    changed[i] = 0;
	changed[0] = 1;
	get_boot(create_empty);
}

/*
 * Read MBR.  Returns:
 *   -1: no 0xaa55 flag present (possibly entire disk BSD)
 *    0: found or created label
 */
int get_boot(enum action what)
{
	int i, sec_fac;
	struct hd_geometry geometry;

	partitions = 4;
	sec_fac = sector_size / 512;

	if (what == create_empty)
		goto got_table;		/* skip reading disk */

	if ((fd = open(disk_device, type_open)) < 0) {
	    if ((fd = open(disk_device, O_RDONLY)) < 0)
		fatal(unable_to_open);
	    else
		printf(_("You will not be able to write the partition table.\n"));
	}

#if defined(BLKSSZGET) && defined(HAVE_blkpg_h)
	/* For a short while BLKSSZGET gave a wrong sector size */
	{ int arg;
	if (ioctl(fd, BLKSSZGET, &arg) == 0)
		sector_size = arg;
	if (sector_size != DEFAULT_SECTOR_SIZE)
		printf(_("Note: sector size is %d (not %d)\n"),
		       sector_size, DEFAULT_SECTOR_SIZE);
	}
#endif

	guess_device_type(fd);

	if (sector_size != read(fd, buffer, sector_size))
		fatal(unable_to_read);

#ifdef HDIO_REQ
	if (!ioctl(fd, HDIO_REQ, &geometry)) {
#else
	if (!ioctl(fd, HDIO_GETGEO, &geometry)) {
#endif
		heads = geometry.heads;
		sectors = geometry.sectors;
		cylinders = geometry.cylinders;
		cylinders /= sec_fac; 	/* do not round up */
		if (dos_compatible_flag)
			sector_offset = sectors;
	} else {
		long longsectors;
		if (!ioctl(fd, BLKGETSIZE, &longsectors)) {
			heads = 1;
			cylinders = 1;
			sectors = longsectors / sec_fac;
		} else {
			heads = cylinders = sectors = 0;
		}
	}
	update_units();

got_table:

	if (check_sun_label())
		return 0;

	if (check_sgi_label())
		return 0;

	if (check_aix_label())
		return 0;

	if (!valid_part_table_flag(buffer)) {
		switch(what) {
		case fdisk:
			fprintf(stderr,
				_("Device contains neither a valid DOS partition"
			 	  " table, nor Sun or SGI disklabel\n"));
#ifdef __sparc__
			create_sunlabel();
#else
			create_doslabel();
#endif
			return 0;
		case require:
			return -1;
		case try_only:
		        return -1;
		case create_empty:
			break;
		}

		fprintf(stderr, _("Internal error\n"));
		exit(1);
	}

	warn_cylinders();
	warn_geometry();

	for (i = 0; i < 4; i++)
		if(IS_EXTENDED (part_table[i]->sys_ind)) {
			if (partitions != 4)
				fprintf(stderr, _("Ignoring extra extended "
					"partition %d\n"), i + 1);
			else read_extended(part_table[ext_index = i]);
		}
	for (i = 3; i < partitions; i++)
		if (!valid_part_table_flag(buffers[i])) {
			fprintf(stderr,
				_("Warning: invalid flag 0x%04x of partition "
				"table %d will be corrected by w(rite)\n"),
				part_table_flag(buffers[i]), i + 1);
			changed[i] = 1;
		}

	return 0;
}

/* read line; return 0 or first char */
int
read_line(void)
{
	static int got_eof = 0;

	line_ptr = line_buffer;
	if (!fgets(line_buffer, LINE_LENGTH, stdin)) {
		if (feof(stdin))
			got_eof++; 	/* user typed ^D ? */
		if (got_eof >= 3) {
			fflush(stdout);
			fprintf(stderr, _("\ngot EOF thrice - exiting..\n"));
			exit(1);
		}
		return 0;
	}
	while (*line_ptr && !isgraph(*line_ptr))
		line_ptr++;
	return *line_ptr;
}

char
read_char(char *mesg)
{
	do {
		fputs(mesg, stdout);
	} while (!read_line());
	return *line_ptr;
}

char
read_chars(char *mesg)
{
        fputs(mesg, stdout);
        if (!read_line()) {
		*line_ptr = '\n';
		line_ptr[1] = 0;
	}
	return *line_ptr;
}

int
read_hex(struct systypes *sys)
{
        int hex;

        while (1)
        {
           read_char(_("Hex code (type L to list codes): "));
           if (tolower(*line_ptr) == 'l')
               list_types(sys);
	   else if (isxdigit (*line_ptr))
	   {
	      hex = 0;
	      do
		 hex = hex << 4 | hex_val(*line_ptr++);
	      while (isxdigit(*line_ptr));
	      return hex;
	   }
        }
}

/*
 * Print the message MESG, then read an integer between LOW and HIGH (inclusive).
 * If the user hits Enter, DFLT is returned.
 * Answers like +10 are interpreted as offsets from BASE.
 *
 * There is no default if DFLT is not between LOW and HIGH.
 */
uint
read_int(uint low, uint dflt, uint high, uint base, char *mesg)
{
	uint i;
	int default_ok = 1;
	static char *ms = NULL;
	static int mslen = 0;

	if (!ms || strlen(mesg)+50 > mslen) {
		mslen = strlen(mesg)+100;
		if (!(ms = realloc(ms,mslen)))
			fatal(out_of_memory);
	}

	if (dflt < low || dflt > high)
		default_ok = 0;

	if (default_ok)
		sprintf(ms, _("%s (%d-%d, default %d): "), mesg, low, high, dflt);
	else
		sprintf(ms, "%s (%d-%d): ", mesg, low, high);

	while (1) {
		int use_default = default_ok;

		/* ask question and read answer */
		while (read_chars(ms) != '\n' && !isdigit(*line_ptr)
		       && *line_ptr != '-' && *line_ptr != '+')
			continue;

		if (*line_ptr == '+' || *line_ptr == '-') {
			i = atoi(line_ptr+1);
			if (*line_ptr == '-')
				i = -i;
 			while (isdigit(*++line_ptr))
				use_default = 0;
			switch (*line_ptr) {
				case 'c':
				case 'C':
					if (!display_in_cyl_units)
						i *= heads * sectors;
					break;
				case 'k':
				case 'K':
					i *= 2;
					i /= (sector_size / 512);
					i /= units_per_sector;
					break;
				case 'm':
				case 'M':
					i *= 2048;
					i /= (sector_size / 512);
					i /= units_per_sector;
					break;
				case 'g':
				case 'G':
					i *= 2048000;
					i /= (sector_size / 512);
					i /= units_per_sector;
					break;
				default:
					break;
			}
			i += base;
		} else {
			i = atoi(line_ptr);
			while (isdigit(*line_ptr)) {
				line_ptr++;
				use_default = 0;
			}
		}
		if (use_default)
		    printf(_("Using default value %d\n"), i = dflt);
		if (i >= low && i <= high)
			break;
		else
			printf(_("Value out of range.\n"));
	}
	return i;
}

int get_partition(int warn, int max)
{
	int i = read_int(1, 0, max, 0, _("Partition number")) - 1;

	if (warn && (
	     (!sun_label && !sgi_label && !part_table[i]->sys_ind)
	     || (sun_label &&
		(!sunlabel->partitions[i].num_sectors ||
		 !sunlabel->infos[i].id))
	     || (sgi_label && (!sgi_get_num_sectors(i)))
	)) fprintf(stderr, _("Warning: partition %d has empty type\n"), i+1);
	return i;
}

char *const str_units(int n)	/* n==1: use singular */
{
	if (n == 1)
		return display_in_cyl_units ? _("cylinder") : _("sector");
	else
		return display_in_cyl_units ? _("cylinders") : _("sectors");
}

void change_units(void)
{
	display_in_cyl_units = !display_in_cyl_units;
	update_units();
	printf(_("Changing display/entry units to %s\n"),
		str_units(PLURAL));
}

void toggle_active(int i)
{
	struct partition *p = part_table[i];

	if (IS_EXTENDED (p->sys_ind) && !p->boot_ind)
		fprintf(stderr,
			_("WARNING: Partition %d is an extended partition\n"),
			i + 1);
	if (p->boot_ind)
		p->boot_ind = 0;
	else p->boot_ind = ACTIVE_FLAG;
	changed[i] = 1;
}

void toggle_dos(void)
{
	dos_compatible_flag = ~dos_compatible_flag;
	if (dos_compatible_flag) {
		sector_offset = sectors;
		printf(_("DOS Compatibility flag is set\n"));
	}
	else {
		sector_offset = 1;
		printf(_("DOS Compatibility flag is not set\n"));
	}
}

void delete_partition(int i)
{
	struct partition *p = part_table[i], *q = ext_pointers[i];

/* Note that for the fifth partition (i == 4) we don't actually
 * decrement partitions.
 */

	if (warn_geometry())
		return;
	changed[i] = 1;

	if (sun_label) {
		sun_delete_partition(i);
		return;
	}
	if (sgi_label) {
		sgi_delete_partition(i);
		return;
	}
	if (i < 4) {
		if (IS_EXTENDED (p->sys_ind) && i == ext_index) {
			while (partitions > 4)
				free(buffers[--partitions]);
			ext_pointers[ext_index] = NULL;
			extended_offset = 0;
		}
		clear_partition(p);
	}
	else if (!q->sys_ind && i > 4) {
		free(buffers[--partitions]);
		clear_partition(ext_pointers[--i]);
		changed[i] = 1;
	}
	else if (i > 3) {
		if (i > 4) {
			p = ext_pointers[i - 1];
			p->boot_ind = 0;
			p->head = q->head;
			p->sector = q->sector;
			p->cyl = q->cyl;
			p->sys_ind = EXTENDED;
			p->end_head = q->end_head;
			p->end_sector = q->end_sector;
			p->end_cyl = q->end_cyl;
			set_start_sect(p, get_start_sect(q));
			set_nr_sects(p, get_nr_sects(q));
			changed[i - 1] = 1;
		} else {
			if(part_table[5]) /* prevent SEGFAULT */
				set_start_sect(part_table[5],
					       get_start_sect(part_table[5]) +
					       offsets[5] - extended_offset);
			offsets[5] = extended_offset;
			changed[5] = 1;
		}
		if (partitions > 5) {
			partitions--;
			free(buffers[i]);
			while (i < partitions) {
				changed[i] = changed[i + 1];
				buffers[i] = buffers[i + 1];
				offsets[i] = offsets[i + 1];
				part_table[i] = part_table[i + 1];
				ext_pointers[i] = ext_pointers[i + 1];
				i++;
			}
		}
		else
			clear_partition(part_table[i]);
	}
}

void change_sysid(void)
{
	char *temp;
	int i = get_partition(0, partitions), sys, origsys;
	struct partition *p = part_table[i];

	origsys = sys = get_sysid(i);

	if (!sys && !sgi_label)
                printf(_("Partition %d does not exist yet!\n"), i + 1);
        else while (1) {
		sys = read_hex (get_sys_types());

		if (!sys && !sgi_label) {
			printf(_("Type 0 means free space to many systems\n"
			       "(but not to Linux). Having partitions of\n"
			       "type 0 is probably unwise. You can delete\n"
			       "a partition using the `d' command.\n"));
			/* break; */
		}

		if (!sun_label && !sgi_label) {
			if (IS_EXTENDED (sys) != IS_EXTENDED (p->sys_ind)) {
				printf(_("You cannot change a partition into"
				       " an extended one or vice versa\n"
				       "Delete it first.\n"));
				break;
			}
		}

                if (sys < 256) {
			if (sun_label && i == 2 && sys != WHOLE_DISK)
				printf(_("Consider leaving partition 3 "
				       "as Whole disk (5),\n"
				       "as SunOS/Solaris expects it and "
				       "even Linux likes it.\n\n"));
			if (sgi_label && ((i == 10 && sys != ENTIRE_DISK)
					  || (i == 8 && sys != 0)))
				printf(_("Consider leaving partition 9 "
				       "as volume header (0),\nand "
				       "partition 11 as entire volume (6)"
				       "as IRIX expects it.\n\n"));
                        if (sys == origsys)
                            break;

			if (sun_label) {
				sun_change_sysid(i, sys);
			} else
			if (sgi_label) {
				sgi_change_sysid(i, sys);
			} else
				part_table[i]->sys_ind = sys;
                        printf (_("Changed system type of partition %d "
                                "to %x (%s)\n"), i + 1, sys,
                                (temp = partition_type(sys)) ? temp :
                                _("Unknown"));
                        changed[i] = 1;
                        break;
                }
        }
}

/* check_consistency() and long2chs() added Sat Mar 6 12:28:16 1993,
 * faith@cs.unc.edu, based on code fragments from pfdisk by Gordon W. Ross,
 * Jan.  1990 (version 1.2.1 by Gordon W. Ross Aug. 1990; Modified by S.
 * Lubkin Oct.  1991). */

static void long2chs(ulong ls, uint *c, uint *h, uint *s)
{
	int	spc = heads * sectors;

	*c = ls / spc;
	ls = ls % spc;
	*h = ls / sectors;
	*s = ls % sectors + 1;	/* sectors count from 1 */
}

static void check_consistency(struct partition *p, int partition)
{
	uint	pbc, pbh, pbs;		/* physical beginning c, h, s */
	uint	pec, peh, pes;		/* physical ending c, h, s */
	uint	lbc, lbh, lbs;		/* logical beginning c, h, s */
	uint	lec, leh, les;		/* logical ending c, h, s */

	if (!heads || !sectors || (partition >= 4))
		return;		/* do not check extended partitions */

/* physical beginning c, h, s */
	pbc = (p->cyl & 0xff) | ((p->sector << 2) & 0x300);
	pbh = p->head;
	pbs = p->sector & 0x3f;

/* physical ending c, h, s */
	pec = (p->end_cyl & 0xff) | ((p->end_sector << 2) & 0x300);
	peh = p->end_head;
	pes = p->end_sector & 0x3f;

/* compute logical beginning (c, h, s) */
	long2chs(get_start_sect(p), &lbc, &lbh, &lbs);

/* compute logical ending (c, h, s) */
	long2chs(get_start_sect(p) + get_nr_sects(p) - 1, &lec, &leh, &les);

/* Same physical / logical beginning? */
	if (cylinders <= 1024 && (pbc != lbc || pbh != lbh || pbs != lbs)) {
		printf(_("Partition %d has different physical/logical "
			"beginnings (non-Linux?):\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pbc, pbh, pbs);
		printf(_("logical=(%d, %d, %d)\n"),lbc, lbh, lbs);
	}

/* Same physical / logical ending? */
	if (cylinders <= 1024 && (pec != lec || peh != leh || pes != les)) {
		printf(_("Partition %d has different physical/logical "
			"endings:\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pec, peh, pes);
		printf(_("logical=(%d, %d, %d)\n"),lec, leh, les);
	}

#if 0
/* Beginning on cylinder boundary? */
	if (pbh != !pbc || pbs != 1) {
		printf(_("Partition %i does not start on cylinder "
			"boundary:\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pbc, pbh, pbs);
		printf(_("should be (%d, %d, 1)\n"), pbc, !pbc);
	}
#endif

/* Ending on cylinder boundary? */
	if (peh != (heads - 1) || pes != sectors) {
		printf(_("Partition %i does not end on cylinder boundary:\n"),
			partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pec, peh, pes);
		printf(_("should be (%d, %d, %d)\n"),
		pec, heads - 1, sectors);
	}
}

void list_disk_geometry(void)
{
	printf(_("\nDisk %s: %d heads, %d sectors, %d cylinders\nUnits = "
	       "%s of %d * %d bytes\n\n"), disk_device, heads, sectors,
	       cylinders, str_units(PLURAL), units_per_sector, sector_size);
}

void list_table(int xtra)
{
	struct partition *p;
	char *type;
	int digit_last = 0;
	int i, w;

	if (sun_label) {
		sun_list_table(xtra);
		return;
	}

	if (sgi_label) {
		sgi_list_table(xtra);
		return;
	}

	w = strlen(disk_device);
	/* Heuristic: we list partition 3 of /dev/foo as /dev/foo3,
	   but if the device name ends in a digit, say /dev/foo1,
	   then the partition is called /dev/foo1p3. */
	if (isdigit(disk_device[w-1]))
		digit_last = 1;

	list_disk_geometry();

	if (w < 5)
		w = 5;

	/* FIXME! let's see how this shows up with other languagues 
	   acme@conectiva.com.br */

	printf(_("%*s Boot    Start       End    Blocks   Id  System\n"),
	       (digit_last ? w + 2 : w + 1), _("Device"));

	for (i = 0 ; i < partitions; i++) {
		if ((p = part_table[i])->sys_ind) {
			unsigned int psects = get_nr_sects(p);
			unsigned int pblocks = psects;
			unsigned int podd = 0;

			if (sector_size < 1024) {
				pblocks /= (1024 / sector_size);
				podd = psects % (1024 / sector_size);
			}
			if (sector_size > 1024)
				pblocks *= (sector_size / 1024);
                        printf(
			    "%*s%s%-2d  %c %9ld %9ld %9ld%c  %2x  %s\n",
/* device */		w, disk_device, (digit_last ? "p" : ""), i+1,
/* boot flag */		!p->boot_ind ? ' ' : p->boot_ind == ACTIVE_FLAG
			? '*' : '?',
/* start */		(long) cround(get_start_sect(p) + offsets[i]),
/* end */		(long) cround(get_start_sect(p) + offsets[i] + psects
				- (psects ? 1 : 0)),
/* odd flag on end */	(long) pblocks, podd ? '+' : ' ',
/* type id */		p->sys_ind,
/* type name */		(type = partition_type(p->sys_ind)) ?
			type : _("Unknown"));
			check_consistency(p, i);
		}
	}
}

void x_list_table(int extend)
{
	struct partition *p, **q;
	int i;

	if (extend)
		q = ext_pointers;
	else
		q = part_table;
	printf(_("\nDisk %s: %d heads, %d sectors, %d cylinders\n\n"),
		disk_device, heads, sectors, cylinders);
        printf(_("Nr AF  Hd Sec  Cyl  Hd Sec  Cyl    Start     Size ID\n"));
	for (i = 0 ; i < partitions; i++)
		if ((p = q[i]) != NULL) {
                        printf("%2d %02x%4d%4d%5d%4d%4d%5d%9d%9d %02x\n",
				i + 1, p->boot_ind, p->head,
				sector(p->sector),
				cylinder(p->sector, p->cyl), p->end_head,
				sector(p->end_sector),
				cylinder(p->end_sector, p->end_cyl),
				get_start_sect(p), get_nr_sects(p), p->sys_ind);
			if (p->sys_ind)
				check_consistency(p, i);
		}
}

void fill_bounds(uint *first, uint *last)
{
	int i;
	struct partition *p = part_table[0];

	for (i = 0; i < partitions; p = part_table[++i]) {
		if (!p->sys_ind || IS_EXTENDED (p->sys_ind)) {
			first[i] = 0xffffffff;
			last[i] = 0;
		} else {
			first[i] = get_start_sect(p) + offsets[i];
			last[i] = first[i] + get_nr_sects(p) - 1;
		}
	}
}

void check(int n, uint h, uint s, uint c, uint start)
{
	uint total, real_s, real_c;

	real_s = sector(s) - 1;
	real_c = cylinder(s, c);
	total = (real_c * sectors + real_s) * heads + h;
	if (!total)
		fprintf(stderr, _("Warning: partition %d contains sector 0\n"), n);
	if (h >= heads)
		fprintf(stderr,
			_("Partition %d: head %d greater than maximum %d\n"),
			n, h + 1, heads);
	if (real_s >= sectors)
		fprintf(stderr, _("Partition %d: sector %d greater than "
			"maximum %d\n"), n, s, sectors);
	if (real_c >= cylinders)
		fprintf(stderr, _("Partitions %d: cylinder %d greater than "
			"maximum %d\n"), n, real_c + 1, cylinders);
	if (cylinders <= 1024 && start != total)
		fprintf(stderr,
			_("Partition %d: previous sectors %d disagrees with "
			"total %d\n"), n, start, total);
}


void verify(void)
{
	int i, j;
	uint total = 1;
	uint first[partitions], last[partitions];
	struct partition *p = part_table[0];

	if (warn_geometry())
		return;

	if (sun_label) {
		verify_sun();
		return;
	}

	if (sgi_label) {
		verify_sgi(1);
		return;
	}

	fill_bounds(first, last);
	for (i = 0; i < partitions; p = part_table[++i])
		if (p->sys_ind && !IS_EXTENDED (p->sys_ind)) {
			check_consistency(p, i);
			if (get_start_sect(p) + offsets[i] < first[i])
				printf(_("Warning: bad start-of-data in "
					"partition %d\n"), i + 1);
			check(i + 1, p->end_head, p->end_sector, p->end_cyl,
				last[i]);
			total += last[i] + 1 - first[i];
			for (j = 0; j < i; j++)
			if ((first[i] >= first[j] && first[i] <= last[j])
			 || ((last[i] <= last[j] && last[i] >= first[j]))) {
				printf(_("Warning: partition %d overlaps "
					"partition %d.\n"), j + 1, i + 1);
				total += first[i] >= first[j] ?
					first[i] : first[j];
				total -= last[i] <= last[j] ?
					last[i] : last[j];
			}
		}

	if (extended_offset) {
		uint e_last = get_start_sect(part_table[ext_index]) +
			get_nr_sects(part_table[ext_index]) - 1;

		for (p = part_table[i = 4]; i < partitions;
				p = part_table[++i]) {
			total++;
			if (!p->sys_ind) {
				if (i != 4 || i + 1 < partitions)
					printf(_("Warning: partition %d "
						"is empty\n"), i + 1);
			}
			else if (first[i] < extended_offset ||
					last[i] > e_last)
				printf(_("Logical partition %d not entirely in "
					"partition %d\n"), i + 1, ext_index + 1);
		}
	}

	if (total > heads * sectors * cylinders)
		printf(_("Total allocated sectors %d greater than the maximum "
			"%d\n"), total, heads * sectors * cylinders);
	else if ((total = heads * sectors * cylinders - total) != 0)
		printf(_("%d unallocated sectors\n"), total);
}

void add_partition(int n, int sys)
{
	char mesg[256];		/* 48 does not suffice in Japanese */
	int i, read = 0;
	struct partition *p = part_table[n], *q = part_table[ext_index];
	uint start, stop = 0, limit, temp,
		first[partitions], last[partitions];

	if (p->sys_ind) {
		printf(_("Partition %d is already defined.  Delete "
			"it before re-adding it.\n"), n + 1);
		return;
	}
	fill_bounds(first, last);
	if (n < 4) {
		start = sector_offset;
		limit = heads * sectors * cylinders - 1;
		if (extended_offset) {
			first[ext_index] = extended_offset;
			last[ext_index] = get_start_sect(q) +
				get_nr_sects(q) - 1;
		}
	} else {
		start = extended_offset + sector_offset;
		limit = get_start_sect(q) + get_nr_sects(q) - 1;
	}
	if (display_in_cyl_units)
		for (i = 0; i < partitions; i++)
			first[i] = (cround(first[i]) - 1) * units_per_sector;

	sprintf(mesg, _("First %s"), str_units(SINGULAR));
	do {
		temp = start;
		for (i = 0; i < partitions; i++) {
			int lastplusoff;

			if (start == offsets[i])
				start += sector_offset;
			lastplusoff = last[i] + ((n<4) ? 0 : sector_offset);
			if (start >= first[i] && start <= lastplusoff)
				start = lastplusoff + 1;
		}
		if (start > limit)
			break;
		if (start >= temp+units_per_sector && read) {
			printf(_("Sector %d is already allocated\n"), temp);
			temp = start;
			read = 0;
		}
		if (!read && start == temp) {
			uint i;
			i = start;
			start = read_int(cround(i), cround(i), cround(limit),
					 0, mesg);
			if (display_in_cyl_units) {
				start = (start - 1) * units_per_sector;
				if (start < i) start = i;
			}
			read = 1;
		}
	} while (start != temp || !read);
	if (n > 4) {			/* NOT for fifth partition */
		offsets[n] = start - sector_offset;
		if (offsets[n] == extended_offset) { /* must be corrected */
		     offsets[n]++;
		     if (sector_offset == 1)
			  start++;
		}
	}

	for (i = 0; i < partitions; i++) {
		if (start < offsets[i] && limit >= offsets[i])
			limit = offsets[i] - 1;
		if (start < first[i] && limit >= first[i])
			limit = first[i] - 1;
	}
	if (start > limit) {
		printf(_("No free sectors available\n"));
		if (n > 4) {
			free(buffers[n]);
			partitions--;
		}
		return;
	}
	if (cround(start) == cround(limit)) {
		stop = limit;
	} else {
		sprintf(mesg, _("Last %s or +size or +sizeM or +sizeK"),
			str_units(SINGULAR));
		stop = read_int(cround(start), cround(limit), cround(limit),
				cround(start), mesg);
		if (display_in_cyl_units) {
			stop = stop * units_per_sector - 1;
			if (stop >limit)
				stop = limit;
		}
	}

	set_partition(n, p, start, stop, sys, offsets[n]);

	if (IS_EXTENDED (sys)) {
		ext_index = n;
		offsets[4] = extended_offset = start;
		ext_pointers[n] = p;
		if (!(buffers[4] = calloc(1, sector_size)))
			fatal(out_of_memory);
		part_table[4] = offset(buffers[4], 0);
		ext_pointers[4] = part_table[4] + 1;
		changed[4] = 1;
		partitions = 5;
	} else {
		if (n > 4)
			set_partition(n - 1, ext_pointers[n - 1],
				offsets[n], stop, EXTENDED,
				extended_offset);
#if 0
		if ((limit = get_nr_sects(p)) & 1)
			printf(_("Warning: partition %d has an odd "
				"number of sectors.\n"), n + 1);
#endif
	}
}

void add_logical(void)
{
	if (partitions > 5 || part_table[4]->sys_ind) {
		if (!(buffers[partitions] = calloc(1, sector_size)))
			fatal(out_of_memory);
		part_table[partitions] = offset(buffers[partitions], 0);
		ext_pointers[partitions] = part_table[partitions] + 1;
		offsets[partitions] = 0;
		partitions++;
	}
	add_partition(partitions - 1, LINUX_NATIVE);
}

void new_partition(void)
{
	int i, free_primary = 0;

	if (warn_geometry())
		return;

	if (sun_label) {
		add_sun_partition(get_partition(0, partitions), LINUX_NATIVE);
		return;
	}

	if (sgi_label) {
		sgi_add_partition(get_partition(0, partitions), LINUX_NATIVE);
		return;
	}

	if (partitions >= MAXIMUM_PARTS) {
		printf(_("The maximum number of partitions has been created\n"));
		return;
	}

	for (i = 0; i < 4; i++)
		free_primary += !part_table[i]->sys_ind;
	if (!free_primary) {
		if (extended_offset)
			add_logical();
		else
			printf(_("You must delete some partition and add "
				"an extended partition first\n"));
	} else {
		char c, line[LINE_LENGTH];
		sprintf(line, _("Command action\n   %s\n   p   primary "
			"partition (1-4)\n"), extended_offset ?
			_("l   logical (5 or over)") : _("e   extended"));
		while (1)
			if ((c = tolower(read_char(line))) == 'p') {
				add_partition(get_partition(0, 4),
					LINUX_NATIVE);
				return;
			}
			else if (c == 'l' && extended_offset) {
				add_logical();
				return;
			}
			else if (c == 'e' && !extended_offset) {
				add_partition(get_partition(0, 4),
					EXTENDED);
				return;
			}
			else
				printf(_("Invalid partition number "
				       "for type `%c'\n"), c);
		
	}
}

void write_table(void)
{
	int i;

	changed[3] = changed[0] || changed[1] || changed[2] || changed[3];
	if (!sun_label && !sgi_label) {
	    for (i = 3; i < partitions; i++) {
		if (changed[i]) {
			write_part_table_flag(buffers[i]);
			if (ext2_llseek(fd, (ext2_loff_t)offsets[i]
				       * sector_size, SEEK_SET) < 0)
				fatal(unable_to_seek);
			if (write(fd, buffers[i], sector_size) != sector_size)
				fatal(unable_to_write);
		}
	    }
	} else if (sgi_label) {
	    /* no test on change? the printf below might be mistaken */
	    sgi_write_table();
	} else if (sun_label) {
	    if (changed[3] || changed[4] || changed[5] ||
		changed[6] || changed[7]) {
		sun_write_table();
	    }
	}

	printf(_("The partition table has been altered!\n\n"));
	reread_partition_table(1);
}

void
reread_partition_table(int leave) {
	int error = 0;
	int i;

	printf(_("Calling ioctl() to re-read partition table.\n"));
	sync();
	sleep(2);
	if ((i = ioctl(fd, BLKRRPART)) != 0) {
                error = errno;
        } else {
                /* some kernel versions (1.2.x) seem to have trouble
                   rereading the partition table, but if asked to do it
		   twice, the second time works. - biro@yggdrasil.com */
                sync();
                sleep(2);
                if((i = ioctl(fd, BLKRRPART)) != 0)
                        error = errno;
        }

	if (i < 0)
		printf(_("Re-read table failed with error %d: %s.\nReboot your "
			"system to ensure the partition table is updated.\n"),
			error, strerror(error));

	if (!sun_label && !sgi_label)
	    printf(
		_("\nWARNING: If you have created or modified any DOS 6.x\n"
		"partitions, please see the fdisk manual page for additional\n"
		"information.\n"));

	if (leave) {
		close(fd);

		printf(_("Syncing disks.\n"));
		sync();
		sleep(4);		/* for sync() */
		exit(!!i);
	}
}

#define MAX_PER_LINE	16
void print_buffer(char buffer[])
{
	int	i,
		l;

	for (i = 0, l = 0; i < sector_size; i++, l++) {
		if (l == 0)
			printf("0x%03X:", i);
		printf(" %02X", (unsigned char) buffer[i]);
		if (l == MAX_PER_LINE - 1) {
			printf("\n");
			l = -1;
		}
	}
	if (l > 0)
		printf("\n");
	printf("\n");
}

void print_raw(void)
{
	int i;

	printf(_("Device: %s\n"), disk_device);
	if (sun_label || sgi_label)
		print_buffer(buffer);
	else for (i = 3; i < partitions; i++)
		print_buffer(buffers[i]);
}

void move_begin(int i)
{
	struct partition *p = part_table[i];
	uint new, first;

	if (warn_geometry())
		return;
	if (!p->sys_ind || !get_nr_sects(p) || IS_EXTENDED (p->sys_ind)) {
		printf(_("Partition %d has no data area\n"), i + 1);
		return;
	}
	first = get_start_sect(p) + offsets[i];
	new = read_int(first, first, 
		       get_start_sect(p) + get_nr_sects(p) + offsets[i] - 1,
		       first, _("New beginning of data")) - offsets[i];

	if (new != get_nr_sects(p)) {
		first = get_nr_sects(p) + get_start_sect(p) - new;
		set_nr_sects(p, first);
		set_start_sect(p, new);
		changed[i] = 1;
	}
}

void xselect(void)
{
	while(1) {
		putchar('\n');
		switch (tolower(read_char(_("Expert command (m for help): ")))) {
		case 'a':
			if (sun_label)
				sun_set_alt_cyl();
			break;
		case 'b':
			if (!sun_label && !sgi_label)
				move_begin(get_partition(0, partitions));
			break;
		case 'c':
			cylinders = read_int(1, cylinders, 131071,
					     0, _("Number of cylinders"));
			if (sun_label)
				sun_set_ncyl(cylinders);
			warn_cylinders();
			break;
		case 'd':
			print_raw();
			break;
		case 'e':
			if (sgi_label)
				sgi_set_xcyl();
			else if (sun_label)
				sun_set_xcyl();
			else
				x_list_table(1);
			break;
		case 'g':
			create_sgilabel();
			break;
		case 'h':
			heads = read_int(1, heads, 256, 0,
					 _("Number of heads"));
			update_units();
			break;
		case 'i':
			if (sun_label)
				sun_set_ilfact();
			break;
		case 'o':
			if (sun_label)
				sun_set_rspeed();
			break;
		case 'p':
			if (sun_label)
				list_table(1);
			else
				x_list_table(0);
			break;
		case 'q':
			close(fd);
			printf("\n");
			exit(0);
		case 'r':
			return;
		case 's':
			sectors = read_int(1, sectors, 63, 0,
					   _("Number of sectors"));
			if (dos_compatible_flag) {
				sector_offset = sectors;
				fprintf(stderr, _("Warning: setting "
					"sector offset for DOS "
					"compatiblity\n"));
			}
			update_units();
			break;
		case 'v':
			verify();
			break;
		case 'w':
			write_table(); 	/* does not return */
			break;
		case 'y':
			if (sun_label)
				sun_set_pcylcount();
			break;
		default:
			xmenu();
		}
	}
}

int
is_ide_cdrom(char *device) {
	/* No device was given explicitly, and we are trying some
       likely things.  But opening /dev/hdc may produce errors like
           "hdc: tray open or drive not ready"
       if it happens to be a CD-ROM drive. It even happens that
       the process hangs on the attempt to read a music CD.
       So try to be careful. This only works since 2.1.73. */

    FILE *procf;
    char buf[100];
    struct stat statbuf;

    if (strncmp("/dev/hd", device, 7))
	return 0;
    sprintf(buf, "/proc/ide/%s/media", device+5);
    procf = fopen(buf, "r");
    if (procf != NULL && fgets(buf, sizeof(buf), procf))
        return  !strncmp(buf, "cdrom", 5);

    /* Now when this proc file does not exist, skip the
       device when it is read-only. */
    if (stat(device, &statbuf) == 0)
        return (statbuf.st_mode & 0222) == 0;

    return 0;
}

void try(char *device, int user_specified)
{
	disk_device = device;
	if (!setjmp(listingbuf)) {
		if (!user_specified)
			if (is_ide_cdrom(device))
				return;
		if ((fd = open(disk_device, type_open)) >= 0) {
			if (get_boot(try_only) < 0) {
				list_disk_geometry();
				if (btrydev(device) < 0)
					fprintf(stderr,
						_("Disk %s doesn't contain a valid "
						"partition table\n"), device);
				close(fd);
			} else {
				close(fd);
				list_table(0);
				if (!sun_label && partitions > 4)
					delete_partition(ext_index);
			}
		} else {
				/* Ignore other errors, since we try IDE
				   and SCSI hard disks which may not be
				   installed on the system. */
			if(errno == EACCES) {
			    fprintf(stderr, _("Cannot open %s\n"), device);
			    return;
			}
		}
	}
}

/* for fdisk -l: try all things in /proc/partitions
   that look like a partition name (do not end in a digit) */
void
tryprocpt() {
	FILE *procpt;
	char line[100], ptname[100], devname[120], *s;
	int ma, mi, sz;

	procpt = fopen(PROC_PARTITIONS, "r");
	if (procpt == NULL) {
		fprintf(stderr, _("cannot open %s\n"), PROC_PARTITIONS);
		return;
	}

	while (fgets(line, sizeof(line), procpt)) {
		if (sscanf (line, " %d %d %d %[^\n]\n",
			    &ma, &mi, &sz, ptname) != 4)
			continue;
		for(s = ptname; *s; s++);
		if (isdigit(s[-1]))
			continue;
		sprintf(devname, "/dev/%s", ptname);
		try(devname, 1);
	}
}

int
dir_exists(char *dirname) {
	struct stat statbuf;

	return (stat(dirname, &statbuf) == 0 && S_ISDIR(statbuf.st_mode));
}

void
dummy(int *kk) {}

int
main(int argc, char **argv) {
	int j, c;
	int optl = 0, opts = 0;
	int user_set_sector_size = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/*
	 * Calls:
	 *  fdisk -v
	 *  fdisk -l [-b sectorsize] [-u] device ...
	 *  fdisk -s [partition] ...
	 *  fdisk [-b sectorsize] [-u] device
	 */
	while ((c = getopt(argc, argv, "b:lsuv")) != EOF) {
		switch (c) {
		case 'b':
			/* ugly: this sector size is really per device,
			   so cannot be combined with multiple disks */
			sector_size = atoi(optarg);
			if (sector_size != 512 && sector_size != 1024 &&
			    sector_size != 2048)
				fatal(usage);
			sector_offset = 2;
			user_set_sector_size = 1;
			break;
		case 'l':
			optl = 1;
			break;
		case 's':
			opts = 1;
			break;
		case 'u':
			display_in_cyl_units = 0;
			break;
		case 'v':
			printf("fdisk v" UTIL_LINUX_VERSION "\n");
			exit(0);
		default:
			fatal(usage);
		}
	}

#if 0
	printf(_("This kernel finds the sector size itself - -b option ignored\n"));
#else
	if (user_set_sector_size && argc-optind != 1)
		printf(_("Warning: the -b (set sector size) option should"
			 " be used with one specified device\n"));
#endif

	if (optl) {
		nowarn = 1;
		type_open = O_RDONLY;
		if (argc > optind) {
			int k;
			/* avoid gcc warning:
			   variable `k' might be clobbered by `longjmp' */
			dummy(&k);
			listing = 1;
			for(k=optind; k<argc; k++)
				try(argv[k], 1);
		} else {
			/* we no longer have default device names */
			/* but, we can use /proc/partitions instead */
			tryprocpt();
		}
		exit(0);
	}

	if (opts) {
		long size;

		nowarn = 1;
		type_open = O_RDONLY;

		opts = argc - optind;
		if (opts <= 0)
			fatal(usage);

		for (j = optind; j < argc; j++) {
			disk_device = argv[j];
			if ((fd = open(disk_device, type_open)) < 0)
				fatal(unable_to_open);
			if (ioctl(fd, BLKGETSIZE, &size))
				fatal(ioctl_error);
			close(fd);
			if (opts == 1)
				printf("%ld\n", size/2);
			else
				printf("%s: %ld\n", argv[j], size/2);
		}
		exit(0);
	}

	if (argc-optind == 1)
		disk_device = argv[optind];
	else if (argc-optind != 0)
		fatal(usage);
	else
		fatal(usage2);

	get_boot(fdisk);

	while (1) {
		putchar('\n');
		switch (tolower(read_char(_("Command (m for help): ")))) {
		case 'a':
			if (sun_label)
				toggle_sunflags(get_partition(1, partitions),
						0x01);
			else
				if (sgi_label)
					sgi_set_bootpartition(
						get_partition(1, partitions));
				else
					toggle_active(get_partition(1, partitions));
			break;
		case 'b':
			if (sgi_label) {
				printf(_("\nThe current boot file is: %s\n"),
				       sgi_get_bootfile());
				if (read_chars(_("Please enter the name of the "
					       "new boot file: ")) == '\n')
					printf(_("Boot file unchanged\n"));
				else
					sgi_set_bootfile(line_ptr);
			} else
				bselect();
			break;
		case 'c':
			if (sun_label)
				toggle_sunflags(get_partition(1, partitions),
						0x10);
			else
				if (sgi_label)
					sgi_set_swappartition(
						get_partition(1, partitions));
				else
					toggle_dos();
			break;
		case 'd':
			delete_partition(
				get_partition(1, partitions));
			break;
		case 'i':
			if (sgi_label)
				create_sgiinfo();
			else
				menu();
		case 'l':
			list_types(get_sys_types());
			break;
		case 'n':
			new_partition();
			break;
		case 'o':
			create_doslabel();
			break;
		case 'p':
			list_table(0);
			break;
		case 'q':
			close(fd);
			printf("\n");
			exit(0);
		case 's':
			create_sunlabel();
			break;
		case 't':
			change_sysid();
			break;
		case 'u':
			change_units();
			break;
		case 'v':
			verify();
			break;
		case 'w':
			write_table(); 		/* does not return */
			break;
		case 'x':
			if( sgi_label ) {
				fprintf(stderr,
					_("\n\tSorry, no experts menu for SGI "
					"partition tables available.\n\n"));
			} else
				xselect();
			break;
		default: menu();
		}
	}
	return 0;
}
