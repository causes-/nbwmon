#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>
#include <unistd.h>
#include <signal.h>

#define GRAPHLINES 10
#define LEN 100

char iface[LEN+1] = "wlan0";
int delay = 1;
bool resize;

struct data {
	// KB/s
	double rxs;
	double txs;
	// peak KB/s
	double rxmax;
	double txmax;
	double max;
	double max2;
	// total traffic
	long rx;
	long tx;
	long rx2;
	long tx2;
	// packets
	long rxp;
	long txp;
	// bandwidth graph
	double *rxgraphs;
	double *txgraphs;
	bool *rxgraph;
	bool *txgraph;
};

int arg(int argc, char *argv[]) {
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-d", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -d needs parameter\n");
				endwin();
				exit(1);
			}
			delay = strtol(argv[++i], NULL, 0);
		} else if (!strcmp("-i", argv[i])) {
			if (argv[i+1] == NULL || argv[i+1][0] == '-') {
				fprintf(stderr, "error: -i needs parameter\n");
				endwin();
				exit(1);
			}
			strncpy(iface, argv[++i], LEN);
		} else {
			fprintf(stderr, "usage: %s [options]\n", argv[0]);
			fprintf(stderr, "-h         help\n");
			fprintf(stderr, "-d <delay> delay\n");
			fprintf(stderr, "-i <iface> interface\n");
			endwin();
			exit(1);
		}
	}

	return 0;
}

void sighandler(int signum) {
	if (signum == SIGWINCH) {
		resize = true;
		signal(SIGWINCH, sighandler);
	}
}

void quit(int i, struct data d) {
	free(d.rxgraphs);
	free(d.txgraphs);
	free(d.rxgraph);
	free(d.txgraph);

	endwin();
	exit(i);
}

struct data resizegraph(struct data d) {
	int COLS2 = COLS;
	int x, y;
	double *rxgraphs;
	double *txgraphs;

	// update LINES and COLS
	endwin();
	refresh();
	clear();

	// return if width is unchanged
	if (COLS == COLS2)
		return d;
	
	// save temporary graph speeds
	rxgraphs = d.rxgraphs;
	txgraphs = d.txgraphs;

	// free graph
	free(d.rxgraph);
	free(d.txgraph);

	// allocate new graph
	d.rxgraphs = calloc(COLS, sizeof(double));
	d.txgraphs = calloc(COLS, sizeof(double));
	d.rxgraph = calloc(GRAPHLINES * COLS, sizeof(bool));
	d.txgraph = calloc(GRAPHLINES * COLS, sizeof(bool));

	if (d.rxgraph == NULL || d.txgraph == NULL || d.rxgraphs == NULL || d.txgraphs == NULL) {
		fprintf(stderr, "memory allocation failed");
		quit(1, d);
	}

	// copy temporary graph speeds to new graph
	for (x = COLS2-1, y = COLS-1; x > 0 && y > 0; x--, y--) {
		d.rxgraphs[y] = rxgraphs[x];
		d.txgraphs[y] = txgraphs[x];
	}

	// free temporary graph speeds
	free(rxgraphs);
	free(txgraphs);

	// make updategraph() to redraw graph
	d.max = 0;

	return d;
}

void printstats(struct data d) {
	int x, y;
	char str[COLS+1];
	char maxspeed[COLS/4+1];
	char minspeed[COLS/4+1];
	int indent = COLS/4;

	clear();

	// print stats
	sprintf(str, "%*s%*s\n", indent+2, "RX", indent, "TX");
	addstr(str);

	sprintf(str, "%-*s%-*.1lf%-*.1lfKB/s\n", indent, "speed", indent, d.rxs, indent, d.txs);
	addstr(str);
	sprintf(str, "%-*s%-*.1lf%-*.1lfKB/s\n", indent, "peak", indent, d.rxmax, indent, d.txmax);
	addstr(str);
	sprintf(str, "%-*s%-*.1lf%-*.1lfMB\n", indent, "traffic", indent,
			(double) d.rx2 / 1024000, indent, (double) d.tx2 / 1024000);
	addstr(str);
	sprintf(str, "%-*s%-*ld%-*ldtotal\n", indent, "packets", indent, d.rxp, indent, d.txp);
	addstr(str);
	addch('\n');

	// print RX graph
	snprintf(maxspeed, COLS/4, "RX %.1f KB/s", d.max);
	snprintf(minspeed, COLS/4, "RX 0.0 KB/s");
	for (x = GRAPHLINES-1; x >= 0; x--) {
		for (y = 0; y < COLS; y++) {
			// TODO: better method for printing scale 
			maxspeed[y] == '\0' ? maxspeed[y+1] = '\0' : 0 ;
			minspeed[y] == '\0' ? minspeed[y+1] = '\0' : 0 ;
			if (x == GRAPHLINES-1 && y < COLS/4 && maxspeed[y] != '\0') {
				addch(maxspeed[y]);
			} else if (x == 0 && y < COLS/4 && minspeed[y] != '\0') {
				addch(minspeed[y]);
			} else if (*(d.rxgraph + x * COLS + y)) {
				addch('*');
			} else {
				addch(' ');
			}
		}
	}
	addch('\n');

	// print TX graph
	snprintf(maxspeed, COLS/4, "TX %.1f KB/s", d.max);
	snprintf(minspeed, COLS/4, "TX 0.0 KB/s");
	for (x = 0; x < GRAPHLINES; x++) {
		for (y = 0; y < COLS; y++) {
			if (x == GRAPHLINES-1 && y < COLS/4 && maxspeed[y] != '\0') {
				addch(maxspeed[y]);
			} else if (x == 0 && y < COLS/4 && minspeed[y] != '\0') {
				addch(minspeed[y]);
			} else if (*(d.txgraph + x * COLS + y)) {
				addch('*');
			} else {
				addch(' ');
			}
		}
	}

	refresh();
}

