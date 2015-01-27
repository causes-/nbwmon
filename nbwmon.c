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
#error "Your platform is not supported"
#endif

#include <stdio.h>
#include <stdlib.h>
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
#include "util.h"

#define VERSION "0.5.2"

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

bool colors = true;
bool siunits = false;
bool minimum = false;
bool globalmax = false;
double delay = 0.5;

void sighandler(int sig) {
	if (sig == SIGWINCH)
		resize = 1;
}

unsigned long arraymax(unsigned long *array, size_t n, unsigned long max) {
	size_t i;

	for (i = 0; i < n; i++)
		if (array[i] > max)
			max = array[i];
	return max;
}

unsigned long arraymin(unsigned long *array, size_t n) {
	size_t i;
	unsigned long min = ULONG_MAX;

	for (i = 0; i < n; i++)
		if (array[i] < min)
			min = array[i];
	return min;
}

unsigned long arrayavg(unsigned long *array, size_t n) {
	size_t i;
	unsigned long sum = 0;

	for (i = 0; i < n; i++)
		sum += array[i];
	sum /= n;
	return sum;
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

char *bytestostr(double bytes) {
	int i;
	int cols;
	static char str[32];
	static const char iec[][4] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB" };
	static const char si[][3] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
	const char *unit;
	char *fmt;
	double prefix;

	if (siunits) {
		prefix = 1000.0;
		cols = LEN(si);
	} else {
		prefix = 1024.0;
		cols = LEN(iec);
	}

	for (i = 0; bytes >= prefix && i < cols; i++)
		bytes /= prefix;

	fmt = i ? "%.2f %s" : "%.0f %s";
	unit = siunits ? si[i] : iec[i];
	snprintf(str, sizeof(str), fmt, bytes, unit);

	return str;
}

void printrightedgew(WINDOW *win, const char *fmt, ...) {
	va_list ap;
	char buf[BUFSIZ];

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZ, fmt, ap);
	va_end(ap);

	mvwprintw(win, getcury(win), getmaxx(win) - 1 - strlen(buf), "%s", buf);
}

void printcenterw(WINDOW *win, const char *fmt, ...) {
	va_list ap;
	char buf[BUFSIZ];

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZ, fmt, ap);
	va_end(ap);

	mvwprintw(win, getcury(win), (getmaxx(win) - strlen(buf)) / 2, "%s", buf);
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
	struct rtnl_link_stats *stats = NULL;

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
	struct sockaddr_dl *sdl = NULL;

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

bool getdata(struct iface *ifa, int cols) {
	static unsigned long long rx, tx;

	if (rx && tx && !resize) {
		if (!getcounters(ifa->ifname, &ifa->rx, &ifa->tx))
			return false;

		memmove(ifa->rxs, ifa->rxs+1, sizeof(ifa->rxs) * (cols - 1));
		memmove(ifa->txs, ifa->txs+1, sizeof(ifa->txs) * (cols - 1));

		ifa->rxs[cols - 1] = (ifa->rx - rx) / delay;
		ifa->txs[cols - 1] = (ifa->tx - tx) / delay;

		if (globalmax) {
			ifa->rxmax = arraymax(ifa->rxs, cols, ifa->rxmax);
			ifa->txmax = arraymax(ifa->txs, cols, ifa->txmax);
		} else {
			ifa->rxmax = arraymax(ifa->rxs, cols, 0);
			ifa->txmax = arraymax(ifa->txs, cols, 0);
		}

		ifa->rxavg = arrayavg(ifa->rxs, cols);
		ifa->txavg = arrayavg(ifa->txs, cols);

		ifa->rxmin = arraymin(ifa->rxs, cols);
		ifa->txmin = arraymin(ifa->txs, cols);
	}

	if (!getcounters(ifa->ifname, &rx, &tx))
		return false;
	return true;
}

