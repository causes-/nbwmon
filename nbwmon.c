#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <ncurses.h>

#define IFNAMSIZ 16
#define PATH_MAX 4096

char iface[IFNAMSIZ];
int unit = 0;
int delay = 1;
int graphlines = 10;
int colors = 1;
int resize = 1;

struct iface {
	long rx;
	long tx;
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

void arg(int argc, char *argv[]) {
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-s", argv[i])) {
			unit = 1;
		} else if (!strcmp("-n", argv[i])) {
			colors = 0;
		} else if (!strcmp("-i", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -i needs parameter\n");
				exit(EXIT_FAILURE);
			}
			if (strlen(argv[i+1]) > IFNAMSIZ-1) {
				fprintf(stderr, "error: maximum interface length: %d\n", IFNAMSIZ-1);
				exit(EXIT_FAILURE);
			}
			strncpy(iface, argv[++i], IFNAMSIZ-1);
		} else if (!strcmp("-d", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -d needs parameter\n");
				exit(EXIT_FAILURE);
			}
			delay = strtol(argv[++i], NULL, 0);
			if (delay < 1) {
				fprintf(stderr, "error: minimum delay: 1\n");
				exit(EXIT_FAILURE);
			}
		} else if (!strcmp("-l", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -l needs parameter\n");
				exit(EXIT_FAILURE);
			}
			graphlines = strtol(argv[++i], NULL, 0);
			if (graphlines < 3) {
				fprintf(stderr, "error: minimum graphlines: 3");
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "usage: %s [options]\n", argv[0]);
			fprintf(stderr, "-h           help\n");
			fprintf(stderr, "-s           use SI units (kB/s)\n");
			fprintf(stderr, "-n           no colors\n");
			fprintf(stderr, "-i <iface>   interface\n");
			fprintf(stderr, "-d <seconds> delay\n");
			fprintf(stderr, "-l <lines>   graph height\n");
			exit(EXIT_FAILURE);
		}
	}
}

void ifaceup (void) {
	char file[PATH_MAX];
	char line[PATH_MAX];
	FILE *fp;
	DIR *d;
	struct dirent *dir;

	if ((d = opendir("/sys/class/net")) != NULL) {
		while ((dir = readdir(d)) != NULL) {
			if (dir->d_name[0] == '.' || !strcmp("lo", dir->d_name))
				continue;

			sprintf(file, "/sys/class/net/%s/operstate", dir->d_name);
			if ((fp = fopen(file, "r")) == NULL)
				continue;

			if (fgets(line, PATH_MAX-1, fp)) {
				strtok(line, "\n");

				// ppp0 shows UNKNOWN but only appears when connected
				if (!strcmp("ppp0", dir->d_name))
					strncpy(iface, dir->d_name, IFNAMSIZ-1);

				if (!strcmp("up", line))
					strncpy(iface, dir->d_name, IFNAMSIZ-1);
			}
			fclose(fp);
		}
	}
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

	if (COLS == COLS2)
		return d;

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

	return d;
}

