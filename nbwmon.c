#define _GNU_SOURCE

#ifdef __linux__
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
#include <signal.h>
#ifdef __NetBSD__
#include <ncurses/ncurses.h>
#else
#include <ncurses.h>
#endif
#include <ifaddrs.h>
#include <net/if.h>

#include "arg.h"

#define VERSION "0.4"

struct iface {
	char ifname[IFNAMSIZ];
	unsigned long long rx;
	unsigned long long tx;
	unsigned long *rxs;
	unsigned long *txs;
	unsigned long rxavg;
	unsigned long txavg;
	unsigned long rxmax;
	unsigned long txmax;
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
			if (ifa->ifa_addr->sa_family == AF_PACKET && ifa->ifa_data != NULL) {
				stats = ifa->ifa_data;
				*rx = stats->rx_bytes;
				*tx = stats->tx_bytes;
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
	size_t sz;
	struct if_msghdr *ifm;
	struct sockaddr_dl *sdl;

	buf = NULL;
	sdl = NULL;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;		/* protocol */
	mib[3] = 0;		/* wildcard address family */
	mib[4] = NET_RT_IFLIST;	/* no flags */
	mib[5] = 0;

	if (sysctl(mib, 6, NULL, &sz, NULL, 0) < 0)
		return false;
	buf = emalloc(sz);
	if (sysctl(mib, 6, buf, &sz, NULL, 0) < 0)
		return false;

	for (next = buf; next < buf + sz; next += ifm->ifm_msglen) {
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

		memmove(ifa->rxs, ifa->rxs+1, sizeof(long) * (cols - 1));
		memmove(ifa->txs, ifa->txs+1, sizeof(long) * (cols - 1));

		ifa->rxs[cols-1] = (ifa->rx - rx) / delay;
		ifa->txs[cols-1] = (ifa->tx - tx) / delay;

		ifa->rxavg = arrayavg(ifa->rxs, cols);
		ifa->txavg = arrayavg(ifa->txs, cols);

		ifa->rxmax = arraymax(ifa->rxs, cols);
		ifa->txmax = arraymax(ifa->txs, cols);
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
	*array = ecalloc(newsize, sizeof(long));

	if (newsize > oldsize)
		memcpy(*array+(newsize-oldsize), arraytmp, sizeof(long) * oldsize);
	else
		memcpy(*array, arraytmp+(oldsize-newsize), sizeof(long) * newsize);

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

	unit = siunits ? si[i] : iec[i];
	fmt = i ? "%.2f %s" : "%.0f %s";
	snprintf(str, sizeof(str), fmt, bytes, unit);

	return str;
}

void printgraphw(WINDOW *win, char *name,
		unsigned long *array, unsigned long max, bool siunits,
		int lines, int cols, int color) {

	int y, x;
	int barheight;
	int firstline;

	werase(win);

	box(win, 0, 0);
	mvwvline(win, 0, 1, '-', lines-1);
	if (name)
		mvwprintw(win, 0, cols-strlen(name)-1, name);
	mvwprintw(win, 0, 1, "[ %s/s ]", bytestostr(max, siunits));
	mvwprintw(win, lines-1, 1, "[ %s/s ]", bytestostr(0.0, siunits));

	lines -= 2;
	cols -= 3;
	firstline = lines - 1;

	wattron(win, color);
	for (y = 0; y < lines; y++) {
		for (x = 0; x < cols; x++) {
			if (array[x] && max) {
				barheight = firstline - ((double) array[x] / max * lines);

				if (barheight < y)
					mvwaddch(win, y + 1, x + 2, '*');
			}
		}
	}
	wattroff(win, color);

	wnoutrefresh(win);
}

void printstatsw(WINDOW *win, struct iface ifa, bool siunits, int cols) {
	int colrx, coltx;
	char *fmt;
	int line = 0;

	werase(win);

	fmt = "%6s %s/s";
	colrx = cols / 4 - 8;
	coltx = colrx + cols / 2 + 1;
	mvwprintw(win, line, colrx, fmt, "RX:", bytestostr(ifa.rxs[cols - 1], siunits));
	mvwprintw(win, line++, coltx, fmt, "TX:", bytestostr(ifa.txs[cols - 1], siunits));

	mvwprintw(win, line, colrx, fmt, "avg:", bytestostr(ifa.rxavg, siunits));
	mvwprintw(win, line++, coltx, fmt, "avg:", bytestostr(ifa.txavg, siunits));

	mvwprintw(win, line, colrx, fmt, "max:", bytestostr(ifa.rxmax, siunits));
	mvwprintw(win, line++, coltx, fmt, "max:", bytestostr(ifa.txmax, siunits));

	fmt = "%6s %s";
	mvwprintw(win, line, colrx, fmt, "total:", bytestostr(ifa.rx, siunits));
	mvwprintw(win, line++, coltx, fmt, "total:", bytestostr(ifa.tx, siunits));

	wnoutrefresh(win);
}

void usage(void) {
	eprintf("usage: %s [options]\n"
			"\n"
			"-h    help\n"
			"-v    version\n"
			"-C    no colors\n"
			"-s    use SI units\n"
			"\n"
			"-d <seconds>      redraw delay\n"
			"-i <interface>    network interface\n"
			"-l <lines>        fixed graph height\n"
			, argv0);
}

int main(int argc, char **argv) {
	char *arg;
	int key;
	int colsold;
	int graphlines;
	struct iface ifa;
	WINDOW *title, *rxgraph, *txgraph, *stats;

	bool colors = true;
	bool siunits = false;
	double delay = 0.5;

	memset(&ifa, 0, sizeof(ifa));

	ARGBEGIN {
	case 'v':
		eprintf("%s-%s\n", argv0, VERSION);
	case 'C':
		colors = false;
		break;
	case 's':
		siunits = true;
		break;
	case 'd':
		delay = estrtod(EARGF(usage()));
		break;
	case 'i':
		arg = EARGF(usage());
		strlcpy(ifa.ifname, arg, IFNAMSIZ);
		break;
	default:
		usage();
	} ARGEND;

	if (!detectiface(ifa.ifname))
		eprintf("can't detect network interface\n");

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

	ifa.rxs = ecalloc(COLS - 2, sizeof(*ifa.rxs));
	ifa.txs = ecalloc(COLS - 2, sizeof(*ifa.txs));

	graphlines = (LINES - 5) / 2;

	title = newwin(1, COLS, 0, 0);
	rxgraph = newwin(graphlines, COLS, 1, 0);
	txgraph = newwin(graphlines, COLS, graphlines + 1, 0);
	stats = newwin(LINES - (graphlines * 2 + 1), COLS, graphlines * 2 + 1, 0);

	if (!getdata(&ifa, delay, COLS-2))
		eprintf("can't read rx and tx bytes for %s\n", ifa.ifname);

	while ((key = getch()) != 'q') {
		if (key != ERR)
			resize = 1;

		if (!getdata(&ifa, delay, COLS-2))
			eprintf("can't read rx and tx bytes for %s\n", ifa.ifname);

		if (resize) {
			colsold = COLS;
			endwin();
			refresh();

			arrayresize(&ifa.rxs, COLS - 2, colsold - 2);
			arrayresize(&ifa.txs, COLS - 2, colsold - 2);

			graphlines = (LINES - 5) / 2;

			wresize(title, 1, COLS);
			wresize(rxgraph, graphlines, COLS);
			wresize(txgraph, graphlines, COLS);
			wresize(stats, LINES-(graphlines*2+1), COLS);
			mvwin(txgraph, graphlines+1, 0);
			mvwin(stats, graphlines*2+1, 0);

			resize = 0;
		}

		werase(title);
		mvwprintw(title, 0, COLS/2-8, "interface: %s\n", ifa.ifname);
		wnoutrefresh(title);
		printgraphw(rxgraph, "[ RX ]", ifa.rxs, ifa.rxmax, siunits,
				graphlines, COLS, COLOR_PAIR(1));
		printgraphw(txgraph, "[ TX ]", ifa.txs, ifa.txmax, siunits,
				graphlines, COLS, COLOR_PAIR(2));
		printstatsw(stats, ifa, siunits, COLS - 2);
		doupdate();
	}

	delwin(title);
	delwin(rxgraph);
	delwin(txgraph);
	delwin(stats);
	endwin();
	free(ifa.rxs);
	free(ifa.txs);
	return EXIT_SUCCESS;
}
