#define _GNU_SOURCE

#ifdef __linux__
#include <linux/version.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,39)
#include <sys/socket.h>
#endif
#include <linux/if_link.h>
#elif __OpenBSD__ || __NetBSD__
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if_dl.h>
#include <net/route.h>
#else
#error "your platform is not supported"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#ifdef __NetBSD__
#include <ncurses/ncurses.h>
#else
#include <ncurses.h>
#endif
#include <ifaddrs.h>
#include <net/if.h>

#include "arg.h"

#define VERSION "0.5"

struct iface {
	char ifname[IFNAMSIZ];
	unsigned long long rx;
	unsigned long long tx;
	unsigned long *rxs;
	unsigned long *txs;
	unsigned long rxmax;
	unsigned long txmax;
	unsigned long rxavg;
	unsigned long txavg;
	unsigned long rxmin;
	unsigned long txmin;
};

char *argv0;

static sig_atomic_t resize;

void sighandler(int sig) {
	if (sig == SIGWINCH)
		resize = 1;
}

void eprintf(const char *fmt, ...) {
	va_list ap;

	endwin();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void *emalloc(size_t size) {
	void *p;

	p = malloc(size);
	if (!p)
		eprintf("out of memory\n");
	return p;
}

void *ecalloc(size_t nmemb, size_t size) {
	void *p;

	p = calloc(nmemb, size);
	if (!p)
		eprintf("out of memory\n");
	return p;
}

long estrtol(const char *str) {
	char *ep;
	long l;

	l = strtol(str, &ep, 10);
	if (!l || *ep != '\0' || ep == str)
		eprintf("invalid number: %s\n", str);
	return l;
}

double estrtod(const char *str) {
	char *ep;
	double d;

	d = strtod(str, &ep);
	if (!d || *ep != '\0' || ep == str)
		eprintf("invalid number: %s\n", str);
	return d;
}

size_t strlcpy(char *dest, const char *src, size_t size) {
	size_t len;

	len = strlen(src);

	if (size) {
		if (len >= size)
			size -= 1;
		else
			size = len;
		strncpy(dest, src, size);
		dest[size] = '\0';
	}

	return size;
}

unsigned long arrayavg(unsigned long *array, size_t n) {
	size_t i;
	unsigned long sum = 0;

	for (i = 0; i < n; i++)
		sum += array[i];
	sum /= n;
	return sum;
}

unsigned long arraymax(unsigned long *array, size_t n) {
	size_t i;
	unsigned long max = 0;

	for (i = 0; i < n; i++)
		if (array[i] > max)
			max = array[i];
	return max;
}

unsigned long arraymin(unsigned long *array, size_t n) {
	size_t i;
	unsigned long min;

	min = ULONG_MAX;

	for (i = 0; i < n; i++)
		if (array[i] < min)
			min = array[i];
	return min;
}

bool detectiface(char *ifname) {
	bool retval = false;
	struct ifaddrs *ifas, *ifa;

	if (getifaddrs(&ifas) == -1)
		return false;

	if (*ifname) {
		for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
			if (!strncmp(ifname, ifa->ifa_name, IFNAMSIZ)) {
				retval = true;
				break;
			}
		}
	} else {
		for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
			if (ifa->ifa_flags & IFF_LOOPBACK)
				continue;
			if (ifa->ifa_flags & IFF_RUNNING) {
				if (ifa->ifa_flags & IFF_UP) {
					strlcpy(ifname, ifa->ifa_name, IFNAMSIZ);
					retval = true;
					break;
				}
			}
		}
	}

	freeifaddrs(ifas);

	return retval;
}