void printgraph(struct iface d, double prefix, char unit[3][4]) {
	int x, y;
	double i;

	mvprintw(0, COLS/2-7, "interface: %s", iface);
	addch('\n');

	attron(COLOR_PAIR(1));
	for (y = graphlines-1; y >= 0; y--) {
		for (x = 0; x < COLS; x++) {
			i = d.rxs[x] / d.graphmax * graphlines;
			if (x == 0)
				addch('-');
			else
				i > y ? addch('*') : addch(' ');
		}
	}
	attroff(COLOR_PAIR(1));

	attron(COLOR_PAIR(2));
	for (y = 0; y <= graphlines-1; y++) {
		for (x = 0; x < COLS; x++) {
			i = d.txs[x] / d.graphmax * graphlines;
			if (x == 0)
				addch('-');
			else
				i > y ? addch('*') : addch(' ');
		}
	}
	attroff(COLOR_PAIR(2));

	if (d.graphmax > prefix) {
		mvprintw(1, 0, "%.2lf %s/s", d.graphmax / prefix, unit[1]);
		mvprintw(graphlines, 0, "0.00 %s/s", unit[0]);
		mvprintw(graphlines+1, 0, "0.00 %s/s", unit[0]);
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
		mvprintw(graphlines*2+1, (COLS/4)-9, "%7s %.2lf %s/s", "RX:", d.rxs[COLS-1] / prefix, unit[1]);
		mvprintw(graphlines*2+1, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "TX:", d.txs[COLS-1] / prefix, unit[1]);
	} else {
		mvprintw(graphlines*2+1, (COLS/4)-9, "%7s %.2lf %s/s", "RX:", d.rxs[COLS-1], unit[0]);
		mvprintw(graphlines*2+1, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "TX:", d.txs[COLS-1], unit[0]);
	}

	if (d.rxmax > prefix || d.txmax > prefix) {
		mvprintw(graphlines*2+2, (COLS/4)-9, "%7s %.2lf %s/s", "max:", d.rxmax / prefix, unit[1]);
		mvprintw(graphlines*2+2, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "max:", d.txmax / prefix, unit[1]);
	} else {
		mvprintw(graphlines*2+2, (COLS/4)-9, "%7s %.2lf %s/s", "max:", d.rxmax, unit[0]);
		mvprintw(graphlines*2+2, (COLS/4)-9+(COLS/2), "%7s %.2lf %s/s", "max:", d.txmax, unit[0]);
	}

	if (d.rx / prefix / prefix > prefix || d.tx / prefix / prefix > prefix) {
		mvprintw(graphlines*2+3, (COLS/4)-9, "%7s %.2lf %s", "total:", d.rx / prefix / prefix / prefix, unit[2]);
		mvprintw(graphlines*2+3, (COLS/4)-9+(COLS/2), "%7s %.2lf %s", "total:",
				d.tx / prefix / prefix / prefix, unit[2]);
	} else {
		mvprintw(graphlines*2+3, (COLS/4)-9, "%7s %.2lf %s", "total:", d.rx / prefix / prefix, unit[1]);
		mvprintw(graphlines*2+3, (COLS/4)-9+(COLS/2), "%7s %.2lf %s", "total:", d.tx / prefix / prefix, unit[1]);
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

struct iface getdata(struct iface d, double prefix) {
	char file[PATH_MAX];
	int i;
	long rx, tx;

	sprintf(file, "/sys/class/net/%s/statistics/rx_bytes", iface);
	rx = fgetl(file);
	sprintf(file, "/sys/class/net/%s/statistics/tx_bytes", iface);
	tx = fgetl(file);
	if (rx == -1 || tx == -1) {
		free(d.rxs);
		free(d.txs);
		endwin();
		fprintf(stderr, "cant find network interface %s\n", iface);
		fprintf(stderr, "you can select interface with: -i <interface>\n");
		exit(EXIT_FAILURE);
	}

	sleep(delay);
	if (resize)
		return d;

	sprintf(file, "/sys/class/net/%s/statistics/rx_bytes", iface);
	d.rx = fgetl(file);
	sprintf(file, "/sys/class/net/%s/statistics/tx_bytes", iface);
	d.tx = fgetl(file);
	if (rx == -1 || tx == -1) {
		free(d.rxs);
		free(d.txs);
		endwin();
		fprintf(stderr, "cant find network interface %s\n", iface);
		fprintf(stderr, "you can select interface with: -i <interface>\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < COLS-1; i++) {
		d.rxs[i] = d.rxs[i+1];
		d.txs[i] = d.txs[i+1];
	}

	d.rxs[COLS-1] = (d.rx - rx) / prefix / delay;
	d.txs[COLS-1] = (d.tx - tx) / prefix / delay;

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
	int prefix;
	char key;
	char units[2][3][4] = {{ "KiB", "MiB", "GiB" }, { "kB", "MB", "GB" }};
	struct iface d = {.rx = 0, .tx = 0, .rxmax = 0, .txmax = 0, .graphmax = 0};

	strncpy(iface, "", IFNAMSIZ);

	arg(argc, argv);

	if (iface[0] == '\0')
		ifaceup();

	prefix = unit ? 1000.0 : 1024.0;

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
		if (key != ERR && tolower(key) == 'q')
			break;

		if (resize)
			d = scalegraph(d);

		printgraph(d, prefix, units[unit]);

		d = getdata(d, prefix);
	}

	free(d.rxs);
	free(d.txs);
	endwin();
	return EXIT_SUCCESS;
}
