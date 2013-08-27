/*
 * last(1) from sysvinit project, merged into util-linux in Aug 2013.
 *
 * Copyright (C) 1991-2004 Miquel van Smoorenburg.
 * Copyright (C) 2013      Ondrej Oprala <ooprala@redhat.com>
 *                         Karel Zak <kzak@redhat.com>
 *
 * Re-implementation of the 'last' command, this time for Linux. Yes I know
 * there is BSD last, but I just felt like writing this. No thanks :-).  Also,
 * this version gives lots more info (especially with -x)
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <utmp.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "c.h"
#include "nls.h"
#include "pathnames.h"
#include "xalloc.h"
#include "closestream.h"
#include "carefulputc.h"
#include "strutils.h"
#include "time-util.h"

#ifndef SHUTDOWN_TIME
# define SHUTDOWN_TIME 254
#endif

#ifndef LAST_LOGIN_LEN
# define LAST_LOGIN_LEN 8
#endif

#ifndef LAST_DOMAIN_LEN
# define LAST_DOMAIN_LEN 16
#endif

#define UCHUNKSIZE	16384	/* How much we read at once. */

struct last_control {
	char lastb;		/* Is this command 'lastb' */
	char extended;		/* Lots of info */
	char showhost;		/* Show hostname */
	char altlist;		/* Hostname at the end */
	char usedns;		/* Use DNS to lookup the hostname */
	char useip;		/* Print IP address in number format */
	char fulltime;		/* Print full dates and times */

	unsigned int name_len;	/* Number of login name characters to print */
	unsigned int domain_len; /* Number of domain name characters to print */
	unsigned int maxrecs;	/* Maximum number of records to list */

	char **show;		/* Match search list */

	char **altv;		/* Alternate wtmp files */
	unsigned int altc;	/* Number of alternative files */
	unsigned int alti;	/* Index number of the alternative file */

	time_t since;		/* at what time to start displaying the file */
	time_t until;		/* at what time to stop displaying the file */
	time_t present;		/* who where present at time_t */
};

/* Double linked list of struct utmp's */
struct utmplist {
	struct utmp ut;
	struct utmplist *next;
	struct utmplist *prev;
};
struct utmplist *utmplist = NULL;

/* Types of listing */
enum {
	R_CRASH = 1,	/* No logout record, system boot in between */
	R_DOWN,		/* System brought down in decent way */
	R_NORMAL,	/* Normal */
	R_NOW,		/* Still logged in */
	R_REBOOT,	/* Reboot record. */
	R_PHANTOM,	/* No logout record but session is stale. */
	R_TIMECHANGE	/* NEW_TIME or OLD_TIME */
};

/* Global variables */
static unsigned int recsdone;	/* Number of records listed */
static time_t lastdate;		/* Last date we've seen */

/*
 *	Read one utmp entry, return in new format.
 *	Automatically reposition file pointer.
 */
