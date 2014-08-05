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

size_t strlcpy(char *dest, const char *src, size_t size) {
	size_t len = 0;
	size_t slen = strlen(src);
	if (size) {
		len = slen >= size ? size - 1 : slen;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return len;
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

void printgraphw(WINDOW *win, double *rxs, double graphmax, int siunits, int lines, int cols, int color) {
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
	scaledata(graphmax, &data, &unit, siunits);
	mvwprintw(win, 0, 0, "%.2f %s/s", graphmax, unit);
	mvwprintw(win, lines-1, 0, "%.2f %s/s", 0.0, unit);
	wnoutrefresh(win);
}

void printstatsw(WINDOW *win, struct iface ifa, int siunits, int cols) {
	int colrx, coltx;
	double data;
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

	scaledata(ifa.rxmax, &data, &unit, siunits);
	mvwprintw(win, line, colrx, fmt, "max:", data, unit);
	scaledata(ifa.txmax, &data, &unit, siunits);
	mvwprintw(win, line++, coltx, fmt, "max:", data, unit);

	scaledata(ifa.rx / 1024, &data, &unit, siunits);
	mvwprintw(win, line, colrx, fmt, "total:", data, unit);
	scaledata(ifa.tx / 1024, &data, &unit, siunits);
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

void getdata(struct iface *ifa, int siunits, double delay, int cols, int syncgraphmax) {
	int i;
	static long long rx, tx;
	double prefix;

	if (rx > 0 && tx > 0 && resize == 0) {
		getcounters(ifa->ifname, &ifa->rx, &ifa->tx);

		memmove(ifa->rxs, ifa->rxs+1, sizeof(double)*(cols-1));
		memmove(ifa->txs, ifa->txs+1, sizeof(double)*(cols-1));

		prefix = siunits ? 1000.0 : 1024.0;
		ifa->rxs[cols-1] = (ifa->rx - rx) / prefix / delay;
		ifa->txs[cols-1] = (ifa->tx - tx) / prefix / delay;

		if (ifa->rxs[cols-1] > ifa->rxmax)
			ifa->rxmax = ifa->rxs[cols-1];
		if (ifa->txs[cols-1] > ifa->txmax)
			ifa->txmax = ifa->txs[cols-1];

		ifa->rxgraphmax = 0;
		ifa->txgraphmax = 0;
		for (i = 0; i < cols; i++) {
			if (ifa->rxs[i] > ifa->rxgraphmax)
				ifa->rxgraphmax = ifa->rxs[i];
			if (ifa->txs[i] > ifa->txgraphmax)
				ifa->txgraphmax = ifa->txs[i];
		}

		if (syncgraphmax) {
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
	int linesold, colsold;
	int graphlines = 0, statslines = 3;
	double delay = 1.0;
	char key = ERR;
	struct iface ifa;
	WINDOW *titlebar, *rxgraph, *txgraph, *stats;

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
			delay = strtod(argv[++i], NULL);
		else if (!strcmp("-i", argv[i]))
			strlcpy(ifa.ifname, argv[++i], IFNAMSIZ);
		else if (!strcmp("-l", argv[i])) {
			fixedlines = 1;
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

	if (fixedlines == 0)
		graphlines = (LINES-statslines-1)/2;
	titlebar = newwin(1, COLS, 0, 0);
	rxgraph = newwin(graphlines, COLS, 1, 0);
	txgraph = newwin(graphlines, COLS, graphlines+1, 0);
	stats = newwin(statslines, COLS, LINES-statslines, 0);
	getdata(&ifa, siunits, delay, COLS, syncgraphmax);

	while (key != 'q') {
		key = getch();
		if (resize || key != ERR) {
			linesold = LINES;
			colsold = COLS;
			endwin();
			refresh();
			clear();
			if (LINES != linesold && fixedlines == 0)
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

		getdata(&ifa, siunits, delay, COLS, syncgraphmax);

		mvwprintw(titlebar, 0, COLS/2-7, "interface: %s\n", ifa.ifname);
		wnoutrefresh(titlebar);
		printgraphw(rxgraph, ifa.rxs, ifa.rxgraphmax, siunits, graphlines, COLS, COLOR_PAIR(1));
		printgraphw(txgraph, ifa.txs, ifa.txgraphmax, siunits, graphlines, COLS, COLOR_PAIR(2));
		printstatsw(stats, ifa, siunits, COLS);
		doupdate();
	}

	delwin(titlebar);
	delwin(rxgraph);
	delwin(txgraph);
	delwin(stats);
	endwin();
	return EXIT_SUCCESS;
}