#ifdef __linux__
static bool getcounters(char *ifname, unsigned long long *rx, unsigned long long *tx) {
	struct ifaddrs *ifas, *ifa;
	struct rtnl_link_stats *stats;

	stats = NULL;

	if (getifaddrs(&ifas) == -1)
		return false;

	for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
		if (!strcmp(ifa->ifa_name, ifname)) {
			if (ifa->ifa_addr == NULL)
				return false;
			if (ifa->ifa_addr->sa_family == AF_PACKET && ifa->ifa_data != NULL) {
				stats = ifa->ifa_data;
				*rx = stats->rx_bytes;
				*tx = stats->tx_bytes;
				break;
			}
		}
	}

	freeifaddrs(ifas);

	if (!stats)
		return false;
	return true;
}

#elif __OpenBSD__ || __NetBSD__
static bool getcounters(char *ifname, unsigned long long *rx, unsigned long long *tx) {
	int mib[6];
	char *buf, *next;
	size_t size;
	struct if_msghdr *ifm;
	struct sockaddr_dl *sdl;

	sdl = NULL;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;		/* protocol */
	mib[3] = 0;		/* wildcard address family */
	mib[4] = NET_RT_IFLIST;	/* no flags */
	mib[5] = 0;

	if (sysctl(mib, 6, NULL, &size, NULL, 0) < 0)
		return false;
	buf = emalloc(size);
	if (sysctl(mib, 6, buf, &size, NULL, 0) < 0)
		return false;

	for (next = buf; next < buf + size; next += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *)next;
		if (ifm->ifm_type != RTM_NEWADDR) {
			if (ifm->ifm_flags & IFF_UP) {
				sdl = (struct sockaddr_dl *)(ifm + 1);
				/* search for the right network interface */
				if (sdl->sdl_family != AF_LINK)
					continue;
				if (strncmp(sdl->sdl_data, ifname, sdl->sdl_nlen) != 0)
					continue;
				*rx = ifm->ifm_data.ifi_ibytes;
				*tx = ifm->ifm_data.ifi_obytes;
				break;
			}
		}
	}

	free(buf);

	if (!sdl)
		return false;
	return true;
}
#endif

bool getdata(struct iface *ifa, double delay, int cols) {
	static unsigned long long rx, tx;

	if (rx && tx && !resize) {
		if (!getcounters(ifa->ifname, &ifa->rx, &ifa->tx))
			return false;

		memmove(ifa->rxs, ifa->rxs+1, sizeof(ifa->rxs) * (cols - 1));
		memmove(ifa->txs, ifa->txs+1, sizeof(ifa->txs) * (cols - 1));

		ifa->rxs[cols - 1] = (ifa->rx - rx) / delay;
		ifa->txs[cols - 1] = (ifa->tx - tx) / delay;

		ifa->rxmax = arraymax(ifa->rxs, cols);
		ifa->txmax = arraymax(ifa->txs, cols);

		ifa->rxavg = arrayavg(ifa->rxs, cols);
		ifa->txavg = arrayavg(ifa->txs, cols);

		ifa->rxmin = arraymin(ifa->rxs, cols);
		ifa->txmin = arraymin(ifa->txs, cols);
	}

	if (!getcounters(ifa->ifname, &rx, &tx))
		return false;
	return true;
}

size_t arrayresize(unsigned long **array, size_t newsize, size_t oldsize) {
	unsigned long *arraytmp;

	if (newsize == oldsize)
		return false;

	arraytmp = *array;
	*array = ecalloc(newsize, sizeof(**array));

	if (newsize > oldsize)
		memcpy(*array+(newsize-oldsize), arraytmp, sizeof(**array) * oldsize);
	else
		memcpy(*array, arraytmp+(oldsize-newsize), sizeof(**array) * newsize);

	free(arraytmp);

	return newsize;
}

char *bytestostr(double bytes, bool siunits) {
	int i;
	static char str[32];
	static const char iec[][4] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB" };
	static const char si[][3] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
	const char *unit;
	char *fmt;
	double prefix;

	prefix = siunits ? 1000.0 : 1024.0;

	for (i = 0; bytes >= prefix && i < 9; i++)
		bytes /= prefix;

	fmt = i ? "%.2f %s" : "%.0f %s";
	unit = siunits ? si[i] : iec[i];
	snprintf(str, sizeof(str), fmt, bytes, unit);

	return str;
}