static int uread(const struct last_control *ctl, FILE *fp, struct utmp *u,
		 int *quit)
{
	static int utsize;
	static char buf[UCHUNKSIZE];
	char tmp[1024];
	static off_t fpos;
	static int bpos;
	off_t o;

	if (quit == NULL && u != NULL) {
		/*
		 *	Normal read.
		 */
		return fread(u, sizeof(struct utmp), 1, fp);
	}

	if (u == NULL) {
		/*
		 *	Initialize and position.
		 */
		utsize = sizeof(struct utmp);
		fseeko(fp, 0, SEEK_END);
		fpos = ftello(fp);
		if (fpos == 0)
			return 0;
		o = ((fpos - 1) / UCHUNKSIZE) * UCHUNKSIZE;
		if (fseeko(fp, o, SEEK_SET) < 0) {
			warn(_("seek failed: %s"), ctl->altv[ctl->alti]);
			return 0;
		}
		bpos = (int)(fpos - o);
		if (fread(buf, bpos, 1, fp) != 1) {
			warn(_("read failed: %s"), ctl->altv[ctl->alti]);
			return 0;
		}
		fpos = o;
		return 1;
	}

	/*
	 *	Read one struct. From the buffer if possible.
	 */
	bpos -= utsize;
	if (bpos >= 0) {
		memcpy(u, buf + bpos, sizeof(struct utmp));
		return 1;
	}

	/*
	 *	Oops we went "below" the buffer. We should be able to
	 *	seek back UCHUNKSIZE bytes.
	 */
	fpos -= UCHUNKSIZE;
	if (fpos < 0)
		return 0;

	/*
	 *	Copy whatever is left in the buffer.
	 */
	memcpy(tmp + (-bpos), buf, utsize + bpos);
	if (fseeko(fp, fpos, SEEK_SET) < 0) {
		warn(_("seek failed: %s"), ctl->altv[ctl->alti]);
		return 0;
	}

	/*
	 *	Read another UCHUNKSIZE bytes.
	 */
	if (fread(buf, UCHUNKSIZE, 1, fp) != 1) {
		warn(_("read failed: %s"), ctl->altv[ctl->alti]);
		return 0;
	}

	/*
	 *	The end of the UCHUNKSIZE byte buffer should be the first
	 *	few bytes of the current struct utmp.
	 */
	memcpy(tmp, buf + UCHUNKSIZE + bpos, -bpos);
	bpos += UCHUNKSIZE;

	memcpy(u, tmp, sizeof(struct utmp));

	return 1;
}

/*
 *	Print a short date.
 */
static char *showdate(void)
{
	char *s = ctime(&lastdate);
	s[16] = 0;
	return s;
}

/*
 *	SIGINT handler
 */
static void int_handler(int sig __attribute__((unused)))
{
	errx(EXIT_FAILURE, _("Interrupted %s"), showdate());
}

/*
 *	SIGQUIT handler
 */
static void quit_handler(int sig __attribute__((unused)))
{
	warnx(_("Interrupted %s"), showdate());
	signal(SIGQUIT, quit_handler);
}

/*
 *	Lookup a host with DNS.
 */
static int dns_lookup(char *result, int size, int useip, int32_t *a)
{
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
	struct sockaddr		*sa;
	int			salen, flags;
	int			mapped = 0;

	flags = useip ? NI_NUMERICHOST : 0;

	/*
	 *	IPv4 or IPv6 ?
	 *	1. If last 3 4bytes are 0, must be IPv4
	 *	2. If IPv6 in IPv4, handle as IPv4
	 *	3. Anything else is IPv6
	 *
	 *	Ugly.
	 */
	if (a[0] == 0 && a[1] == 0 && a[2] == (int32_t)htonl (0xffff))
		mapped = 1;

	if (mapped || (a[1] == 0 && a[2] == 0 && a[3] == 0)) {
		/* IPv4 */
		sin.sin_family = AF_INET;
		sin.sin_port = 0;
		sin.sin_addr.s_addr = mapped ? a[3] : a[0];
		sa = (struct sockaddr *)&sin;
		salen = sizeof(sin);
	} else {
		/* IPv6 */
		memset(&sin6, 0, sizeof(sin6));
		sin6.sin6_family = AF_INET6;
		sin6.sin6_port = 0;
		memcpy(sin6.sin6_addr.s6_addr, a, 16);
		sa = (struct sockaddr *)&sin6;
		salen = sizeof(sin6);
	}

	return getnameinfo(sa, salen, result, size, NULL, 0, flags);
}

/*
 *	Show one line of information on screen
 */
