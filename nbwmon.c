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
#include <unistd.h>
#include <ncurses.h>
#include <ifaddrs.h>
#include <net/if.h>

struct iface {
	char *ifname;
	long long rx;
	long long tx;
	double rxmax;
	double txmax;
	double graphmax;
	double *rxs;
	double *txs;
};

sig_atomic_t resize = 0;

void usage(char *argv0) {
	fprintf(stderr, "usage: %s [options]\n", argv0);
	fprintf(stderr, "-h             help\n");
	fprintf(stderr, "-s             use SI units\n");
	fprintf(stderr, "-n             no colors\n");
	fprintf(stderr, "-i <interface> network interface\n");
	fprintf(stderr, "-d <seconds>   delay\n");
	fprintf(stderr, "-l <lines>     graph height\n");
	exit(EXIT_FAILURE);
}

void eprintf(const char *fmt, ...) {
	va_list ap;
	endwin();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void *ecalloc(size_t nmemb, size_t size) {
	void *p;
	p = calloc(nmemb, size);
	if (!p)
		eprintf("out of memory\n");
	return p;
}

void sighandler(int sig) {
	if (sig == SIGWINCH) {
		resize = 1;
		signal(SIGWINCH, sighandler);
	}
}

char *detectiface(void) {
	static char ifname[IFNAMSIZ];
	struct ifaddrs *ifas, *ifa;

	if (getifaddrs(&ifas) == -1)
		return ifname;
	for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;
		else if (!(ifa->ifa_flags & IFF_RUNNING))
			continue;
		else if (!(ifa->ifa_flags & IFF_UP))
			continue;
		strncpy(ifname, ifa->ifa_name, IFNAMSIZ - 1);
		ifname[IFNAMSIZ-1] = '\0';
		break;
	}
	freeifaddrs(ifas);

	return ifname;
}

void scalegraph(struct iface *ifa, int *graphlines, int fixedlines) {
	int i, j;
	int colsold;
	double *rxs;
	double *txs;

	resize = 0;
	colsold = COLS;

	endwin();
	refresh();
	clear();

	if (fixedlines == 0)
		*graphlines = LINES/2-2;

	if (COLS != colsold) {
		rxs = ifa->rxs;
		txs = ifa->txs;

		ifa->rxs = ecalloc(COLS, sizeof(double));
		ifa->txs = ecalloc(COLS, sizeof(double));

		for (i = COLS-1, j = colsold-1; i >= 0 && j >= 0; i--, j--) {
			ifa->rxs[i] = rxs[j];
			ifa->txs[i] = txs[j];
		}

		free(rxs);
		free(txs);
	}
}

void printgraph(struct iface ifa, double prefix, int graphlines) {
	static int y, x;
	static double rx;
	static double tx;
	static char *u;
	static char unit[3][4];

	if (prefix == 1024.0) {
		strncpy(unit[0], "KiB", 4);
		strncpy(unit[1], "MiB", 4);
		strncpy(unit[2], "GiB", 4);
	} else {
		strncpy(unit[0], "kB", 4);
		strncpy(unit[1], "MB", 4);
		strncpy(unit[2], "GB", 4);
	}

	mvprintw(0, COLS/2-7, "interface: %s\n", ifa.ifname);

	attron(COLOR_PAIR(1));
	for (y = graphlines-1; y >= 0; y--) {
		for (x = 0; x < COLS; x++) {
			if (ifa.rxs[x] / ifa.graphmax * graphlines > y)
				addch('*');
			else
				if (x != 0) {
					addch(' ');
				} else {
					attroff(COLOR_PAIR(1));
					addch('-');
					attron(COLOR_PAIR(1));
				}
		}
	}
	attroff(COLOR_PAIR(1));

	attron(COLOR_PAIR(2));
	for (y = 0; y <= graphlines-1; y++) {
		for (x = 0; x < COLS; x++) {
			if (ifa.txs[x] / ifa.graphmax * graphlines > y)
				addch('*');
			else
				if (x != 0) {
					addch(' ');
				} else {
					attroff(COLOR_PAIR(2));
					addch('-');
					attron(COLOR_PAIR(2));
				}
		}
	}
	attroff(COLOR_PAIR(2));

	u = unit[0];
	rx = ifa.graphmax;
	if (rx > prefix) {
		u = unit[1];
		rx /= prefix;
	}
	mvprintw(1, 0, "%.2lf %s/s", rx, u);
	mvprintw(graphlines, 0, "0.00 %s/s", u);
	mvprintw(graphlines+1, 0, "0.00 %s/s", u);
	mvprintw(graphlines*2, 0, "%.2lf %s/s", rx, u);

	for (y = graphlines*2+1; y < graphlines*2+4; y++)
		for (x = 0; x < COLS; x++)
			mvprintw(y, x, " ");

	u = unit[0];
	rx = ifa.rxs[COLS-1];
	tx = ifa.txs[COLS-1];
	if (rx > prefix || tx > prefix) {
		u = unit[1];
		rx /= prefix;
		tx /= prefix;
	}
	mvprintw(graphlines*2+1, (COLS/4)-9, "%7s %.2lf %s/s", "RX:", rx, u);
	mvprintw(graphlines*2+1, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "TX:", tx, u);

	u = unit[0];
	rx = ifa.rxmax;
	tx = ifa.txmax;
	if (rx > prefix || tx > prefix) {
		u = unit[1];
		rx /= prefix;
		tx /= prefix;
	}
	mvprintw(graphlines*2+2, (COLS/4)-9, "%7s %.2lf %s/s", "max:", rx, u);
	mvprintw(graphlines*2+2, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "max:", tx, u);

	u = unit[1];
	rx = ifa.rx / prefix / prefix;
	tx = ifa.tx / prefix / prefix;
	if (rx > prefix || tx > prefix) {
		u = unit[2];
		rx /= prefix;
		tx /= prefix;
	}
	mvprintw(graphlines*2+3, (COLS/4)-9, "%7s %.2lf %s", "total:", rx, u);
	mvprintw(graphlines*2+3, (COLS/4)-9+(COLS/2), "%7s %.2lf %s", "total:", tx, u);

	refresh();
}

