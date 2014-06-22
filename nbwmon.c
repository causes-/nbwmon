#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>
#include <unistd.h>

#define LEN 100
#define GRAPHLEN 8

char iface[LEN+1] = "wlan0";
int delay = 1;
int indent = LEN/4;

struct data {
	// KB/s
	float rxs;
	float txs;
	// peak KB/s
	float rxmax;
	float txmax;
	float max;
	float max2;
	// total traffic
	int rx;
	int tx;
	int rx2;
	int tx2;
	// packets
	int rxp;
	int txp;
	// bandwidth graph
	float rxgraphs[LEN];
	float txgraphs[LEN];
	bool rxgraph[GRAPHLEN][LEN];
	bool txgraph[GRAPHLEN][LEN];
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

long fgetint(char *file) {
	char line[LEN+1];
	FILE *fp;

	if ((fp = fopen(file, "r")) == NULL) {
		endwin();
		fprintf(stderr, "cant find %s (%s)\n\n", iface, file);
		exit(1);
	}

	fgets(line, LEN+1, fp);
	fclose(fp);

	return strtol(line, NULL, 0);
}

void printstats(struct data d) {
	int x, y;
	char str[LEN+1];
	char maxspeed[LEN/4] = "";
	char minspeed[LEN/4] = "";

	clear();

	// print stats
	sprintf(str, "%*s %*s %s\n", indent+3, "RX", indent-3, "", "TX");
	addstr(str);

	sprintf(str, "%-*s%-*.1f%-*.1fKB/s\n", indent, "speed", indent, d.rxs, indent, d.txs);
	addstr(str);
	sprintf(str, "%-*s%-*.1f%-*.1fKB/s\n", indent, "peak", indent, d.rxmax, indent, d.txmax);
	addstr(str);
	sprintf(str, "%-*s%-*.1f%-*.1fMB\n", indent, "traffic", indent,
			(float) d.rx2 / 1024000, indent, (float) d.tx2 / 1024000);
	addstr(str);
	sprintf(str, "%-*s%-*d%-*dtotal\n", indent, "packets", indent, d.rxp, indent, d.txp);
	addstr(str);
	addch('\n');

	// print graph
	snprintf(maxspeed, LEN-1, "RX %.1f KB/s", d.max);
	snprintf(minspeed, LEN-1, "RX 0.0 KB/s");
	for (x = GRAPHLEN-1; x >= 0; x--) {
		for (y = 0; y < LEN; y++) {
			if (x == GRAPHLEN-1 && y < LEN/4 && maxspeed[y] != '\0') {
				addch(maxspeed[y]);
			} else if (x == 0 && y < LEN/4 && minspeed[y] != '\0') {
				addch(minspeed[y]);
			} else if (d.rxgraph[x][y]) {
				addch('*');
			} else {
				addch(' ');
			}
		}
		addch('\n');
	}
	addch('\n');
	snprintf(maxspeed, LEN-1, "TX %.1f KB/s", d.max);
	snprintf(minspeed, LEN-1, "TX 0.0 KB/s");
	for (x = 0; x < GRAPHLEN; x++) {
		for (y = 0; y < LEN; y++) {
			if (x == GRAPHLEN-1 && y < LEN/4 && maxspeed[y] != '\0') {
				addch(maxspeed[y]);
			} else if (x == 0 && y < LEN/4 && minspeed[y] != '\0') {
				addch(minspeed[y]);
			} else if (d.txgraph[x][y]) {
				addch('*');
			} else {
				addch(' ');
			}
		}
		addch('\n');
	}
	addch('\n');

	refresh();
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

	d.rxs = (float) (d.rx2 - d.rx) / 1024 / delay;
	d.txs = (float) (d.tx2 - d.tx) / 1024 / delay;

	d.rxmax = (d.rxs > d.rxmax ? d.rxs : d.rxmax);
	d.txmax = (d.txs > d.txmax ? d.txs : d.txmax);

	d.max2 = d.max;
	d.max = 0;
	for (i = 0; i < LEN; i++) {
		if (d.rxgraphs[i] > d.max)
			d.max = d.rxgraphs[i];
		if (d.txgraphs[i] > d.max)
			d.max = d.txgraphs[i];
	}

	return d;
}

struct data updategraph(struct data d) {
	int x, y;
	float i, j;

	// scale graph
	if (d.max == d.rxs || d.max == d.txs || d.max != d.max2) {
		for (x = 0; x < GRAPHLEN; x++) {
			for (y = 0; y < LEN; y++) {
				i = (float) d.rxgraphs[y] / d.max * GRAPHLEN;
				j = (float) d.txgraphs[y] / d.max * GRAPHLEN;
				if (i > x)
					d.rxgraph[x][y] = true;
				else
					d.rxgraph[x][y] = false;
				if (j > x)
					d.txgraph[x][y] = true;
				else
					d.txgraph[x][y] = false;
			}
		}
	}

	// move graph
	for (x = 0; x < GRAPHLEN; x++) {
		for (y = 0; y < LEN-1; y++) {
			d.rxgraph[x][y] = d.rxgraph[x][y+1];
			d.txgraph[x][y] = d.txgraph[x][y+1];
		}
	}
	for (y = 0; y < LEN-1; y++) {
		d.rxgraphs[y] = d.rxgraphs[y+1];
		d.txgraphs[y] = d.txgraphs[y+1];
	}

	// create new graph line
	i = (float) d.rxs / d.max * GRAPHLEN;
	j = (float) d.txs / d.max * GRAPHLEN;
	for (x = 0; x < GRAPHLEN; x++) {
		if (i > x)
			d.rxgraph[x][LEN-1] = true;
		else
			d.rxgraph[x][LEN-1] = false;
		if (j > x)
			d.txgraph[x][LEN-1] = true;
		else
			d.txgraph[x][LEN-1] = false;
	}
	d.rxgraphs[LEN-1] = d.rxs;
	d.txgraphs[LEN-1] = d.txs;

	return d;
}

int main(int argc, char *argv[]) {
	char key;
	struct data d = {.rxs = 0, .txs = 0};

	arg(argc, argv);

	initscr();
	curs_set(0);
	noecho();
	nodelay(stdscr, TRUE);

	while (1) {
		printstats(d);

		key = getch();
		if ( key != ERR && tolower(key) == 'q') {
			endwin();
			exit(1);
		}

		d = getdata(d);

		d = updategraph(d);
	}

	endwin();

	return 0;
}
