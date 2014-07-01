#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/limits.h>
#include <ncurses.h>

static sig_atomic_t resize = 0;

struct iface {
	char ifname[IFNAMSIZ];
	int colors;
	int delay;
	int graphlines;
	int fixedheight;
	long rx;
	long tx;
	double prefix;
	double rxmax;
	double txmax;
	double graphmax;
	double *rxs;
	double *txs;
};

void sighandler(int sig) {
	if (sig == SIGWINCH) {
		resize = 1;
		signal(SIGWINCH, sighandler);
	}
}

char *detectiface (char *ifname) {
	char file[PATH_MAX];
	char line[PATH_MAX];
	FILE *fp;
	DIR *dp;
	struct dirent *dir;

	if ((dp = opendir("/sys/class/net")) != NULL) {
		while ((dir = readdir(dp)) != NULL) {
			if (dir->d_name[0] == '.' || !strcmp("lo", dir->d_name))
				continue;

			sprintf(file, "/sys/class/net/%s/operstate", dir->d_name);
			if ((fp = fopen(file, "r")) == NULL)
				continue;

			if (fgets(line, PATH_MAX-1, fp)) {
				strtok(line, "\n");

				if (!strcmp("up", line))
					strncpy(ifname, dir->d_name, IFNAMSIZ-1);
				else if (!strcmp("ppp0", dir->d_name))
					strncpy(ifname, dir->d_name, IFNAMSIZ-1);
			}
			fclose(fp);
		}
	}

	return ifname;
}

struct iface scalegraph(struct iface d) {
	int i, j;
	int COLS2;
	double *rxs;
	double *txs;

	resize = 0;
	COLS2 = COLS;

	endwin();
	refresh();
	clear();

	if (!d.fixedheight)
		d.graphlines = LINES/2-2;

	if (COLS != COLS2) {
		rxs = d.rxs;
		txs = d.txs;

		d.rxs = calloc(COLS, sizeof(double));
		d.txs = calloc(COLS, sizeof(double));

		if (d.rxs == NULL || d.txs == NULL) {
			free(rxs);
			free(txs);
			free(d.rxs);
			free(d.txs);
			endwin();
			fprintf(stderr, "memory allocation failed\n");
			exit(EXIT_FAILURE);
		}

		for (i = COLS-1, j = COLS2-1; i >= 0 && j >= 0; i--, j--) {
			d.rxs[i] = rxs[j];
			d.txs[i] = txs[j];
		}

		free(rxs);
		free(txs);
	}

	return d;
}

