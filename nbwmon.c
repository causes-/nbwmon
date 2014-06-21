#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ncurses.h>
#include <unistd.h>

#define LEN 80

char iface[LEN+1] = "wlan0";
int delay = 2;
int ident = LEN / 4;

struct data {
	// KB/s
	float rxs;
	float txs;
	// peak KB/s
	float rxmax;
	float txmax;
	float max;
	// total traffic
	int rx;
	int tx;
	int rx2;
	int tx2;
	// packets
	int rxp;
	int txp;
	// errors
	int rxe;
	int txe;
	// bandwidth graph
	float rxgraphs[LEN];
	float txgraphs[LEN];
	bool rxgraph[10][LEN];
	bool txgraph[10][LEN];
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
		fprintf(stderr, "cant read: %s\n", file);
		exit(1);
	}

	fgets(line, LEN+1, fp);
	fclose(fp);

	return strtol(line, NULL, 0);
}

void printstats(struct data d) {
	int x, y;
	char str[LEN+1];

	clear();

	sprintf(str, "interface: %s delay: %d sec\n\n", iface, delay);
	addstr(str);
	sprintf(str, "%*s %*s %s\n", ident+3, "RX", ident-3, "", "TX");
	addstr(str);

	sprintf(str, "%-*s %-*.1f %-*.1f KB/s\n", ident, "speed", ident, d.rxs, ident, d.txs);
	addstr(str);
	sprintf(str, "%-*s %-*.1f %-*.1f KB/s\n", ident, "peak", ident, d.rxmax, ident, d.txmax);
	addstr(str);
	sprintf(str, "%-*s %-*.1f %-*.1f MB\n", ident, "total", ident,
			(float) d.rx2 / 1024000, ident, (float) d.tx2 / 1024000);
	addstr(str);
	sprintf(str, "%-*s %-*d %-*d total\n", ident, "packets", ident, d.rxp, ident, d.txp);
	addstr(str);
	sprintf(str, "%-*s %-*d %-*d total\n\n", ident, "errors", ident, d.rxe, ident, d.txe);
	addstr(str);

	for (x = 9; x >= 0; x--) {
		for (y = 0; y < LEN; y++) {
			if (d.rxgraph[x][y]) {
				addch('#');
			} else {
				addch(' ');
			}
		}
		addch('\n');
	}

	addch('\n');

	for (x = 0; x < 10; x++) {
		for (y = 0; y < LEN; y++) {
			if (d.txgraph[x][y]) {
				addch('#');
			} else {
				addch(' ');
			}
		}
		addch('\n');
	}

	refresh();
}

struct data getdata(struct data d) {
	char str[LEN+1];

	sprintf(str, "/sys/class/net/%s/statistics/rx_packets", iface);
	d.rxp = fgetint(str);
	sprintf(str, "/sys/class/net/%s/statistics/tx_packets", iface);
	d.txp = fgetint(str);

	sprintf(str, "/sys/class/net/%s/statistics/rx_errors", iface);
	d.rxe = fgetint(str);
	sprintf(str, "/sys/class/net/%s/statistics/tx_errors", iface);
	d.txe = fgetint(str);

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

	d.max = d.rxmax = (d.rxs > d.max ? d.rxs : d.max);
	d.max = d.txmax = (d.txs > d.max ? d.txs : d.max);

	return d;
}

struct data updategraph(struct data d) {
	int x, y;
	float i, j;

	// scale graph
	if (d.max == d.rxs || d.max == d.txs) {
		for (x = 0; x < 10; x++) {
			for (y = 0; y < LEN; y++) {
				i = (float) d.rxgraphs[y] / d.max * 10;
				j = (float) d.txgraphs[y] / d.max * 10;
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
	for (x = 0; x < 10; x++) {
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
	i = (float) d.rxs / d.max * 10;
	j = (float) d.txs / d.max * 10;
	for (x = 0; x < 10; x++) {
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
	struct data d = {.rxs = 0, .txs = 0, .rxmax = 0, .txmax = 0, .max = 0};

	arg(argc, argv);

	initscr();
	noecho();
	curs_set(0);

	while (1) {
		printstats(d);

		d = getdata(d);

		d = updategraph(d);
	}

	endwin();

	return 0;
}