void printgraphw(WINDOW *win, char *name, int lines, int cols, int color,
		unsigned long *array, unsigned long min, unsigned long max) {
	int y, x;
	double height;

	werase(win);

	box(win, 0, 0);
	mvwvline(win, 0, 1, '-', lines-1);
	if (name)
		mvwprintw(win, 0, cols - 5 - strlen(name), "[ %s ]",name);
	mvwprintw(win, 0, 1, "[ %s/s ]", bytestostr(max));
	if (minimum)
		mvwprintw(win, lines-1, 1, "[ %s/s ]", bytestostr(min));
	else
		mvwprintw(win, lines-1, 1, "[ %s/s ]", bytestostr(0));

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

void printstatsw(WINDOW *win, char *name,
		unsigned long cur, unsigned long min, unsigned long avg,
		unsigned long max, unsigned long long total) {
	werase(win);

	box(win, 0, 0);
	if (name)
		mvwprintw(win, 0, 1, "[ %s ]", name);

	mvwprintw(win, 1, 1, "Current:");
	printrightedgew(win, "%s/s", bytestostr(cur));

	mvwprintw(win, 2, 1, "Maximum:");
	printrightedgew(win, "%s/s", bytestostr(max));

	mvwprintw(win, 3, 1, "Average:");
	printrightedgew(win, "%s/s", bytestostr(avg));

	mvwprintw(win, 4, 1, "Minimum:");
	printrightedgew(win, "%s/s", bytestostr(min));

	mvwprintw(win, 5, 1, "Total:");
	printrightedgew(win, "%s", bytestostr(total));

	wnoutrefresh(win);
}

void usage(void) {
	eprintf("usage: %s [options]\n"
			"\n"
			"-h    Help\n"
			"-v    Version\n"
			"-C    No colors\n"
			"-s    Use SI units\n"
			"-m    Scale graph minimum\n"
			"-g    Show global maximum\n"
			"\n"
			"-d <seconds>      Redraw delay\n"
			"-i <interface>    Network interface\n"
			, argv0);
}

int main(int argc, char **argv) {
	int key = ERR;
	int colsold;
	int graphlines;
	struct iface ifa;
	WINDOW *title, *rxgraph, *txgraph, *rxstats, *txstats;

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
	case 'g':
		globalmax = true;
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
		eprintf("Can't find network interface\n");

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
	mvprintw(0, 0, "Collecting data from %s for %.2f seconds\n", ifa.ifname, delay);

	ifa.rxs = ecalloc(COLS - 3, sizeof(*ifa.rxs));
	ifa.txs = ecalloc(COLS - 3, sizeof(*ifa.txs));

	graphlines = (LINES - 8) / 2;

	title = newwin(1, COLS, 0, 0);
	rxgraph = newwin(graphlines, COLS, 1, 0);
	txgraph = newwin(graphlines, COLS, graphlines + 1, 0);
	rxstats = newwin(LINES - (graphlines * 2 + 1), COLS / 2, graphlines * 2 + 1, 0);
	txstats = newwin(LINES - (graphlines * 2 + 1), COLS - COLS / 2, graphlines * 2 + 1, COLS / 2);

	do {
		if (key != ERR)
			resize = 1;

		if (!getdata(&ifa, COLS - 3))
			eprintf("Can't read rx and tx bytes for %s\n", ifa.ifname);

		if (COLS < 46 || LINES < 18) {
			if (resize) {
				endwin();
				erase();
				refresh();
				resize = 0;
			}
			werase(title);
			wprintw(title, "Terminal size too small (Minimum: 46x18)");
			wnoutrefresh(title);
			doupdate();
			continue;
		}

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
		printcenterw(title, "[ nbwmon-%s | Interface: %s ]", VERSION, ifa.ifname);
		wnoutrefresh(title);

		printgraphw(rxgraph, "Received", graphlines, COLS, COLOR_PAIR(1),
				ifa.rxs, ifa.rxmin, ifa.rxmax);
		printgraphw(txgraph, "Transmitted", graphlines, COLS, COLOR_PAIR(2),
				ifa.txs, ifa.txmin, ifa.txmax);

		printstatsw(rxstats, "Received",
				ifa.rxs[COLS - 4], ifa.rxmin, ifa.rxavg, ifa.rxmax, ifa.rx);
		printstatsw(txstats, "Transmitted",
				ifa.txs[COLS - 4], ifa.txmin, ifa.txavg, ifa.txmax, ifa.tx);

		doupdate();
	} while ((key = getch()) != 'q');

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
