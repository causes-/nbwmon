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
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <ncurses.h>
#include <ifaddrs.h>
#include <net/if.h>

#define VERSION "0.3.1"

#define MAX(A,B) ((A) > (B) ? (A) : (B))

struct iface {
	char ifname[IFNAMSIZ];
	long long rx;
	long long tx;
	double *rxs;
	double *txs;
	double rxavg;
	double txavg;
	double rxmax;
	double txmax;
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

void *emalloc(size_t size) {
	void *p = malloc(size);
	if (!p)
		eprintf("out of memory\n");
	return p;
}

void *ecalloc(size_t nmemb, size_t size) {
	void *p = calloc(nmemb, size);
	if (!p)
		eprintf("out of memory\n");
	return p;
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

void detectiface(char *ifname) {
	struct ifaddrs *ifas, *ifa;
	if (getifaddrs(&ifas) == -1)
		return;
	for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;
		if (ifa->ifa_flags & IFF_RUNNING)
			if (ifa->ifa_flags & IFF_UP) {
				strlcpy(ifname, ifa->ifa_name, IFNAMSIZ);
				break;
			}
	}
	freeifaddrs(ifas);
}

void scalerxs(double **rxs, int cols, int colsold) {
	double *rxstmp;
	if (cols == colsold)
		return;
	rxstmp = *rxs;
	*rxs = ecalloc(cols, sizeof(double));
	if (cols > colsold)
		memcpy(*rxs+(cols-colsold), rxstmp, sizeof(double)*colsold);
	else
		memcpy(*rxs, rxstmp+(colsold-cols), sizeof(double)*cols);
	free(rxstmp);
}

void scaledata(double raw, double *data, const char **unit, int siunits) {
	int i;
	double prefix;
	static const char iec[][4] = { "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB" };
	static const char si[][3] = { "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };

	prefix = siunits ? 1000.0 : 1024.0;
	*data = raw;
	for (i = 0; *data >= prefix && i < 8; i++)
		*data /= prefix;
	*unit = siunits ? si[i] : iec[i];
}

void printgraphw(WINDOW *win, double *rxs, double max, int siunits, int lines, int cols, int color) {
	int y, x;
	double data;
	const char *unit;
	werase(win);
	wborder(win, '-', ' ', ' ', ' ', ' ', ' ', ' ', ' ');
	scaledata(max, &data, &unit, siunits);
	mvwprintw(win, 0, 0, "%.2f %s/s", data, unit);
	mvwprintw(win, lines-1, 0, "%.1f %s/s", 0.0, unit);
	wattron(win, color);
	for (y = 0; y < lines; y++)
		for (x = 0; x < cols; x++)
			if (lines - 1 - rxs[x] / max * lines  < y)
				mvwaddch(win, y, x, '*');
	wattroff(win, color);
	wnoutrefresh(win);
}

void printstatsw(WINDOW *win, struct iface ifa, int siunits, int cols) {
	int colrx, coltx;
	double data;
	double prefix;
	const char *unit;
	char *fmt;
	int line = 0;

	werase(win);

	fmt = "%6s %.2f %s/s";
	colrx = cols / 4 - 8;
	coltx = colrx + cols / 2 + 1;
	scaledata(ifa.rxs[cols-1], &data, &unit, siunits);
	mvwprintw(win, line, colrx, fmt, "RX:", data, unit);
	scaledata(ifa.txs[cols-1], &data, &unit, siunits);
	mvwprintw(win, line++, coltx, fmt, "TX:", data, unit);

	scaledata(ifa.rxavg, &data, &unit, siunits);
	mvwprintw(win, line, colrx, fmt, "avg:", data, unit);
	scaledata(ifa.txavg, &data, &unit, siunits);
	mvwprintw(win, line++, coltx, fmt, "avg:", data, unit);

	scaledata(ifa.rxmax, &data, &unit, siunits);
	mvwprintw(win, line, colrx, fmt, "max:", data, unit);
	scaledata(ifa.txmax, &data, &unit, siunits);
	mvwprintw(win, line++, coltx, fmt, "max:", data, unit);

	fmt = "%6s %.2f %s";
	prefix = siunits ? 1000.0 : 1024.0;
	scaledata(ifa.rx / prefix, &data, &unit, siunits);
	mvwprintw(win, line, colrx, fmt, "total:", data, unit);
	scaledata(ifa.tx / prefix, &data, &unit, siunits);
	mvwprintw(win, line++, coltx, fmt, "total:", data, unit);

	wnoutrefresh(win);
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

double avgrxs(double *rxs, int cols) {
	int i;
	double sum = 0;
	for (i = 0; i < cols; i++)
		sum += rxs[i];
	sum /= (cols-1);
	return sum;
}

double maxrxs(double *rxs, int cols) {
	int i;
	double max = 0;
	for (i = 0; i < cols; i++)
		if (rxs[i] > max)
			max = rxs[i];
	return max;
}

void getdata(struct iface *ifa, int siunits, double delay, int cols) {
	static long long rx, tx;
	double prefix;

	if (rx && tx && !resize) {
		getcounters(ifa->ifname, &ifa->rx, &ifa->tx);

		memmove(ifa->rxs, ifa->rxs+1, sizeof(double)*(cols-1));
		memmove(ifa->txs, ifa->txs+1, sizeof(double)*(cols-1));

		prefix = siunits ? 1000.0 : 1024.0;
		ifa->rxs[cols-1] = (ifa->rx - rx) / prefix / delay;
		ifa->txs[cols-1] = (ifa->tx - tx) / prefix / delay;

		ifa->rxmax = maxrxs(ifa->rxs, cols);
		ifa->txmax = maxrxs(ifa->txs, cols);

		ifa->rxavg = avgrxs(ifa->rxs, cols);
		ifa->txavg = avgrxs(ifa->txs, cols);
	}

	getcounters(ifa->ifname, &rx, &tx);
}

int main(int argc, char *argv[]) {
	int i;
	int linesold, colsold;
	int graphlines = 0;
	int statslines = 4;
	double delay = 1.0;
	char key;
	struct iface ifa;
	WINDOW *title, *rxgraph, *txgraph, *stats;

	int siunits = 0;
	int colors = 1;
	int fixedlines = 0;
	int syncgraphmax = 0;

	memset(&ifa, 0, sizeof ifa);

	for (i = 1; i < argc; i++) {
		if (!strcmp("-v", argv[i]))
			eprintf("%s %s\n", argv[0], VERSION);
		else if (!strcmp("-n", argv[i]))
			colors = 0;
		else if (!strcmp("-s", argv[i]))
			siunits = 1;
		else if (!strcmp("-u", argv[i]))
			syncgraphmax = 1;
		else if (argv[i+1] == NULL || argv[i+1][0] == '-')
			eprintf("usage: %s [options]\n"
					"\n"
					"-h    help\n"
					"-v    version\n"
					"-n    no colors\n"
					"-s    use SI units\n"
					"-u    unified graphmax\n"
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
			fixedlines = 1;
			graphlines = estrtol(argv[++i]);
		}
	}

	if (ifa.ifname[0] == '\0')
		detectiface(ifa.ifname);
	if (ifa.ifname[0] == '\0')
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
	ifa.rxs = ecalloc(COLS, sizeof(double));
	ifa.txs = ecalloc(COLS, sizeof(double));
	mvprintw(0, 0, "collecting data from %s for %.2f seconds\n", ifa.ifname, delay);

	if (!fixedlines)
		graphlines = (LINES-1-statslines)/2;
	title = newwin(1, COLS, 0, 0);
	rxgraph = newwin(graphlines, COLS, 1, 0);
	txgraph = newwin(graphlines, COLS, graphlines+1, 0);
	stats = newwin(statslines, COLS, LINES-statslines, 0);
	getdata(&ifa, siunits, delay, COLS);

	while ((key = getch()) != 'q') {
		if (key != ERR)
			resize = 1;
		getdata(&ifa, siunits, delay, COLS);
		if (syncgraphmax)
			ifa.rxmax = ifa.txmax = MAX(ifa.rxmax, ifa.txmax);

		if (resize) {
			linesold = LINES;
			colsold = COLS;
			endwin();
			refresh();

			if (COLS != colsold) {
				scalerxs(&ifa.rxs, COLS, colsold);
				scalerxs(&ifa.txs, COLS, colsold);
			}
			if (LINES != linesold && fixedlines == 0)
				graphlines = (LINES-statslines-1)/2;

			wresize(title, 1, COLS);
			wresize(rxgraph, graphlines, COLS);
			wresize(txgraph, graphlines, COLS);
			wresize(stats, statslines, COLS);
			mvwin(txgraph, graphlines+1, 0);
			mvwin(stats, LINES-statslines, 0);
			resize = 0;
		}

		werase(title);
		mvwprintw(title, 0, COLS/2-7, "interface: %s\n", ifa.ifname);
		wnoutrefresh(title);
		printgraphw(rxgraph, ifa.rxs, ifa.rxmax, siunits, graphlines, COLS, COLOR_PAIR(1));
		printgraphw(txgraph, ifa.txs, ifa.txmax, siunits, graphlines, COLS, COLOR_PAIR(2));
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
