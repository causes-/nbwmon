#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>
#include <unistd.h>
#include <signal.h>

#define LEN 100

char iface[LEN+1] = "wlan0";
int delay = 1;
int graphlines = 10;
int terminate = -1;
bool colors = true;
bool resize = false;

struct data {
	// total traffic
	long rx;
	long tx;
	// max KB/s
	double rxmax;
	double txmax;
	double graphmax;
	// graph KB/s
	double *rxs;
	double *txs;
};

int arg(int argc, char *argv[]) {
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-i", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -i needs parameter\n");
				endwin();
				exit(1);
			}
			strncpy(iface, argv[++i], LEN-1);
		} else if (!strcmp("-d", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -d needs parameter\n");
				endwin();
				exit(1);
			}
			delay = strtol(argv[++i], NULL, 0);
		} else if (!strcmp("-l", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -l needs parameter\n");
				endwin();
				exit(1);
			}
			graphlines = strtol(argv[++i], NULL, 0);
		} else if (!strcmp("-n", argv[i])) {
			colors = false;
		} else {
			fprintf(stderr, "usage: %s [options]\n", argv[0]);
			fprintf(stderr, "-h         help\n");
			fprintf(stderr, "-n         no colors\n");
			fprintf(stderr, "-i <iface> interface\n");
			fprintf(stderr, "-d <delay> delay\n");
			fprintf(stderr, "-l <lines> graph height\n");
			endwin();
			exit(1);
		}
	}

	return 0;
}

void sighandler(int sig) {
	if (sig == SIGWINCH) {
		resize = true;
		signal(SIGWINCH, sighandler);
	}
}

long fgetl(char *file) {
	char line[LEN+1];
	FILE *fp;

	if ((fp = fopen(file, "r")) == NULL) {
		endwin();
		fprintf(stderr, "cant find network interface: %s\n", iface);
		fprintf(stderr, "you can select interface with nbwmon -i wlan0\n");
		terminate = 1;
		return 0;
	}

	fgets(line, LEN, fp);
	fclose(fp);

	return strtol(line, NULL, 0);
}

struct data scalegraph(struct data d) {
	int x, y;
	int COLS2 = COLS;
	double *rxs;
	double *txs;

	resize = false;

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
		endwin();
		fprintf(stderr, "memory allocation failed");
		free(rxs);
		free(txs);
		terminate = 1;
		return d;
	}

	for (x = COLS2-1, y = COLS-1; x >= 0 && y >= 0; x--, y--) {
		d.rxs[y] = rxs[x];
		d.txs[y] = txs[x];
	}

	free(rxs);
	free(txs);

	return d;
}

void printgraph(struct data d) {
	int x, y;
	double i;

	clear();

	mvprintw(0, (COLS/4)-9, "%7s %.2lf KB/s", "RX:", d.rxs[COLS-1]);
	mvprintw(0, (COLS/4)-9+(COLS/2), "%7s %.2lf KB/s", "TX:", d.txs[COLS-1]);
	mvprintw(1, (COLS/4)-9, "%7s %.2lf KB/s", "max:", d.rxmax);
	mvprintw(1, (COLS/4)-9+(COLS/2), "%7s %.2lf KB/s", "max:", d.txmax);
	mvprintw(2, (COLS/4)-9, "%7s %.2lf MB", "total:", (double) d.rx / 1024000);
	mvprintw(2, (COLS/4)-9+(COLS/2), "%7s %.2lf MB", "total:", (double) d.tx / 1024000);
	addch('\n');

	attron(COLOR_PAIR(1));
	for (y = graphlines-1; y >= 0; y--) {
		for (x = 0; x < COLS; x++) {
			i = d.rxs[x] / d.graphmax * graphlines;
			if (i > y)
				addch('*');
			else
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
				addch(' ');
		}
	}
	attroff(COLOR_PAIR(2));
	addch('\n');

	mvprintw(3, 0, "%.2lf KB/s", d.graphmax);
	mvprintw(graphlines+2, 0, "0.00 KB/s");
	mvprintw(graphlines+3, 0, "0.00 KB/s");
	mvprintw(graphlines*2+2, 0, "%.2lf KB/s", d.graphmax);

	refresh();
}

struct data getdata(struct data d) {
	char str[LEN+1];
	int i;
	double rx, tx;

	sprintf(str, "/sys/class/net/%s/statistics/rx_bytes", iface);
	rx = fgetl(str);
	sprintf(str, "/sys/class/net/%s/statistics/tx_bytes", iface);
	tx = fgetl(str);
	if (terminate != -1)
		return d;

	sleep(delay);

	if (resize == true)
		return d;

	sprintf(str, "/sys/class/net/%s/statistics/rx_bytes", iface);
	d.rx = fgetl(str);
	sprintf(str, "/sys/class/net/%s/statistics/tx_bytes", iface);
	d.tx = fgetl(str);
	if (terminate != -1)
		return d;

	for (i = 0; i < COLS-1; i++) {
		d.rxs[i] = d.rxs[i+1];
		d.txs[i] = d.txs[i+1];
	}

	d.rxs[COLS-1] = (double) (d.rx - rx) / 1024 / delay;
	d.txs[COLS-1] = (double) (d.tx - tx) / 1024 / delay;

	if (d.rxs[COLS-1] > d.rxmax)
		d.rxmax = d.rxs[COLS-1];
	if (d.txs[COLS-1] > d.txmax)
		d.txmax = d.txs[COLS-1];

	d.graphmax = 0;
	for (i = 0; i < COLS; i++) {
		if (d.rxs[i] > d.graphmax)
			d.graphmax = d.rxs[i];
		if (d.txs[i] > d.graphmax)
			d.graphmax = d.txs[i];
	}

	return d;
}

int main(int argc, char *argv[]) {
	struct data d = {.rx = 0, .tx = 0, .rxmax = 0, .txmax = 0, .graphmax = 0};
	char key;

	arg(argc, argv);

	initscr();
	curs_set(0);
	noecho();
	nodelay(stdscr, TRUE);

	d.rxs = calloc(COLS, sizeof(double));
	d.txs = calloc(COLS, sizeof(double));

	if (d.rxs == NULL || d.txs == NULL) {
		endwin();
		fprintf(stderr, "memory allocation failed");
		terminate = 1;
	}

	if (colors == true && has_colors() != FALSE) {
		start_color();
		init_pair(1, COLOR_GREEN, COLOR_BLACK);
		init_pair(2, COLOR_RED, COLOR_BLACK);
	} else {
		colors = false;
	}
	
	signal(SIGWINCH, sighandler);

	do {
		key = getch();
		if (key != ERR && tolower(key) == 'q') {
			terminate = 0;
			break;
		}

		if (resize == true)
			d = scalegraph(d);

		printgraph(d);

		d = getdata(d);
	} while (terminate == -1);

	endwin();

	free(d.rxs);
	free(d.txs);

	return terminate == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