static int list(const struct last_control *ctl, struct utmp *p, time_t t, int what)
{
	time_t		secs, tmp;
	char		logintime[32];
	char		logouttime[32];
	char		length[32];
	char		final[512];
	char		utline[UT_LINESIZE+1];
	char		domain[256];
	char		*s;
	int		mins, hours, days;
	int		r, len;

	/*
	 *	uucp and ftp have special-type entries
	 */
	utline[0] = 0;
	strncat(utline, p->ut_line, UT_LINESIZE);
	if (strncmp(utline, "ftp", 3) == 0 && isdigit(utline[3]))
		utline[3] = 0;
	if (strncmp(utline, "uucp", 4) == 0 && isdigit(utline[4]))
		utline[4] = 0;

	/*
	 *	Is this something we wanna show?
	 */
	if (ctl->show) {
		char **walk;
		for (walk = ctl->show; *walk; walk++) {
			if (strncmp(p->ut_name, *walk, UT_NAMESIZE) == 0 ||
			    strcmp(utline, *walk) == 0 ||
			    (strncmp(utline, "tty", 3) == 0 &&
			     strcmp(utline + 3, *walk) == 0)) break;
		}
		if (*walk == NULL) return 0;
	}

	/*
	 *	Calculate times
	 */
	tmp = (time_t)p->ut_time;

	if (ctl->present && (ctl->present < tmp || (0 < t && t < ctl->present)))
		return 0;

	strcpy(logintime, ctime(&tmp));
	if (ctl->fulltime)
		sprintf(logouttime, "- %s", ctime(&t));
	else {
		logintime[16] = 0;
		sprintf(logouttime, "- %s", ctime(&t) + 11);
		logouttime[7] = 0;
	}
	secs = t - p->ut_time;
	mins  = (secs / 60) % 60;
	hours = (secs / 3600) % 24;
	days  = secs / 86400;
	if (days)
		sprintf(length, "(%d+%02d:%02d)", days, hours, mins);
	else
		sprintf(length, " (%02d:%02d)", hours, mins);

	switch(what) {
		case R_CRASH:
			sprintf(logouttime, "- crash");
			break;
		case R_DOWN:
			sprintf(logouttime, "- down ");
			break;
		case R_NOW:
			length[0] = 0;
			if (ctl->fulltime)
				sprintf(logouttime, "  still logged in");
			else {
				sprintf(logouttime, "  still");
				sprintf(length, "logged in");
			}
			break;
		case R_PHANTOM:
			length[0] = 0;
			if (ctl->fulltime)
				sprintf(logouttime, "  gone - no logout");
			else {
				sprintf(logouttime, "   gone");
				sprintf(length, "- no logout");
			}
			break;
		case R_REBOOT:
			break;
		case R_TIMECHANGE:
			logouttime[0] = 0;
			length[0] = 0;
			break;
		case R_NORMAL:
			break;
		default:
			abort();
	}

	/*
	 *	Look up host with DNS if needed.
	 */
	r = -1;
	if (ctl->usedns || ctl->useip)
		r = dns_lookup(domain, sizeof(domain), ctl->useip, p->ut_addr_v6);
	if (r < 0) {
		len = UT_HOSTSIZE;
		if (len >= (int)sizeof(domain)) len = sizeof(domain) - 1;
		domain[0] = 0;
		strncat(domain, p->ut_host, len);
	}

	if (ctl->showhost) {
		if (!ctl->altlist) {
			len = snprintf(final, sizeof(final),
				ctl->fulltime ?
				"%-8.*s %-12.12s %-16.*s %-24.24s %-26.26s %-12.12s\n" :
				"%-8.*s %-12.12s %-16.*s %-16.16s %-7.7s %-12.12s\n",
				ctl->name_len, p->ut_name, utline,
				ctl->domain_len, domain, logintime, logouttime, length);
		} else {
			len = snprintf(final, sizeof(final),
				ctl->fulltime ?
				"%-8.*s %-12.12s %-24.24s %-26.26s %-12.12s %s\n" :
				"%-8.*s %-12.12s %-16.16s %-7.7s %-12.12s %s\n",
				ctl->name_len, p->ut_name, utline,
				logintime, logouttime, length, domain);
		}
	} else
		len = snprintf(final, sizeof(final),
			ctl->fulltime ?
			"%-8.*s %-12.12s %-24.24s %-26.26s %-12.12s\n" :
			"%-8.*s %-12.12s %-16.16s %-7.7s %-12.12s\n",
			ctl->name_len, p->ut_name, utline,
			logintime, logouttime, length);

#if defined(__GLIBC__)
#  if (__GLIBC__ == 2) && (__GLIBC_MINOR__ == 0)
	final[sizeof(final)-1] = '\0';
#  endif
#endif

	/*
	 *	Print out "final" string safely.
	 */
	for (s = final; *s; s++)
		carefulputc(*s, stdout, '*');

	if (len < 0 || (size_t)len >= sizeof(final))
		putchar('\n');

	recsdone++;
	if (ctl->maxrecs && ctl->maxrecs <= recsdone)
		return 1;

	return 0;
}