void printgraph(struct iface d, const char unit[3][4]) {
	int y, x;
	double i;

	mvprintw(0, COLS/2-7, "interface: %s", d.ifname);
	addch('\n');

	attron(COLOR_PAIR(1));
	for (y = d.graphlines-1; y >= 0; y--) {
		for (x = 0; x < COLS; x++) {
			i = d.rxs[x] / d.graphmax * d.graphlines;
			if (x == 0)
				addch('-');
			else
				i > y ? addch('*') : addch(' ');
		}
	}
	attroff(COLOR_PAIR(1));

	attron(COLOR_PAIR(2));
	for (y = 0; y <= d.graphlines-1; y++) {
		for (x = 0; x < COLS; x++) {
			i = d.txs[x] / d.graphmax * d.graphlines;
			if (x == 0)
				addch('-');
			else
				i > y ? addch('*') : addch(' ');
		}
	}
	attroff(COLOR_PAIR(2));

	if (d.graphmax > d.prefix) {
		mvprintw(1, 0, "%.2lf %s/s", d.graphmax / d.prefix, unit[1]);
		mvprintw(d.graphlines, 0, "0.00 %s/s", unit[1]);
		mvprintw(d.graphlines+1, 0, "0.00 %s/s", unit[1]);
		mvprintw(d.graphlines*2, 0, "%.2lf %s/s", d.graphmax / d.prefix, unit[1]);
	} else {
		mvprintw(1, 0, "%.2lf %s/s", d.graphmax, unit[0]);
		mvprintw(d.graphlines, 0, "0.00 %s/s", unit[0]);
		mvprintw(d.graphlines+1, 0, "0.00 %s/s", unit[0]);
		mvprintw(d.graphlines*2, 0, "%.2lf %s/s", d.graphmax, unit[0]);
	}

	for (y = d.graphlines*2+1; y < d.graphlines*2+4; y++)
		for (x = 0; x < COLS; x++)
			mvprintw(y, x, " ");

	if (d.rxs[COLS-1] > d.prefix || d.txs[COLS-1] > d.prefix) {
		mvprintw(d.graphlines*2+1, (COLS/4)-9, "%7s %.2lf %s/s", "RX:",
				d.rxs[COLS-1] / d.prefix, unit[1]);
		mvprintw(d.graphlines*2+1, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "TX:",
				d.txs[COLS-1] / d.prefix, unit[1]);
	} else {
		mvprintw(d.graphlines*2+1, (COLS/4)-9, "%7s %.2lf %s/s", "RX:",
				d.rxs[COLS-1], unit[0]);
		mvprintw(d.graphlines*2+1, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "TX:",
				d.txs[COLS-1], unit[0]);
	}

	if (d.rxmax > d.prefix || d.txmax > d.prefix) {
		mvprintw(d.graphlines*2+2, (COLS/4)-9, "%7s %.2lf %s/s", "max:",
				d.rxmax / d.prefix, unit[1]);
		mvprintw(d.graphlines*2+2, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "max:",
				d.txmax / d.prefix, unit[1]);
	} else {
		mvprintw(d.graphlines*2+2, (COLS/4)-9, "%7s %.2lf %s/s", "max:",
				d.rxmax, unit[0]);
		mvprintw(d.graphlines*2+2, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "max:",
				d.txmax, unit[0]);
	}

	if (d.rx / d.prefix / d.prefix > d.prefix || d.tx / d.prefix / d.prefix > d.prefix) {
		mvprintw(d.graphlines*2+3, (COLS/4)-9, "%7s %.2lf %s", "total:",
				d.rx / d.prefix / d.prefix / d.prefix, unit[2]);
		mvprintw(d.graphlines*2+3, (COLS/4)-9+(COLS/2), "%7s %.2lf %s", "total:",
				d.tx / d.prefix / d.prefix / d.prefix, unit[2]);
	} else {
		mvprintw(d.graphlines*2+3, (COLS/4)-9, "%7s %.2lf %s", "total:",
				d.rx / d.prefix / d.prefix, unit[1]);
		mvprintw(d.graphlines*2+3, (COLS/4)-9+(COLS/2), "%7s %.2lf %s", "total:",
				d.tx / d.prefix / d.prefix, unit[1]);
	}

	refresh();
}