#ifdef __linux__
int getcounters(char *ifname, long long *rx, long long *tx) {
	struct ifaddrs *ifas, *ifa;
	struct rtnl_link_stats *stats;
	int family;

	*rx = -1;
	*tx = -1;

	if (getifaddrs(&ifas) == -1)
		return 1;
	for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
		if (!strcmp(ifa->ifa_name, ifname)) {
			family = ifa->ifa_addr->sa_family;
			if (family == AF_PACKET && ifa->ifa_data != NULL) {
				stats = ifa->ifa_data;
				*rx = stats->rx_bytes;
				*tx = stats->tx_bytes;
			}
		}
	}
	freeifaddrs(ifas);

	if (*rx == -1 || *tx == -1)
		return 1;
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

	if (sysctl(mib, 6, NULL, &sz, NULL, 0) < 0) {
		free(buf);
		return 1;
	}

	buf = ecalloc(1, sz);

	if (sysctl(mib, 6, buf, &sz, NULL, 0) < 0) {
		free(buf);
		return 1;
	}

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
		return 1;
	return 0;
}
#endif

int getdata(struct iface *ifa, int delay, double prefix) {
	static int i;
	static long long rx, tx;

	if (getcounters(ifa->ifname, &rx, &tx) != 0)
		return 1;
	sleep(delay);
	if (resize)
		return 0;
	if (getcounters(ifa->ifname, &ifa->rx, &ifa->tx) != 0)
		return 1;

	memmove(ifa->rxs, ifa->rxs+1, sizeof ifa->rxs * (COLS-1));
	memmove(ifa->txs, ifa->txs+1, sizeof ifa->txs * (COLS-1));

	ifa->rxs[COLS-1] = (ifa->rx - rx) / prefix / delay;
	ifa->txs[COLS-1] = (ifa->tx - tx) / prefix / delay;

	ifa->graphmax = 0;
	for (i = 0; i < COLS; i++) {
		if (ifa->rxs[i] > ifa->graphmax)
			ifa->graphmax = ifa->rxs[i];
		if (ifa->txs[i] > ifa->graphmax)
			ifa->graphmax = ifa->txs[i];
	}

	if (ifa->rxs[COLS-1] > ifa->rxmax)
		ifa->rxmax = ifa->rxs[COLS-1];
	if (ifa->txs[COLS-1] > ifa->txmax)
		ifa->txmax = ifa->txs[COLS-1];

	return 0;
}

int main(int argc, char *argv[]) {
	unsigned int i;
	int colors = 1;
	int delay = 1;
	int graphlines = 0;
	int fixedlines = 0;
	double prefix = 1024.0;
	char key;
	struct iface ifa;

	memset(&ifa, 0, sizeof ifa);
	ifa.ifname = detectiface();

	for (i = 1; i < argc; i++) {
		if (!strcmp("-s", argv[i]))
			prefix = 1000.0;
		else if (!strcmp("-n", argv[i]))
			colors = 0;
		else if (argv[i+1] == NULL || argv[i+1][0] == '-')
			usage(argv[0]);
		else if (!strcmp("-i", argv[i])) {
			if (strlen(argv[i+1]) > IFNAMSIZ-1)
				eprintf("maximum interface length: %d\n", IFNAMSIZ-1);
			strncpy(ifa.ifname, argv[++i], IFNAMSIZ-1);
			ifa.ifname[IFNAMSIZ-1] = '\0';
		} else if (!strcmp("-d", argv[i])) {
			delay = strtol(argv[++i], NULL, 10);
			if (delay < 1)
				eprintf("minimum delay: 1\n");
		} else if (!strcmp("-l", argv[i])) {
			graphlines = strtol(argv[++i], NULL, 10);
			if (graphlines < 3)
				eprintf("minimum graph height: 3\n");
			fixedlines = 1;
		}
	}

	if (ifa.ifname[0] == '\0')
		eprintf("can't detect network interface\n");

	initscr();
	curs_set(0);
	noecho();
	nodelay(stdscr, TRUE);
	if (colors && has_colors()) {
		start_color();
		use_default_colors();
		init_pair(1, COLOR_GREEN, -1);
		init_pair(2, COLOR_RED, -1);
	}

	if (fixedlines == 0)
		graphlines = LINES/2-2;

	ifa.rxs = ecalloc(COLS, sizeof(double));
	ifa.txs = ecalloc(COLS, sizeof(double));

	signal(SIGWINCH, sighandler);

	while (1) {
		key = getch();
		if (key == 'q')
			break;
		else if (key == 'r' || resize)
			scalegraph(&ifa, &graphlines, fixedlines);
		printgraph(ifa, prefix, graphlines);
		if (getdata(&ifa, delay, prefix) != 0)
			eprintf("can't read rx and tx bytes\n");
	}

	endwin();
	return EXIT_SUCCESS;
}
