#define _GNU_SOURCE

#ifdef __linux__
#include <linux/if_link.h>
#elif __OpenBSD__
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if_dl.h>
#include <net/route.h>
#else
#warning "your platform is not supported"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ncurses.h>
#include <ifaddrs.h>
#include <net/if.h>

static sig_atomic_t resize = 0;

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

void arg(int argc, char *argv[], char *ifname, double *prefix, int *colors, int *delay, int *graphlines) {
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-i", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -i needs parameter\n");
				exit(EXIT_FAILURE);
			} else if (strlen(argv[i+1]) > IFNAMSIZ-1) {
				fprintf(stderr, "error: maximum interface length: %d\n", IFNAMSIZ-1);
				exit(EXIT_FAILURE);
			}
			strncpy(ifname, argv[++i], IFNAMSIZ-1);
			ifname[IFNAMSIZ-1] = '\0';
		} else if (!strcmp("-s", argv[i])) {
			*prefix = 1000.0;
		} else if (!strcmp("-n", argv[i])) {
			*colors = 0;
		} else if (!strcmp("-d", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -d needs parameter\n");
				exit(EXIT_FAILURE);
			}
			*delay = strtol(argv[++i], NULL, 0);
			if (*delay < 1) {
				fprintf(stderr, "error: minimum delay: 1\n");
				exit(EXIT_FAILURE);
			}
		} else if (!strcmp("-l", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -l needs parameter\n");
				exit(EXIT_FAILURE);
			}
			*graphlines = strtol(argv[++i], NULL, 0);
			if (*graphlines < 3) {
				fprintf(stderr, "error: minimum graph height: 3\n");
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "usage: %s [options]\n", argv[0]);
			fprintf(stderr, "-h             help\n");
			fprintf(stderr, "-s             use SI units\n");
			fprintf(stderr, "-n             no colors\n");
			fprintf(stderr, "-i <interface> network interface\n");
			fprintf(stderr, "-d <seconds>   delay\n");
			fprintf(stderr, "-l <lines>     graph height\n");
			exit(EXIT_FAILURE);
		}
	}
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
		return NULL;

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

void scalegraph(struct iface *d, int *graphlines, int fixedlines) {
	int i, j;
	int COLS2;
	double *rxs;
	double *txs;

	resize = 0;
	COLS2 = COLS;

	endwin();
	refresh();
	clear();

	if (fixedlines == 0)
		*graphlines = LINES/2-2;

	if (COLS != COLS2) {
		rxs = d->rxs;
		txs = d->txs;

		d->rxs = calloc(COLS, sizeof(double));
		d->txs = calloc(COLS, sizeof(double));

		if (d->rxs == NULL || d->txs == NULL) {
			free(rxs);
			free(txs);
			free(d->rxs);
			free(d->txs);
			endwin();
			fprintf(stderr, "error: out of memory\n");
			exit(EXIT_FAILURE);
		}

		for (i = COLS-1, j = COLS2-1; i >= 0 && j >= 0; i--, j--) {
			d->rxs[i] = rxs[j];
			d->txs[i] = txs[j];
		}

		free(rxs);
		free(txs);
	}
}