long fgetl(char *file) {
	char line[PATH_MAX];
	FILE *fp;

	if ((fp = fopen(file, "r")) == NULL)
		return -1;

	if (fgets(line, PATH_MAX-1, fp) == NULL) {
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return strtol(line, NULL, 0);
}

struct iface getdata(struct iface d) {
	char file[PATH_MAX];
	int i;
	long rx, tx;

	sprintf(file, "/sys/class/net/%s/statistics/rx_bytes", d.ifname);
	rx = fgetl(file);
	sprintf(file, "/sys/class/net/%s/statistics/tx_bytes", d.ifname);
	tx = fgetl(file);
	if (rx == -1 || tx == -1) {
		free(d.rxs);
		free(d.txs);
		endwin();
		fprintf(stderr, "cant find network interface: %s\n", d.ifname);
		fprintf(stderr, "you can select interface with: -i <interface>\n");
		exit(EXIT_FAILURE);
	}

	sleep(d.delay);
	if (resize)
		return d;

	sprintf(file, "/sys/class/net/%s/statistics/rx_bytes", d.ifname);
	d.rx = fgetl(file);
	sprintf(file, "/sys/class/net/%s/statistics/tx_bytes", d.ifname);
	d.tx = fgetl(file);
	if (rx == -1 || tx == -1) {
		free(d.rxs);
		free(d.txs);
		endwin();
		fprintf(stderr, "cant find network interface: %s\n", d.ifname);
		fprintf(stderr, "you can select interface with: -i <interface>\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < COLS-1; i++) {
		d.rxs[i] = d.rxs[i+1];
		d.txs[i] = d.txs[i+1];
	}

	d.rxs[COLS-1] = (d.rx - rx) / d.prefix / d.delay;
	d.txs[COLS-1] = (d.tx - tx) / d.prefix / d.delay;

	d.graphmax = 0;
	for (i = 0; i < COLS; i++) {
		if (d.rxs[i] > d.graphmax)
			d.graphmax = d.rxs[i];
		if (d.txs[i] > d.graphmax)
			d.graphmax = d.txs[i];
	}

	if (d.rxs[COLS-1] > d.rxmax)
		d.rxmax = d.rxs[COLS-1];
	if (d.txs[COLS-1] > d.txmax)
		d.txmax = d.txs[COLS-1];

	return d;
}

int main(int argc, char *argv[]) {
	int i;
	char key;

	struct iface d = {
		.ifname = "",
		.prefix = 1024.0,
		.colors = 1,
		.delay = 1,
		.graphlines = 0,
		.fixedheight = 0,
		.rx = 0,
		.tx = 0,
		.rxmax = 0,
		.txmax = 0,
		.graphmax = 0
	};

	const char units[2][3][4] = {
		{ "KiB", "MiB", "GiB" },
		{ "kB", "MB", "GB" }
	};

	for (i = 1; i < argc; i++) {
		if (!strcmp("-s", argv[i])) {
			d.prefix = 1000.0;
		} else if (!strcmp("-n", argv[i])) {
			d.colors = 0;
		} else if (!strcmp("-i", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "-i needs parameter\n");
				exit(EXIT_FAILURE);
			} else if (strlen(argv[i+1]) > IFNAMSIZ-1) {
				fprintf(stderr, "maximum interface length: %d\n", IFNAMSIZ-1);
				exit(EXIT_FAILURE);
			}
			strncpy(d.ifname, argv[++i], IFNAMSIZ-1);
		} else if (!strcmp("-d", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "-d needs parameter\n");
				exit(EXIT_FAILURE);
			}
			d.delay = strtol(argv[++i], NULL, 0);
			if (d.delay < 1) {
				fprintf(stderr, "minimum d.delay: 1\n");
				exit(EXIT_FAILURE);
			}
		} else if (!strcmp("-l", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "-l needs parameter\n");
				exit(EXIT_FAILURE);
			}
			d.graphlines = strtol(argv[++i], NULL, 0);
			d.fixedheight = 1;
			if (d.graphlines < 3) {
				fprintf(stderr, "minimum graph height: 3");
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

	if (!strcmp("", d.ifname))
		strncpy(d.ifname, detectiface(d.ifname), IFNAMSIZ-1);

	if (!strcmp("", d.ifname)) {
		free(d.rxs);
		free(d.txs);
		endwin();
		fprintf(stderr, "couldnt find active interface\n");
		return EXIT_FAILURE;
	}

	initscr();
	curs_set(0);
	noecho();
	nodelay(stdscr, TRUE);
	if (d.colors && has_colors()) {
		start_color();
		use_default_colors();
		init_pair(1, COLOR_GREEN, -1);
		init_pair(2, COLOR_RED, -1);
	}

	if (!d.fixedheight)
		d.graphlines = LINES/2-2;

	d.rxs = calloc(COLS, sizeof(double));
	d.txs = calloc(COLS, sizeof(double));

	if (d.rxs == NULL || d.txs == NULL) {
		free(d.rxs);
		free(d.txs);
		endwin();
		fprintf(stderr, "memory allocation failed\n");
		return EXIT_FAILURE;
	}

	signal(SIGWINCH, sighandler);

	while (1) {
		key = getch();
		if (key != ERR && key == 'q')
			break;

		if (resize)
			d = scalegraph(d);

		printgraph(d, d.prefix > 1000.1 ? units[0] : units[1]);

		d = getdata(d);
	}

	free(d.rxs);
	free(d.txs);
	endwin();
	return EXIT_SUCCESS;
}