void printgraphw(WINDOW *win, char *name,
		unsigned long *array, unsigned long min, unsigned long max,
		bool siunits, bool minimum,
		int lines, int cols, int color) {
	int y, x;
	double height;

	werase(win);

	box(win, 0, 0);
	mvwvline(win, 0, 1, '-', lines-1);
	if (name)
		mvwprintw(win, 0, cols - 5 - strlen(name), "[ %s ]",name);
	mvwprintw(win, 0, 1, "[ %s/s ]", bytestostr(max, siunits));
	if (minimum)
		mvwprintw(win, lines-1, 1, "[ %s/s ]", bytestostr(min, siunits));
	else
		mvwprintw(win, lines-1, 1, "[ %s/s ]", bytestostr(0, siunits));

	wattron(win, color);
	for (y = 0; y < (lines - 2); y++) {
		for (x = 0; x < (cols - 3); x++) {
			if (array[x] && max) {
				if (minimum)
					height = lines - 3 - (((double) array[x] - min) / (max - min) * lines);
				else
					height = lines - 3 - ((double) array[x] / max * lines);

				if (height < y)
					mvwaddch(win, y + 1, x + 2, '*');
			}
		}
	}
	wattroff(win, color);

	wnoutrefresh(win);
}

void printrightedgew(WINDOW *win, int line, int cols, const char *fmt, ...) {
	va_list ap;
	char buf[BUFSIZ];

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZ, fmt, ap);
	va_end(ap);

	mvwprintw(win, line, cols - strlen(buf), "%s", buf);
}

void printcenterw(WINDOW *win, int line, int cols, const char *fmt, ...) {
	va_list ap;
	char buf[BUFSIZ];

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZ, fmt, ap);
	va_end(ap);

	mvwprintw(win, line, cols / 2 - strlen(buf) / 2, "%s", buf);
}

void printstatsw(WINDOW *win, char *name,
		unsigned long cur, unsigned long min, unsigned long avg,
		unsigned long max, unsigned long long total,
		bool siunits, int cols) {
	werase(win);

	box(win, 0, 0);
	if (name)
		mvwprintw(win, 0, 1, "[ %s ]", name);

	mvwprintw(win, 1, 1, "current:");
	printrightedgew(win, 1, cols - 1, "%s/s", bytestostr(cur, siunits));

	mvwprintw(win, 2, 1, "maximum:");
	printrightedgew(win, 2, cols - 1, "%s/s", bytestostr(max, siunits));

	mvwprintw(win, 3, 1, "average:");
	printrightedgew(win, 3, cols - 1, "%s/s", bytestostr(avg, siunits));

	mvwprintw(win, 4, 1, "minimum:");
	printrightedgew(win, 4, cols - 1, "%s/s", bytestostr(min, siunits));

	mvwprintw(win, 5, 1, "total:");
	printrightedgew(win, 5, cols - 1, "%s", bytestostr(total, siunits));

	wnoutrefresh(win);
}

void usage(void) {
	eprintf("usage: %s [options]\n"
			"\n"
			"-h    help\n"
			"-v    version\n"
			"-C    no colors\n"
			"-s    use SI units\n"
			"-m    scale graph minimum\n"
			"\n"
			"-d <seconds>      redraw delay\n"
			"-i <interface>    network interface\n"
			, argv0);
}

