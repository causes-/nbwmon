#define _GNU_SOURCE

#ifdef __linux__
#include <linux/if_link.h>
#elif __OpenBSD__
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
#include <ncurses.h>
#include <ifaddrs.h>
#include <net/if.h>

#define VERSION "0.4"

#define MAX(A,B) ((A) > (B) ? (A) : (B))

struct iface {
	char ifname[IFNAMSIZ];
	long long rx;
	long long tx;
	long *rxs;
	long *txs;
	long rxavg;
	long txavg;
	long rxmax;
	long txmax;
};

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
	size_t len = strlen(src);
	if (size) {
		if (len >= size)
			size -= 1;
		else
			size = len;
		memcpy(dest, src, size);
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

long arrayavg(long *array, size_t n) {
	int i;
	long sum = 0;
	for (i = 0; i < n; i++)
		sum += array[i];
	sum /= (n-1);
	return sum;
}

long arraymax(long *array, size_t n) {
	int i;
	long max = 0;
	for (i = 0; i < n; i++)
		if (array[i] > max)
			max = array[i];
	return max;
}

void detectiface(char *ifname) {
	struct ifaddrs *ifas, *ifa;

	if (getifaddrs(&ifas) == -1)
		eprintf("can't detect network interface\n");

	for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;
		if (ifa->ifa_flags & IFF_RUNNING)
			if (ifa->ifa_flags & IFF_UP) {
				strlcpy(ifname, ifa->ifa_name, IFNAMSIZ);
				freeifaddrs(ifas);
				return;
			}
	}
	eprintf("can't detect network interface\n");
}

#ifdef __linux__
void getcounters(char *ifname, long long *rx, long long *tx) {
	struct ifaddrs *ifas, *ifa;
	struct rtnl_link_stats *stats;

	*rx = -1;
	*tx = -1;

	if (getifaddrs(&ifas) == -1)
		return;
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

	if (*rx == -1 || *tx == -1)
		eprintf("can't read rx and tx bytes for %s\n", ifname);
}

#elif __OpenBSD__
void getcounters(char *ifname, long long *rx, long long *tx) {
	int mib[6];
	char *buf = NULL, *next;
	size_t sz;
	struct if_msghdr *ifm;
	struct sockaddr_dl *sdl;

	*rx = -1;
	*tx = -1;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;		/* protocol */
	mib[3] = 0;		/* wildcard address family */
	mib[4] = NET_RT_IFLIST;	/* no flags */
	mib[5] = 0;

	sysctl(mib, 6, NULL, &sz, NULL, 0);
	buf = emalloc(sz);
	if (sysctl(mib, 6, buf, &sz, NULL, 0) < 0)
		eprintf("can't read rx and tx bytes for %s\n", ifname);

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

	if (*rx == -1 || *tx == -1)
		eprintf("can't read rx and tx bytes for %s\n", ifname);
}
#endif

void getdata(struct iface *ifa, double delay, int cols) {
	static long long rx, tx;

	if (rx && tx && !resize) {
		getcounters(ifa->ifname, &ifa->rx, &ifa->tx);

		memmove(ifa->rxs, ifa->rxs+1, sizeof(long) * (cols-1));
		memmove(ifa->txs, ifa->txs+1, sizeof(long) * (cols-1));

		ifa->rxs[cols-1] = (ifa->rx - rx) / delay;
		ifa->txs[cols-1] = (ifa->tx - tx) / delay;

		ifa->rxavg = arrayavg(ifa->rxs, cols);
		ifa->txavg = arrayavg(ifa->txs, cols);

		ifa->rxmax = arraymax(ifa->rxs, cols);
		ifa->txmax = arraymax(ifa->txs, cols);
	}

	getcounters(ifa->ifname, &rx, &tx);
}

void arrayresize(long **array, size_t newsize, size_t oldsize) {
	long *arraytmp;
	if (newsize == oldsize)
		return;
	arraytmp = *array;
	*array = ecalloc(newsize, sizeof(long));
	if (newsize > oldsize)
		memcpy(*array+(newsize-oldsize), arraytmp, sizeof(long) * oldsize);
	else
		memcpy(*array, arraytmp+(oldsize-newsize), sizeof(long) * newsize);
	free(arraytmp);
}

void bytestostr(long in, char *out, bool siunits) {
	int i;
	double scaled;
	double prefix;
	const char *unit;
	char *fmt;
	static const char iec[][4] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB" };
	static const char si[][3] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };

	scaled = in;
	prefix = siunits ? 1000.0 : 1024.0;
	for (i = 0; scaled >= prefix && i < 9; i++)
		scaled /= prefix;
	unit = siunits ? si[i] : iec[i];
	fmt = i ? "%.2f %s" : "%.0f %s";
	sprintf(out, fmt, scaled, unit);
}

void printgraphw(WINDOW *win, long *array, double max, bool siunits,
		int lines, int cols, bool hidescale, int color) {
	int y, x;
	char datastr[16];

	werase(win);

	if (!hidescale) {
		wborder(win, '-', ' ', ' ', ' ', ' ', ' ', ' ', ' ');
		bytestostr(max, datastr, siunits);
		mvwprintw(win, 0, 0, "%s/s", datastr);
		bytestostr(0.0, datastr, siunits);
		mvwprintw(win, lines-1, 0, "%s/s", 0.0, datastr);
	}

	wattron(win, color);
	for (y = 0; y < lines; y++)
		for (x = 0; x < cols; x++)
			if (lines - 1 - array[x] / max * lines  < y)
				mvwaddch(win, y, x, '*');
	wattroff(win, color);

	wnoutrefresh(win);
}