void printgraph(struct iface d, double prefix, int graphlines) {
	int y, x;
	double i;

	static char unit[3][4];

	if (prefix > 1000.1) {
		strncpy(unit[0], "KiB", 4);
		strncpy(unit[1], "MiB", 4);
		strncpy(unit[2], "GiB", 4);
	} else {
		strncpy(unit[0], "kB", 4);
		strncpy(unit[1], "MB", 4);
		strncpy(unit[2], "GB", 4);
	}

	mvprintw(0, COLS/2-7, "interface: %s", d.ifname);
	addch('\n');

	attron(COLOR_PAIR(1));
	for (y = graphlines-1; y >= 0; y--) {
		for (x = 0; x < COLS; x++) {
			i = d.rxs[x] / d.graphmax * graphlines;
			if (i > y)
				addch('*');
			else
				if (x == 0) {
					attroff(COLOR_PAIR(1));
					addch('-');
					attron(COLOR_PAIR(1));
				} else
					addch(' ');
		}
	}
	attroff(COLOR_PAIR(1));

	attron(COLOR_PAIR(2));
	for (y = 0; y <= graphlines-1; y++) {
		for (x = 0; x < COLS; x++) {
			i = d.txs[x] / d.graphmax * graphlines;
			if (i > y)
				addch('*');
			else
				if (x == 0) {
					attroff(COLOR_PAIR(2));
					addch('-');
					attron(COLOR_PAIR(2));
				} else
					addch(' ');
		}
	}
	attroff(COLOR_PAIR(2));

	if (d.graphmax > prefix) {
		mvprintw(1, 0, "%.2lf %s/s", d.graphmax / prefix, unit[1]);
		mvprintw(graphlines, 0, "0.00 %s/s", unit[1]);
		mvprintw(graphlines+1, 0, "0.00 %s/s", unit[1]);
		mvprintw(graphlines*2, 0, "%.2lf %s/s", d.graphmax / prefix, unit[1]);
	} else {
		mvprintw(1, 0, "%.2lf %s/s", d.graphmax, unit[0]);
		mvprintw(graphlines, 0, "0.00 %s/s", unit[0]);
		mvprintw(graphlines+1, 0, "0.00 %s/s", unit[0]);
		mvprintw(graphlines*2, 0, "%.2lf %s/s", d.graphmax, unit[0]);
	}

	for (y = graphlines*2+1; y < graphlines*2+4; y++)
		for (x = 0; x < COLS; x++)
			mvprintw(y, x, " ");

	if (d.rxs[COLS-1] > prefix || d.txs[COLS-1] > prefix) {
		mvprintw(graphlines*2+1, (COLS/4)-9, "%7s %.2lf %s/s", "RX:",
				d.rxs[COLS-1] / prefix, unit[1]);
		mvprintw(graphlines*2+1, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "TX:",
				d.txs[COLS-1] / prefix, unit[1]);
	} else {
		mvprintw(graphlines*2+1, (COLS/4)-9, "%7s %.2lf %s/s", "RX:",
				d.rxs[COLS-1], unit[0]);
		mvprintw(graphlines*2+1, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "TX:",
				d.txs[COLS-1], unit[0]);
	}

	if (d.rxmax > prefix || d.txmax > prefix) {
		mvprintw(graphlines*2+2, (COLS/4)-9, "%7s %.2lf %s/s", "max:",
				d.rxmax / prefix, unit[1]);
		mvprintw(graphlines*2+2, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "max:",
				d.txmax / prefix, unit[1]);
	} else {
		mvprintw(graphlines*2+2, (COLS/4)-9, "%7s %.2lf %s/s", "max:",
				d.rxmax, unit[0]);
		mvprintw(graphlines*2+2, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "max:",
				d.txmax, unit[0]);
	}

	if (d.rx / prefix / prefix > prefix || d.tx / prefix / prefix > prefix) {
		mvprintw(graphlines*2+3, (COLS/4)-9, "%7s %.2lf %s", "total:",
				d.rx / prefix / prefix / prefix, unit[2]);
		mvprintw(graphlines*2+3, (COLS/4)-9+(COLS/2), "%7s %.2lf %s", "total:",
				d.tx / prefix / prefix / prefix, unit[2]);
	} else {
		mvprintw(graphlines*2+3, (COLS/4)-9, "%7s %.2lf %s", "total:",
				d.rx / prefix / prefix, unit[1]);
		mvprintw(graphlines*2+3, (COLS/4)-9+(COLS/2), "%7s %.2lf %s", "total:",
				d.tx / prefix / prefix, unit[1]);
	}

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
		return -1;

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
		return -1;
	}

	buf = malloc(sz);
	if (!buf) {
		fprintf(stderr, "error: out of memory\n");
		exit(EXIT_FAILURE);
	}

	if (sysctl(mib, 6, buf, &sz, NULL, 0) < 0) {
		free(buf);
		return -1;
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
	return 0;
}
#endif

int getdata(struct iface *d, int delay, double prefix) {
	int i;
	long long rx, tx;

	getcounters(d->ifname, &rx, &tx);
	if (rx == -1 || tx == -1)
		return 1;

	sleep(delay);
	if (resize)
		return 0;

	getcounters(d->ifname, &d->rx, &d->tx);
	if (d->rx == -1 || d->tx == -1)
		return 1;

	for (i = 0; i < COLS-1; i++) {
		d->rxs[i] = d->rxs[i+1];
		d->txs[i] = d->txs[i+1];
	}

	d->rxs[COLS-1] = (d->rx - rx) / prefix / delay;
	d->txs[COLS-1] = (d->tx - tx) / prefix / delay;

	d->graphmax = 0;
	for (i = 0; i < COLS; i++) {
		if (d->rxs[i] > d->graphmax)
			d->graphmax = d->rxs[i];
		if (d->txs[i] > d->graphmax)
			d->graphmax = d->txs[i];
	}

	if (d->rxs[COLS-1] > d->rxmax)
		d->rxmax = d->rxs[COLS-1];
	if (d->txs[COLS-1] > d->txmax)
		d->txmax = d->txs[COLS-1];

	return 0;
}

int main(int argc, char *argv[]) {
	int colors = 1;
	int delay = 1;
	int fixedlines = 0;
	int graphlines = 0;
	double prefix = 1024.0;
	char key;

	struct iface d = {
		.rx = 0,
		.tx = 0,
		.rxmax = 0,
		.txmax = 0,
		.graphmax = 0
	};

	d.ifname = detectiface();

	arg(argc, argv, d.ifname, &prefix, &colors, &delay, &graphlines);

	if (graphlines != 0)
		fixedlines = 1;

	if (!strcmp(d.ifname, "")) {
		fprintf(stderr, "error: can't find network interface\n");
		return EXIT_FAILURE;
	}

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

	d.rxs = calloc(COLS, sizeof(double));
	d.txs = calloc(COLS, sizeof(double));

	if (d.rxs == NULL || d.txs == NULL) {
		free(d.rxs);
		free(d.txs);
		endwin();
		fprintf(stderr, "error: out of memory\n");
		return EXIT_FAILURE;
	}

	signal(SIGWINCH, sighandler);

	while (1) {
		key = getch();
		if (key == 'q')
			break;
		else if (key == 'r' || resize)
			scalegraph(&d, &graphlines, fixedlines);

		printgraph(d, prefix, graphlines);

		if (getdata(&d, delay, prefix) != 0) {
			free(d.rxs);
			free(d.txs);
			endwin();
			fprintf(stderr, "error: can't read rx and tx bytes\n");
			return EXIT_SUCCESS;
		}
	}

	free(d.rxs);
	free(d.txs);
	endwin();
	return EXIT_SUCCESS;
}