int main(int argc, char **argv) {
	int key;
	int colsold;
	int graphlines;
	struct iface ifa;
	WINDOW *title, *rxgraph, *txgraph, *rxstats, *txstats;

	bool colors = true;
	bool siunits = false;
	bool minimum = false;
	double delay = 0.5;

	memset(&ifa, 0, sizeof(ifa));

	ARGBEGIN {
	case 'v':
		eprintf("nbwmon-%s\n", VERSION);
	case 'C':
		colors = false;
		break;
	case 's':
		siunits = true;
		break;
	case 'm':
		minimum = true;
		break;
	case 'd':
		delay = estrtod(EARGF(usage()));
		break;
	case 'i':
		strlcpy(ifa.ifname, EARGF(usage()), IFNAMSIZ);
		break;
	default:
		usage();
	} ARGEND;

	if (!detectiface(ifa.ifname))
		eprintf("can't find network interface\n");

	initscr();
	curs_set(0);
	noecho();
	keypad(stdscr, TRUE);
	timeout(delay * 1000);
	if (colors && has_colors()) {
		start_color();
		use_default_colors();
		init_pair(1, COLOR_GREEN, -1);
		init_pair(2, COLOR_RED, -1);
	}

	signal(SIGWINCH, sighandler);
	mvprintw(0, 0, "collecting data from %s for %.2f seconds\n", ifa.ifname, delay);

	ifa.rxs = ecalloc(COLS - 3, sizeof(*ifa.rxs));
	ifa.txs = ecalloc(COLS - 3, sizeof(*ifa.txs));

	graphlines = (LINES - 8) / 2;

	title = newwin(1, COLS, 0, 0);
	rxgraph = newwin(graphlines, COLS, 1, 0);
	txgraph = newwin(graphlines, COLS, graphlines + 1, 0);
	rxstats = newwin(LINES - (graphlines * 2 + 1), COLS / 2, graphlines * 2 + 1, 0);
	txstats = newwin(LINES - (graphlines * 2 + 1), COLS - COLS / 2, graphlines * 2 + 1, COLS / 2);

	if (!getdata(&ifa, delay, COLS - 3))
		eprintf("can't read rx and tx bytes for %s\n", ifa.ifname);

	while ((key = getch()) != 'q') {
		if (key != ERR)
			resize = 1;

		if (!getdata(&ifa, delay, COLS - 3))
			eprintf("can't read rx and tx bytes for %s\n", ifa.ifname);

		if (resize) {
			colsold = COLS;
			endwin();
			refresh();

			arrayresize(&ifa.rxs, COLS - 3, colsold - 3);
			arrayresize(&ifa.txs, COLS - 3, colsold - 3);

			graphlines = (LINES - 8) / 2;

			wresize(title, 1, COLS);
			wresize(rxgraph, graphlines, COLS);
			wresize(txgraph, graphlines, COLS);
			wresize(rxstats, LINES - (graphlines * 2 + 1), COLS / 2);
			wresize(txstats, LINES - (graphlines * 2 + 1), COLS - COLS / 2);
			mvwin(txgraph, graphlines + 1, 0);
			mvwin(rxstats, graphlines * 2 + 1, 0);
			mvwin(txstats, graphlines * 2 + 1, COLS / 2);

			resize = 0;
		}

		werase(title);
		printcenterw(title, 0, COLS, "[ nbwmon-%s interface: %s ]", VERSION, ifa.ifname);
		wnoutrefresh(title);

		printgraphw(rxgraph, "Received", ifa.rxs, ifa.rxmin, ifa.rxmax,
				siunits, minimum,
				graphlines, COLS, COLOR_PAIR(1));
		printgraphw(txgraph, "Transmitted", ifa.txs, ifa.txmin, ifa.txmax,
				siunits, minimum,
				graphlines, COLS, COLOR_PAIR(2));

		printstatsw(rxstats, "Received",
				ifa.rxs[COLS - 4], ifa.rxmin, ifa.rxavg, ifa.rxmax, ifa.rx,
				siunits, COLS / 2);
		printstatsw(txstats, "Transmitted",
				ifa.txs[COLS - 4], ifa.txmin, ifa.txavg, ifa.txmax, ifa.tx,
				siunits, COLS - COLS / 2);

		doupdate();
	}

	delwin(title);
	delwin(rxgraph);
	delwin(txgraph);
	delwin(rxstats);
	delwin(txstats);
	endwin();

	free(ifa.rxs);
	free(ifa.txs);

	return EXIT_SUCCESS;
}
