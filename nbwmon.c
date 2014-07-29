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

void scalegraph(struct iface *ifa, unsigned int *graphlines, int opts) {
	int linestmp = LINES;
	int colstmp = COLS;
	double *rxs;
	double *txs;

	endwin();
	refresh();
	clear();

	if (LINES != linestmp && !(opts & FIXEDLINES))
		*graphlines = LINES/2-2;

	if (COLS != colstmp) {
		rxs = ifa->rxs;
		txs = ifa->txs;
		ifa->rxs = ecalloc(COLS, sizeof(double));
		ifa->txs = ecalloc(COLS, sizeof(double));

		if (COLS > colstmp) {
			memmove(ifa->rxs+(COLS-colstmp), rxs, sizeof(double)*colstmp);
			memmove(ifa->txs+(COLS-colstmp), txs, sizeof(double)*colstmp);
		} else {
			memmove(ifa->rxs, rxs+(colstmp-COLS), sizeof(double)*COLS);
			memmove(ifa->txs, txs+(colstmp-COLS), sizeof(double)*COLS);
		}
		free(rxs);
		free(txs);
	}
	resize = 0;
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

void printgraph(struct iface ifa, unsigned int graphlines, int opts) {
	int y, x;
	int colrx, coltx;
	double data;
	const char *unit;
	char *fmt;

	mvprintw(0, COLS/2-7, "interface: %s\n", ifa.ifname);

	attron(COLOR_PAIR(1));
	for (y = graphlines-1; y >= 0; y--) {
		for (x = 0; x < COLS; x++) {
			if (ifa.rxs[x] / ifa.graphmax * graphlines > y)
				addch('*');
			else if (x == 0) {
				attroff(COLOR_PAIR(1));
				addch('-');
				attron(COLOR_PAIR(1));
			} else
				addch(' ');
		}
	}
	attroff(COLOR_PAIR(1));
	attron(COLOR_PAIR(2));
	for (y = 0; y < graphlines; y++) {
		for (x = 0; x < COLS; x++) {
			if (ifa.txs[x] / ifa.graphmax * graphlines > y)
				addch('*');
			else if (x == 0) {
				attroff(COLOR_PAIR(2));
				addch('-');
				attron(COLOR_PAIR(2));
			} else
				addch(' ');
		}
	}
	attroff(COLOR_PAIR(2));

	fmt = "%.2f %s/s";
	scaledata(ifa.graphmax, opts, &data, &unit);
	mvprintw(1, 0, fmt, data, unit);
	mvprintw(graphlines, 0, fmt, 0.0, unit);
	mvprintw(graphlines+1, 0, fmt, 0.0, unit);
	mvprintw(graphlines*2, 0, fmt, data, unit);

	fmt = "%6s %.2f %s/s\t"; /* clear overflowing chars with /t */
	colrx = COLS / 4 - 8;
	coltx = colrx + COLS / 2 + 1;
	scaledata(ifa.rxs[COLS-1], opts, &data, &unit);
	mvprintw(graphlines*2+1, colrx, fmt, "RX:", data, unit);
	scaledata(ifa.txs[COLS-1], opts, &data, &unit);
	mvprintw(graphlines*2+1, coltx, fmt, "TX:", data, unit);

	scaledata(ifa.rxmax, opts, &data, &unit);
	mvprintw(graphlines*2+2, colrx, fmt, "max:", data, unit);
	scaledata(ifa.txmax, opts, &data, &unit);
	mvprintw(graphlines*2+2, coltx, fmt, "max:", data, unit);

	scaledata(ifa.rx / 1024, opts, &data, &unit);
	mvprintw(graphlines*2+3, colrx, fmt, "total:", data, unit);
	scaledata(ifa.tx / 1024, opts, &data, &unit);
	mvprintw(graphlines*2+3, coltx, fmt, "total:", data, unit);

	refresh();
}

#ifdef __linux__
int getcounters(char *ifname, long long *rx, long long *tx) {
	struct ifaddrs *ifas, *ifa;
	struct rtnl_link_stats *stats;

	*rx = -1;
	*tx = -1;

	if (getifaddrs(&ifas) == -1)
		return 1;
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
	return 0;
}

#elif __OpenBSD__
int getcounters(char *ifname, long long *rx, long long *tx) {
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
	return 0;
}
#endif

int getdata(struct iface *ifa, double delay, int opts) {
	int i;
	static long long rx, tx;
	double prefix;

	if (rx > 0 && tx > 0 && !resize && !(opts & KEYPRESSED)) {
		getcounters(ifa->ifname, &ifa->rx, &ifa->tx);

		memmove(ifa->rxs, ifa->rxs+1, sizeof(double)*(COLS-1));
		memmove(ifa->txs, ifa->txs+1, sizeof(double)*(COLS-1));

		prefix = (opts & SIUNITS) ? 1000.0 : 1024.0;
		ifa->rxs[COLS-1] = (ifa->rx - rx) / prefix / delay;
		ifa->txs[COLS-1] = (ifa->tx - tx) / prefix / delay;

		if (ifa->rxs[COLS-1] > ifa->rxmax)
			ifa->rxmax = ifa->rxs[COLS-1];
		if (ifa->txs[COLS-1] > ifa->txmax)
			ifa->txmax = ifa->txs[COLS-1];

		ifa->graphmax = 0;
		for (i = 0; i < COLS; i++) {
			if (ifa->rxs[i] > ifa->graphmax)
				ifa->graphmax = ifa->rxs[i];
			if (ifa->txs[i] > ifa->graphmax)
				ifa->graphmax = ifa->txs[i];
		}
	}
	getcounters(ifa->ifname, &rx, &tx);
	return 0;
}

int main(int argc, char *argv[]) {
	int i;
	int opts = 0;
	unsigned int graphlines;
	double delay = 1.0;
	char key;
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

	getcounters(ifa.ifname, &ifa.rx, &ifa.tx);
	if (!(opts & FIXEDLINES))
		graphlines = LINES/2-2;
	printgraph(ifa, graphlines, opts);

	while ((key = getch()) != 'q') {
		if (key != ERR)
			opts |= KEYPRESSED;
		getdata(&ifa, delay, opts);
		if (resize || key == 'r')
			scalegraph(&ifa, &graphlines, opts);
		printgraph(ifa, graphlines, opts);
		opts &= ~KEYPRESSED;
	}

	endwin();
	return EXIT_SUCCESS;
}
