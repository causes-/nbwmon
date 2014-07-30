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

#define NOCOLORS	(1 << 0)
#define FIXEDLINES	(1 << 1)
#define SIUNITS		(1 << 2)
#define KEYPRESSED	(1 << 3)

struct iface {
	char ifname[IFNAMSIZ];
	long long rx;
	long long tx;
	double *rxs;
	double *txs;
	double rxmax;
	double txmax;
	double graphmax;
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
		memmove(*rxs+(cols-colsold), rxstmp, sizeof(double)*colsold);
	else
		memmove(*rxs, rxstmp+(colsold-cols), sizeof(double)*cols);
	free(rxstmp);
}

void printrxs(double *rxs, double graphmax, int lines, int cols, int color) {
	int y, x;
	attron(color);
	for (y = lines-1; y >= 0; y--) {
		for (x = 0; x < cols; x++) {
			if (rxs[x] / graphmax * lines > y)
				addch('*');
			else if (x == 0) {
				attroff(color);
				addch('-');
				attron(color);
			} else
				addch(' ');
		}
	}
	attroff(color);
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

void printstats(struct iface ifa, int lines, int cols, int opts) {
	int colrx, coltx;
	double data;
	const char *unit;
	char *fmt;

	fmt = "%.2f %s/s";
	scaledata(ifa.graphmax, opts, &data, &unit);
	mvprintw(1, 0, fmt, data, unit);
	mvprintw(lines, 0, fmt, 0.0, unit);
	mvprintw(lines+1, 0, fmt, data, unit);
	mvprintw(lines*2, 0, fmt, 0.0, unit);

	fmt = "%6s %.2f %s/s\t"; /* clear overflowing chars with /t */
	colrx = cols / 4 - 8;
	coltx = colrx + cols / 2 + 1;
	scaledata(ifa.rxs[cols-1], opts, &data, &unit);
	mvprintw(lines*2+1, colrx, fmt, "RX:", data, unit);
	scaledata(ifa.txs[cols-1], opts, &data, &unit);
	mvprintw(lines*2+1, coltx, fmt, "TX:", data, unit);

	scaledata(ifa.rxmax, opts, &data, &unit);
	mvprintw(lines*2+2, colrx, fmt, "max:", data, unit);
	scaledata(ifa.txmax, opts, &data, &unit);
	mvprintw(lines*2+2, coltx, fmt, "max:", data, unit);

	scaledata(ifa.rx / 1024, opts, &data, &unit);
	mvprintw(lines*2+3, colrx, fmt, "total:", data, unit);
	scaledata(ifa.tx / 1024, opts, &data, &unit);
	mvprintw(lines*2+3, coltx, fmt, "total:", data, unit);

	refresh();
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

void getdata(struct iface *ifa, double delay, int graphcols, int opts) {
	int i;
	static long long rx, tx;
	double prefix;

	if (rx > 0 && tx > 0 && resize == 0 && !(opts & KEYPRESSED)) {
		getcounters(ifa->ifname, &ifa->rx, &ifa->tx);

		memmove(ifa->rxs, ifa->rxs+1, sizeof(double)*(graphcols-1));
		memmove(ifa->txs, ifa->txs+1, sizeof(double)*(graphcols-1));

		prefix = (opts & SIUNITS) ? 1000.0 : 1024.0;
		ifa->rxs[graphcols-1] = (ifa->rx - rx) / prefix / delay;
		ifa->txs[graphcols-1] = (ifa->tx - tx) / prefix / delay;

		if (ifa->rxs[graphcols-1] > ifa->rxmax)
			ifa->rxmax = ifa->rxs[graphcols-1];
		if (ifa->txs[graphcols-1] > ifa->txmax)
			ifa->txmax = ifa->txs[graphcols-1];

		ifa->graphmax = 0;
		for (i = 0; i < graphcols; i++) {
			if (ifa->rxs[i] > ifa->graphmax)
				ifa->graphmax = ifa->rxs[i];
			if (ifa->txs[i] > ifa->graphmax)
				ifa->graphmax = ifa->txs[i];
		}
	}

	getcounters(ifa->ifname, &rx, &tx);
}

int main(int argc, char *argv[]) {
	int i;
	int opts = 0;
	int linesold, colsold;
	int graphlines;
	double delay = 1.0;
	char key = ERR;
	struct iface ifa;

	memset(&ifa, 0, sizeof ifa);

	for (i = 1; i < argc; i++) {
		if (!strcmp("-s", argv[i]))
			opts |= SIUNITS;
		else if (!strcmp("-n", argv[i]))
			opts |= NOCOLORS;
		else if (argv[i+1] == NULL || argv[i+1][0] == '-')
			eprintf("usage: %s [options]\n"
					"-h             help\n"
					"-s             use SI units\n"
					"-n             no colors\n"
					"-i <interface> network interface\n"
					"-d <seconds>   redraw delay\n"
					"-l <lines>     graph height\n", argv[0]);
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
		graphlines = LINES/2-2;
	getcounters(ifa.ifname, &ifa.rx, &ifa.tx);

	do {
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
			scalerxs(&ifa.rxs, COLS, colsold);
			scalerxs(&ifa.txs, COLS, colsold);
			if (LINES != linesold && !(opts & FIXEDLINES))
				graphlines = LINES/2-2;
			resize = 0;
		}

		mvprintw(0, COLS/2-7, "interface: %s\n", ifa.ifname);
		printrxs(ifa.rxs, ifa.graphmax, graphlines, COLS, COLOR_PAIR(1));
		printrxs(ifa.txs, ifa.graphmax, graphlines, COLS, COLOR_PAIR(2));
		printstats(ifa, graphlines, COLS, opts);

		opts &= ~KEYPRESSED;
	} while (key != 'q');

	endwin();
	return EXIT_SUCCESS;
}
