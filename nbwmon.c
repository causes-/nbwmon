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

#define VERSION "0.3"

enum {
	SIUNITS = 1 << 0,
	NOCOLORS = 1 << 1,
	FIXEDLINES = 1 << 2,
	KEYPRESSED = 1 << 3,
	UGMAX = 1 << 4,
};

struct iface {
	char ifname[IFNAMSIZ];
	long long rx;
	long long tx;
	double *rxs;
	double *txs;
	double rxmax;
	double txmax;
	double rxgraphmax;
	double txgraphmax;
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

char *astrncpy(char *dest, const char *src, size_t n) {
	char *p = strncpy(dest, src, n-1);
	dest[n-1] = '\0';
	return p;
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
				astrncpy(ifname, ifa->ifa_name, IFNAMSIZ);
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

void scaledata(double raw, int opts, double *data, const char **unit) {
	int i;
	double prefix;
	static const char iec[][4] = { "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB" };
	static const char si[][3] = { "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };

	prefix = (opts & SIUNITS) ? 1000.0 : 1024.0;
	*data = raw;
	for (i = 0; *data >= prefix && i < 8; i++)
		*data /= prefix;
	*unit = (opts & SIUNITS) ? si[i] : iec[i];
}

void printrxsw(WINDOW *win, double *rxs, double graphmax, int lines, int cols, int color, int opts) {
	int y, x;
	double data;
	const char *unit;
	wmove(win, 0, 0);
	wattron(win, color);
	for (y = lines-1; y >= 0; y--)
		for (x = 0; x < cols; x++)
			if (rxs[x] / graphmax * lines > y)
				waddch(win, '*');
			else if (x == 0) {
				wattroff(win, color);
				waddch(win, '-');
				wattron(win, color);
			} else
				waddch(win, ' ');
	wattroff(win, color);
	scaledata(graphmax, opts, &data, &unit);
	mvwprintw(win, 0, 0, "%.2f %s/s", graphmax, unit);
	mvwprintw(win, lines-1, 0, "%.2f %s/s", 0.0, unit);
	wnoutrefresh(win);
}

void printstatsw(WINDOW *win, struct iface ifa, int cols, int opts) {
	int colrx, coltx;
	double data;
	const char *unit;
	char *fmt;
	int line = 0;

	werase(win);

	fmt = "%6s %.2f %s/s";
	colrx = cols / 4 - 8;
	coltx = colrx + cols / 2 + 1;
	scaledata(ifa.rxs[cols-1], opts, &data, &unit);
	mvwprintw(win, line, colrx, fmt, "RX:", data, unit);
	scaledata(ifa.txs[cols-1], opts, &data, &unit);
	mvwprintw(win, line++, coltx, fmt, "TX:", data, unit);

	scaledata(ifa.rxmax, opts, &data, &unit);
	mvwprintw(win, line, colrx, fmt, "max:", data, unit);
	scaledata(ifa.txmax, opts, &data, &unit);
	mvwprintw(win, line++, coltx, fmt, "max:", data, unit);

	scaledata(ifa.rx / 1024, opts, &data, &unit);
	mvwprintw(win, line, colrx, fmt, "total:", data, unit);
	scaledata(ifa.tx / 1024, opts, &data, &unit);
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
		eprintf("can't read rx and tx bytes\n");
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
		eprintf("can't read rx and tx bytes\n");

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
		eprintf("can't read rx and tx bytes\n");
}
#endif

double getlargest(double *rxs, int len) {
	int i;
	double max = 0;
	for (i = 0; i < len; i++)
		if (rxs[i] > max)
			max = rxs[i];
	return max;
}

void getdata(struct iface *ifa, double delay, int cols, int opts) {
	static long long rx, tx;
	double prefix;

	if (rx > 0 && tx > 0 && resize == 0 && !(opts & KEYPRESSED)) {
		getcounters(ifa->ifname, &ifa->rx, &ifa->tx);

		memmove(ifa->rxs, ifa->rxs+1, sizeof(double)*(cols-1));
		memmove(ifa->txs, ifa->txs+1, sizeof(double)*(cols-1));

		prefix = (opts & SIUNITS) ? 1000.0 : 1024.0;
		ifa->rxs[cols-1] = (ifa->rx - rx) / prefix / delay;
		ifa->txs[cols-1] = (ifa->tx - tx) / prefix / delay;

		if (ifa->rxs[cols-1] > ifa->rxmax)
			ifa->rxmax = ifa->rxs[cols-1];
		if (ifa->txs[cols-1] > ifa->txmax)
			ifa->txmax = ifa->txs[cols-1];

		ifa->rxgraphmax = getlargest(ifa->rxs, cols);
		ifa->txgraphmax = getlargest(ifa->txs, cols);

		if (opts & UGMAX) {
			if (ifa->rxgraphmax > ifa->txgraphmax)
				ifa->rxgraphmax = ifa->txgraphmax;
			else
				ifa->txgraphmax = ifa->rxgraphmax;
		}
	}

	getcounters(ifa->ifname, &rx, &tx);
}

int main(int argc, char *argv[]) {
	int i;
	int opts = 0;
	int linesold, colsold;
	int graphlines, statslines = 3;
	double delay = 1.0;
	char key = ERR;
	struct iface ifa;
	WINDOW *titlebar, *rxgraph, *txgraph, *stats;

	memset(&ifa, 0, sizeof ifa);

	for (i = 1; i < argc; i++) {
		if (!strcmp("-v", argv[i]))
			eprintf("%s %s\n", argv[0], VERSION);
		else if (!strcmp("-n", argv[i]))
			opts |= NOCOLORS;
		if (!strcmp("-s", argv[i]))
			opts |= SIUNITS;
		if (!strcmp("-u", argv[i]))
			opts |= UGMAX;
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
					"-l <lines>        fixed graph height\n", argv[0]);
		else if (!strcmp("-d", argv[i]))
			delay = strtod(argv[++i], NULL);
		else if (!strcmp("-i", argv[i]))
			astrncpy(ifa.ifname, argv[++i], IFNAMSIZ);
		else if (!strcmp("-l", argv[i])) {
			opts |= FIXEDLINES;
			graphlines = strtol(argv[++i], NULL, 10);
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
	if (!(opts & NOCOLORS) && has_colors()) {
		start_color();
		use_default_colors();
		init_pair(1, COLOR_GREEN, -1);
		init_pair(2, COLOR_RED, -1);
	}
	signal(SIGWINCH, sighandler);
	ifa.rxs = ecalloc(COLS, sizeof(double));
	ifa.txs = ecalloc(COLS, sizeof(double));

	if (!(opts & FIXEDLINES))
		graphlines = (LINES-statslines-1)/2;
	titlebar = newwin(1, COLS, 0, 0);
	rxgraph = newwin(graphlines, COLS, 1, 0);
	txgraph = newwin(graphlines, COLS, graphlines+1, 0);
	stats = newwin(statslines, COLS, LINES-statslines, 0);
	getdata(&ifa, delay, COLS, opts);

	while (key != 'q') {
		key = getch();
		if (key != ERR)
			opts |= KEYPRESSED;
		getdata(&ifa, delay, COLS, opts);

		if (resize || key == 'r') {
			linesold = LINES;
			colsold = COLS;
			endwin();
			refresh();
			clear();
			if (LINES != linesold && !(opts & FIXEDLINES))
				graphlines = (LINES-statslines-1)/2;
			scalerxs(&ifa.rxs, COLS, colsold);
			scalerxs(&ifa.txs, COLS, colsold);

			wresize(titlebar, 1, COLS);
			wresize(rxgraph, graphlines, COLS);
			wresize(txgraph, graphlines, COLS);
			wresize(stats, statslines, COLS);
			mvwin(txgraph, graphlines+1, 0);
			mvwin(stats, LINES-statslines, 0);
			werase(stats);
			resize = 0;
		}

		mvwprintw(titlebar, 0, COLS/2-7, "interface: %s\n", ifa.ifname);
		wnoutrefresh(titlebar);
		printrxsw(rxgraph, ifa.rxs, ifa.rxgraphmax, graphlines, COLS, COLOR_PAIR(1), opts);
		printrxsw(txgraph, ifa.txs, ifa.txgraphmax, graphlines, COLS, COLOR_PAIR(2), opts);
		printstatsw(stats, ifa, COLS, opts);
		doupdate();

		opts &= ~KEYPRESSED;
	}

	delwin(titlebar);
	delwin(rxgraph);
	delwin(txgraph);
	delwin(stats);
	endwin();
	return EXIT_SUCCESS;
}