static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(
		" %s [options] [<username>...] [<tty>...]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -<number>            how many lines to show\n"), out);
	fputs(_(" -a, --hostlast       display hostnames in the last column\n"), out);
	fputs(_(" -d, --dns            translate the IP number back into a hostname\n"), out);
	fprintf(out,
	      _(" -f, --file <file>    use a specific file instead of %s\n"), _PATH_WTMP);
	fputs(_(" -F, --fulltimes      print full login and logout times and dates\n"), out);
	fputs(_(" -i, --ip             display IP numbers in numbers-and-dots notation\n"), out);
	fputs(_(" -n, --limit <number> how many lines to show\n"), out);
	fputs(_(" -R, --nohostname     don't display the hostname field\n"), out);
	fputs(_(" -s, --since <time>   display the lines since the specified time\n"), out);
	fputs(_(" -t, --until <time>   display the lines until the specified time\n"), out);
	fputs(_(" -p, --present <time> display who where present at the specified time\n"), out);
	fputs(_(" -w, --fullnames      display full user and domain names\n"), out);
	fputs(_(" -x, --system         display system shutdown entries and run level changes\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("last(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}


static void process_wtmp_file(const struct last_control *ctl)
{
	FILE *fp;		/* Filepointer of wtmp file */

	struct utmp ut;		/* Current utmp entry */
	struct utmplist *p;	/* Pointer into utmplist */
	struct utmplist *next;	/* Pointer into utmplist */

	time_t lastboot = 0;	/* Last boottime */
	time_t lastrch = 0;	/* Last run level change */
	time_t lastdown;	/* Last downtime */
	time_t begintime;	/* When wtmp begins */
	int whydown = 0;	/* Why we went down: crash or shutdown */

	int c, x;		/* Scratch */
	struct stat st;		/* To stat the [uw]tmp file */
	int quit = 0;		/* Flag */
	int down = 0;		/* Down flag */

	time(&lastdown);
	lastrch = lastdown;

	/*
	 * Fill in 'lastdate'
	 */
	lastdate = lastdown;

	/*
	 * Install signal handlers
	 */
	signal(SIGINT, int_handler);
	signal(SIGQUIT, quit_handler);

	/*
	 * Open the utmp file
	 */
	if ((fp = fopen(ctl->altv[ctl->alti], "r")) == NULL)
		err(EXIT_FAILURE, _("cannot open %s"), ctl->altv[ctl->alti]);

	/*
	 * Optimize the buffer size.
	 */
	setvbuf(fp, NULL, _IOFBF, UCHUNKSIZE);

	/*
	 * Read first structure to capture the time field
	 */
	if (uread(ctl, fp, &ut, NULL) == 1)
		begintime = ut.ut_time;
	else {
		fstat(fileno(fp), &st);
		begintime = st.st_ctime;
		quit = 1;
	}

	/*
	 * Go to end of file minus one structure
	 * and/or initialize utmp reading code.
	 */
	uread(ctl, fp, NULL, NULL);

	/*
	 * Read struct after struct backwards from the file.
	 */
	while (!quit) {

		if (uread(ctl, fp, &ut, &quit) != 1)
			break;

		if (ctl->since && ut.ut_time < ctl->since)
			continue;

		if (ctl->until && ctl->until < ut.ut_time)
			continue;

		lastdate = ut.ut_time;

		if (ctl->lastb) {
			quit = list(ctl, &ut, ut.ut_time, R_NORMAL);
			continue;
		}

		/*
		 * Set ut_type to the correct type.
		 */
		if (strncmp(ut.ut_line, "~", 1) == 0) {
			if (strncmp(ut.ut_user, "shutdown", 8) == 0)
				ut.ut_type = SHUTDOWN_TIME;
			else if (strncmp(ut.ut_user, "reboot", 6) == 0)
				ut.ut_type = BOOT_TIME;
			else if (strncmp(ut.ut_user, "runlevel", 8) == 0)
				ut.ut_type = RUN_LVL;
		}
#if 1 /*def COMPAT*/
		/*
		 * For stupid old applications that don't fill in
		 * ut_type correctly.
		 */
		else {
			if (ut.ut_type != DEAD_PROCESS &&
			    ut.ut_name[0] && ut.ut_line[0] &&
			    strcmp(ut.ut_name, "LOGIN") != 0)
				ut.ut_type = USER_PROCESS;
			/*
			 * Even worse, applications that write ghost
			 * entries: ut_type set to USER_PROCESS but
			 * empty ut_name...
			 */
			if (ut.ut_name[0] == 0)
				ut.ut_type = DEAD_PROCESS;

			/*
			 * Clock changes.
			 */
			if (strcmp(ut.ut_name, "date") == 0) {
				if (ut.ut_line[0] == '|')
					ut.ut_type = OLD_TIME;
				if (ut.ut_line[0] == '{')
					ut.ut_type = NEW_TIME;
			}
		}
#endif
		switch (ut.ut_type) {
		case SHUTDOWN_TIME:
			if (ctl->extended) {
				strcpy(ut.ut_line, "system down");
				quit = list(ctl, &ut, lastboot, R_NORMAL);
			}
			lastdown = lastrch = ut.ut_time;
			down = 1;
			break;
		case OLD_TIME:
		case NEW_TIME:
			if (ctl->extended) {
				strcpy(ut.ut_line,
				ut.ut_type == NEW_TIME ? "new time" :
					"old time");
				quit = list(ctl, &ut, lastdown, R_TIMECHANGE);
			}
			break;
		case BOOT_TIME:
			strcpy(ut.ut_line, "system boot");
			quit = list(ctl, &ut, lastdown, R_REBOOT);
			lastboot = ut.ut_time;
			down = 1;
			break;
		case RUN_LVL:
			x = ut.ut_pid & 255;
			if (ctl->extended) {
				sprintf(ut.ut_line, "(to lvl %c)", x);
				quit = list(ctl, &ut, lastrch, R_NORMAL);
			}
			if (x == '0' || x == '6') {
				lastdown = ut.ut_time;
				down = 1;
				ut.ut_type = SHUTDOWN_TIME;
			}
			lastrch = ut.ut_time;
			break;

		case USER_PROCESS:
			/*
			 * This was a login - show the first matching
			 * logout record and delete all records with
			 * the same ut_line.
			 */
			c = 0;
			for (p = utmplist; p; p = next) {
				next = p->next;
				if (strncmp(p->ut.ut_line, ut.ut_line,
				    UT_LINESIZE) == 0) {
					/* Show it */
					if (c == 0) {
						quit = list(ctl, &ut, p->ut.ut_time, R_NORMAL);
						c = 1;
					}
					if (p->next) p->next->prev = p->prev;
					if (p->prev)
						p->prev->next = p->next;
					else
						utmplist = p->next;
					free(p);
				}
			}
			/*
			 * Not found? Then crashed, down, still
			 * logged in, or missing logout record.
			 */
			if (c == 0) {
				if (lastboot == 0) {
					c = R_NOW;
					/* Is process still alive? */
					if (ut.ut_pid > 0 &&
					    kill(ut.ut_pid, 0) != 0 &&
					    errno == ESRCH)
						c = R_PHANTOM;
				} else
					c = whydown;
				quit = list(ctl, &ut, lastboot, c);
			}
			/* FALLTHRU */

		case DEAD_PROCESS:
			/*
			 * Just store the data if it is
			 * interesting enough.
			 */
			if (ut.ut_line[0] == 0)
				break;
			p = xmalloc(sizeof(struct utmplist));
			memcpy(&p->ut, &ut, sizeof(struct utmp));
			p->next  = utmplist;
			p->prev  = NULL;
			if (utmplist) utmplist->prev = p;
			utmplist = p;
			break;

		case EMPTY:
		case INIT_PROCESS:
		case LOGIN_PROCESS:
		case ACCOUNTING:
			/* ignored ut_types */
			break;

		default:
			warnx("unrecogized ut_type: %d", ut.ut_type);
		}

		/*
		 * If we saw a shutdown/reboot record we can remove
		 * the entire current utmplist.
		 */
		if (down) {
			lastboot = ut.ut_time;
			whydown = (ut.ut_type == SHUTDOWN_TIME) ? R_DOWN : R_CRASH;
			for (p = utmplist; p; p = next) {
				next = p->next;
				free(p);
			}
			utmplist = NULL;
			down = 0;
		}
	}

	printf(_("\n%s begins %s"), basename(ctl->altv[ctl->alti]), ctime(&begintime));
	fclose(fp);
}

int main(int argc, char **argv)
{
	struct last_control ctl = {
		.showhost = TRUE,
		.name_len = LAST_LOGIN_LEN,
		.domain_len = LAST_DOMAIN_LEN
	};
	int c;
	usec_t p;

	static const struct option long_opts[] = {
	      { "limit",	required_argument, NULL, 'n' },
	      { "help",	no_argument,       NULL, 'h' },
	      { "file",       required_argument, NULL, 'f' },
	      { "nohostname", no_argument,       NULL, 'R' },
	      { "version",    no_argument,       NULL, 'V' },
	      { "hostlast",   no_argument,       NULL, 'a' },
	      { "since",      required_argument, NULL, 's' },
	      { "until",      required_argument, NULL, 't' },
	      { "present",    required_argument, NULL, 'p' },
	      { "system",     no_argument,       NULL, 'x' },
	      { "dns",        no_argument,       NULL, 'd' },
	      { "ip",         no_argument,       NULL, 'i' },
	      { "fulltimes",  no_argument,       NULL, 'F' },
	      { "fullnames",  no_argument,       NULL, 'w' },
	      { NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv,
			"hVf:n:RxadFit:p:s:0123456789w", long_opts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'R':
			ctl.showhost = FALSE;
			break;
		case 'x':
			ctl.extended = TRUE;
			break;
		case 'n':
			ctl.maxrecs = strtos32_or_err(optarg, _("failed to parse number"));
			break;
		case 'f':
			if (!ctl.altv)
				ctl.altv = xmalloc(sizeof(char *) * argc);
			ctl.altv[ctl.altc++] = xstrdup(optarg);
			break;
		case 'd':
			ctl.usedns = TRUE;
			break;
		case 'i':
			ctl.useip = TRUE;
			break;
		case 'a':
			ctl.altlist = TRUE;
			break;
		case 'F':
			ctl.fulltime = TRUE;
			break;
		case 'p':
			if (parse_timestamp(optarg, &p) < 0)
				errx(EXIT_FAILURE, _("invalid time value \"%s\""), optarg);
			ctl.present = (time_t) (p / 1000000);
			break;
		case 's':
			if (parse_timestamp(optarg, &p) < 0)
				errx(EXIT_FAILURE, _("invalid time value \"%s\""), optarg);
			ctl.since = (time_t) (p / 1000000);
			break;
		case 't':
			if (parse_timestamp(optarg, &p) < 0)
				errx(EXIT_FAILURE, _("invalid time value \"%s\""), optarg);
			ctl.until = (time_t) (p / 1000000);
			break;
		case 'w':
			if (ctl.name_len < UT_NAMESIZE)
				ctl.name_len = UT_NAMESIZE;
			if (ctl.domain_len < UT_HOSTSIZE)
				ctl.domain_len = UT_HOSTSIZE;
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			ctl.maxrecs = 10 * ctl.maxrecs + c - '0';
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (optind < argc)
		ctl.show = argv + optind;

	/*
	 * Which file do we want to read?
	 */
	ctl.lastb = !strcmp(program_invocation_short_name, "lastb");
	if (!ctl.altc) {
		ctl.altv = xmalloc(sizeof(char *));
		if (ctl.lastb)
			ctl.altv[0] = xstrdup(_PATH_BTMP);
		else
			ctl.altv[0] = xstrdup(_PATH_WTMP);
		ctl.altc++;
	}

	for (; ctl.alti < ctl.altc; ctl.alti++) {
		process_wtmp_file(&ctl);
		free(ctl.altv[ctl.alti]);
	}
	free(ctl.altv);
	return EXIT_SUCCESS;
}