long fgetint(char *file) {
	char line[LEN+1];
	FILE *fp;

	if ((fp = fopen(file, "r")) == NULL) {
		// TODO: free memory
		endwin();
		fprintf(stderr, "cant find %s (%s)\n\n", iface, file);
		exit(1);
	}

	fgets(line, LEN+1, fp);
	fclose(fp);

	return strtol(line, NULL, 0);
}

struct data getdata(struct data d) {
	char str[LEN+1];
	int i;

	sprintf(str, "/sys/class/net/%s/statistics/rx_packets", iface);
	d.rxp = fgetint(str);
	sprintf(str, "/sys/class/net/%s/statistics/tx_packets", iface);
	d.txp = fgetint(str);

	sprintf(str, "/sys/class/net/%s/statistics/rx_bytes", iface);
	d.rx = fgetint(str);
	sprintf(str, "/sys/class/net/%s/statistics/tx_bytes", iface);
	d.tx = fgetint(str);

	sleep(delay);

	sprintf(str, "/sys/class/net/%s/statistics/rx_bytes", iface);
	d.rx2 = fgetint(str);
	sprintf(str, "/sys/class/net/%s/statistics/tx_bytes", iface);
	d.tx2 = fgetint(str);

	d.rxs = (d.rx2 - d.rx) / 1024 / delay;
	d.txs = (d.tx2 - d.tx) / 1024 / delay;

	d.rxmax = (d.rxs > d.rxmax ? d.rxs : d.rxmax);
	d.txmax = (d.txs > d.txmax ? d.txs : d.txmax);

	d.max2 = d.max;
	d.max = 0;

	for (i = 0; i < COLS; i++) {
		if (d.rxgraphs[i] > d.max)
			d.max = d.rxgraphs[i];
		if (d.txgraphs[i] > d.max)
			d.max = d.txgraphs[i];
	}

	return d;
}

struct data updategraph(struct data d) {
	int x, y;
	double i, j;

	// move graph
	for (x = 0; x < GRAPHLINES; x++) {
		for (y = 0; y < COLS-1; y++) {
			*(d.rxgraph + x * COLS + y) = *(d.rxgraph + x * COLS + y + 1);
			*(d.txgraph + x * COLS + y) = *(d.txgraph + x * COLS + y + 1);
		}
	}
	for (y = 0; y < COLS-1; y++) {
		d.rxgraphs[y] = d.rxgraphs[y+1];
		d.txgraphs[y] = d.txgraphs[y+1];
	}

	// create new graph line
	i = d.rxs / d.max * GRAPHLINES;
	j = d.txs / d.max * GRAPHLINES;
	for (x = 0; x < GRAPHLINES; x++) {
		if (i > x)
			*(d.rxgraph + x * COLS + COLS - 1) = true;
		else
			*(d.rxgraph + x * COLS + COLS - 1) = false;
		if (j > x)
			*(d.txgraph + x * COLS + COLS - 1) = true;
		else
			*(d.txgraph + x * COLS + COLS - 1) = false;
	}
	d.rxgraphs[COLS-1] = d.rxs;
	d.txgraphs[COLS-1] = d.txs;

	// scale graph
	if (d.max == d.rxs || d.max == d.txs || d.max != d.max2) {
		for (x = 0; x < GRAPHLINES; x++) {
			for (y = 0; y < COLS; y++) {
				i = d.rxgraphs[y] / d.max * GRAPHLINES;
				j = d.txgraphs[y] / d.max * GRAPHLINES;
				if (i > x)
					*(d.rxgraph + x * COLS + y) = true;
				else
					*(d.rxgraph + x * COLS + y) = false;
				if (j > x)
					*(d.txgraph + x * COLS + y) = true;
				else
					*(d.txgraph + x * COLS + y) = false;
			}
		}
	}

	return d;
}

int main(int argc, char *argv[]) {
	struct data d = {.rxs = 0, .txs = 0};
	char key;
	resize = false;

	arg(argc, argv);

	initscr();
	curs_set(0);
	noecho();
	nodelay(stdscr, TRUE);

	d.rxgraphs = calloc(COLS, sizeof(double));
	d.txgraphs = calloc(COLS, sizeof(double));
	d.rxgraph = calloc(GRAPHLINES * COLS, sizeof(bool));
	d.txgraph = calloc(GRAPHLINES * COLS, sizeof(bool));

	if (d.rxgraph == NULL || d.txgraph == NULL || d.rxgraphs == NULL || d.txgraphs == NULL) {
		fprintf(stderr, "memory allocation failed");
		quit(1, d);
	}
	
	signal(SIGWINCH, sighandler);

	while (1) {
		key = getch();
		if (key != ERR && tolower(key) == 'q') {
			quit(0, d);
		} else if (key != ERR && tolower(key) == 'r') {
			resize = true;
		}

		if (resize == true) {
			d = resizegraph(d);
			resize = false;
		}

		printstats(d);

		d = getdata(d);

		d = updategraph(d);
	}

	quit(0, d);

	return 0;
}
