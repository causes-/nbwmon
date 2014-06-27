#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <ncurses.h>

#define LEN 256

char iface[LEN+1] = "";
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
		if (!strcmp("-i", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -i needs parameter\n");
				exit(EXIT_FAILURE);
			}
			strncpy(iface, argv[++i], LEN-1);
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
		} else if (!strcmp("-n", argv[i])) {
			colors = 0;
		} else {
			fprintf(stderr, "usage: %s [options]\n", argv[0]);
			fprintf(stderr, "-h           help\n");
			fprintf(stderr, "-n           no colors\n");
			fprintf(stderr, "-i <iface>   interface\n");
			fprintf(stderr, "-d <seconds> delay\n");
			fprintf(stderr, "-l <lines>   graph height\n");
			exit(EXIT_FAILURE);
		}
	}
}

void ifaceup (void) {
	char file[LEN+1];
	char line[LEN+1];
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

			if (fgets(line, 256, fp)) {
				strtok(line, "\n");

				// ppp0 shows UNKNOWN but only appears when connected
				if (!strcmp("ppp0", dir->d_name))
					strncpy(iface, dir->d_name, LEN);

				if (!strcmp("up", line))
					strncpy(iface, dir->d_name, LEN);
			}
			fclose(fp);
		}
	}
}

struct iface scalegraph(struct iface d) {
	int i, j;
	int COLS2 = COLS;
	double *rxs;
	double *txs;

	resize = 0;

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

void printgraph(struct iface d) {
	int x, y;
	double i;

	for (y = 0; y < 3; y++)
		for (x = 0; x < COLS; x++)
			mvprintw(y, x, " ");

	if (d.rxs[COLS-1] > 1024 || d.txs[COLS-1] > 1024) {
		mvprintw(0, (COLS/4)-9, "%7s %.2lf MiB/s", "RX:", d.rxs[COLS-1] / 1024);
		mvprintw(0, (COLS/4)-9+(COLS/2), "%7s %.2lf MiB/s", "TX:",
				d.txs[COLS-1] / 1024);
	} else {
		mvprintw(0, (COLS/4)-9, "%7s %.2lf KiB/s", "RX:", d.rxs[COLS-1]);
		mvprintw(0, (COLS/4)-9+(COLS/2), "%7s %.2lf KiB/s", "TX:", d.txs[COLS-1]);
	}

	if (d.rxmax > 1024 || d.txmax > 1024) {
		mvprintw(1, (COLS/4)-9, "%7s %.2lf MiB/s", "max:", d.rxmax / 1024);
		mvprintw(1, (COLS/4)-9+(COLS/2), "%7s %.2lf MiB/s", "max:", d.txmax / 1024);
	} else {
		mvprintw(1, (COLS/4)-9, "%7s %.2lf KiB/s", "max:", d.rxmax);
		mvprintw(1, (COLS/4)-9+(COLS/2), "%7s %.2lf KiB/s", "max:", d.txmax);
	}

	if (d.rx / 1024 / 1024 > 1024 || d.tx / 1024 / 1024 > 1024) {
		mvprintw(2, (COLS/4)-9, "%7s %.2lf GiB", "total:",
				(double) d.rx / 1024 / 1024 / 1024);
		mvprintw(2, (COLS/4)-9+(COLS/2), "%7s %.2lf GiB", "total:",
				(double) d.tx / 1024 / 1024 / 1024);
	} else {
		mvprintw(2, (COLS/4)-9, "%7s %.2lf MiB", "total:",
				(double) d.rx / 1024 / 1024);
		mvprintw(2, (COLS/4)-9+(COLS/2), "%7s %.2lf MiB", "total:",
				(double) d.tx / 1024 / 1024);
	}

	addch('\n');

	attron(COLOR_PAIR(1));
	for (y = graphlines-1; y >= 0; y--) {
		for (x = 0; x < COLS; x++) {
			i = d.rxs[x] / d.graphmax * graphlines;
			i > y ? addch('*') : addch(' ');
		}
	}
	attroff(COLOR_PAIR(1));

	attron(COLOR_PAIR(2));
	for (y = 0; y <= graphlines-1; y++) {
		for (x = 0; x < COLS; x++) {
			i = d.txs[x] / d.graphmax * graphlines;
			i > y ? addch('*') : addch(' ');
		}
	}
	attroff(COLOR_PAIR(2));

	addch('\n');

	if (d.graphmax > 1024) {
		mvprintw(3, 0, "%.2lf MiB/s", d.graphmax / 1024);
		mvprintw(graphlines+2, 0, "0.00 MiB/s");
		mvprintw(graphlines+3, 0, "0.00 MiB/s");
		mvprintw(graphlines*2+2, 0, "%.2lf MiB/s", d.graphmax / 1024);
	} else {
		mvprintw(3, 0, "%.2lf KiB/s", d.graphmax);
		mvprintw(graphlines+2, 0, "0.00 KiB/s");
		mvprintw(graphlines+3, 0, "0.00 KiB/s");
		mvprintw(graphlines*2+2, 0, "%.2lf KiB/s", d.graphmax);
	}

	mvprintw(graphlines*2+3, COLS/2-9, "interface: %s", iface);

	refresh();
}

long fgetl(char *file) {
	char line[LEN+1];
	FILE *fp;

	if ((fp = fopen(file, "r")) == NULL)
		return -1;

	if (fgets(line, LEN, fp) == NULL) {
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return strtol(line, NULL, 0);
}

struct iface getdata(struct iface d) {
	char file[LEN+1];
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

	d.rxs[COLS-1] = (double) (d.rx - rx) / 1024 / delay;
	d.txs[COLS-1] = (double) (d.tx - tx) / 1024 / delay;

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
	struct iface d = {.rx = 0, .tx = 0, .rxmax = 0, .txmax = 0, .graphmax = 0};
	char key;

	arg(argc, argv);

	if (iface[0] == '\0')
		ifaceup();

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

		printgraph(d);
		d = getdata(d);
	}

	free(d.rxs);
	free(d.txs);
	endwin();
	return EXIT_SUCCESS;
}