void printstatsw(WINDOW *win, struct iface ifa, bool siunits, int cols) {
	int colrx, coltx;
	char datastr[16];
	char *fmt;
	int line = 0;

	werase(win);

	fmt = "%6s %s/s";
	colrx = cols / 4 - 8;
	coltx = colrx + cols / 2 + 1;
	bytestostr(ifa.rxs[cols-1], datastr, siunits);
	mvwprintw(win, line, colrx, fmt, "RX:", datastr);
	bytestostr(ifa.txs[cols-1], datastr, siunits);
	mvwprintw(win, line++, coltx, fmt, "TX:", datastr);

	bytestostr(ifa.rxavg, datastr, siunits);
	mvwprintw(win, line, colrx, fmt, "avg:", datastr);
	bytestostr(ifa.txavg, datastr, siunits);
	mvwprintw(win, line++, coltx, fmt, "avg:", datastr);

	bytestostr(ifa.rxmax, datastr, siunits);
	mvwprintw(win, line, colrx, fmt, "max:", datastr);
	bytestostr(ifa.txmax, datastr, siunits);
	mvwprintw(win, line++, coltx, fmt, "max:", datastr);

	fmt = "%6s %s";
	bytestostr(ifa.rx, datastr, siunits);
	mvwprintw(win, line, colrx, fmt, "total:", datastr);
	bytestostr(ifa.tx, datastr, siunits);
	mvwprintw(win, line++, coltx, fmt, "total:", datastr);

	wnoutrefresh(win);
}

int main(int argc, char *argv[]) {
	int i;
	int linesold, colsold;
	int graphlines = 0;
	int statslines = 4;
	double delay = 0.5;
	char key;
	struct iface ifa;
	WINDOW *title, *rxgraph, *txgraph, *stats;

	bool colors = true;
	bool siunits = false;
	bool hidescale = false;
	bool syncgraphmax = false;
	bool fixedlines = false;

	memset(&ifa, 0, sizeof ifa);

	for (i = 1; i < argc; i++) {
		if (!strcmp("-v", argv[i]))
			eprintf("%s-%s\n", argv[0], VERSION);
		else if (!strcmp("-n", argv[i]))
			colors = false;
		else if (!strcmp("-s", argv[i]))
			siunits = true;
		else if (!strcmp("-S", argv[i]))
			hidescale = true;
		else if (!strcmp("-u", argv[i]))
			syncgraphmax = true;
		else if (argv[i+1] == NULL || argv[i+1][0] == '-')
			eprintf("usage: %s [options]\n"
					"\n"
					"-h    help\n"
					"-v    version\n"
					"-n    no colors\n"
					"-s    use SI units\n"
					"-S    hide graph scale\n"
					"-u    unified graph max\n"
					"\n"
					"-i <interface>    network interface\n"
					"-d <seconds>      redraw delay\n"
					"-l <lines>        fixed graph height\n"
					, argv[0]);
		else if (!strcmp("-d", argv[i]))
			delay = estrtod(argv[++i]);
		else if (!strcmp("-i", argv[i]))
			strlcpy(ifa.ifname, argv[++i], IFNAMSIZ);
		else if (!strcmp("-l", argv[i])) {
			graphlines = estrtol(argv[++i]);
			fixedlines = true;
		}
	}
	if (ifa.ifname[0] == '\0')
		detectiface(ifa.ifname);

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
	ifa.rxs = ecalloc(COLS, sizeof(long));
	ifa.txs = ecalloc(COLS, sizeof(long));
	mvprintw(0, 0, "collecting data from %s for %.2f seconds\n", ifa.ifname, delay);

	if (!fixedlines)
		graphlines = (LINES-1-statslines)/2;
	title = newwin(1, COLS, 0, 0);
	rxgraph = newwin(graphlines, COLS, 1, 0);
	txgraph = newwin(graphlines, COLS, graphlines+1, 0);
	stats = newwin(LINES-(graphlines*2+1), COLS, graphlines*2+1, 0);
	getdata(&ifa, delay, COLS);

	while ((key = getch()) != 'q') {
		if (key != ERR)
			resize = 1;

		getdata(&ifa, delay, COLS);
		if (syncgraphmax)
			ifa.rxmax = ifa.txmax = MAX(ifa.rxmax, ifa.txmax);

		if (resize) {
			linesold = LINES;
			colsold = COLS;
			endwin();
			refresh();

			if (COLS != colsold) {
				arrayresize(&ifa.rxs, COLS, colsold);
				arrayresize(&ifa.txs, COLS, colsold);
			}
			if (LINES != linesold && !fixedlines)
				graphlines = (LINES-1-statslines)/2;

			wresize(title, 1, COLS);
			wresize(rxgraph, graphlines, COLS);
			wresize(txgraph, graphlines, COLS);
			wresize(stats, LINES-(graphlines*2+1), COLS);
			mvwin(txgraph, graphlines+1, 0);
			mvwin(stats, graphlines*2+1, 0);
			resize = 0;
		}

		werase(title);
		mvwprintw(title, 0, COLS/2-7, "interface: %s\n", ifa.ifname);
		wnoutrefresh(title);
		printgraphw(rxgraph, ifa.rxs, ifa.rxmax, siunits, graphlines, COLS, hidescale, COLOR_PAIR(1));
		printgraphw(txgraph, ifa.txs, ifa.txmax, siunits, graphlines, COLS, hidescale, COLOR_PAIR(2));
		printstatsw(stats, ifa, siunits, COLS);
		doupdate();
	}

	delwin(title);
	delwin(rxgraph);
	delwin(txgraph);
	delwin(stats);
	endwin();
	return EXIT_SUCCESS;
}
