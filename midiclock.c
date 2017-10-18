/*
    
    OSC controlled MIDI clock source
    Copyright (C) 2017 Dougall Irving

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    Compile with:
                   gcc -o midiclock midiclock.c -lasound -llo
*/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>

#include <lo/lo.h>

#include <alsa/asoundlib.h>
#include <alsa/seq.h>

#define TICKS_PER_QUARTER 120
#define BPM 100
#define FALSE 0
#define TRUE  1
#define OSC_PORT 4040

snd_seq_t *seq_handle;
int queue_id, port_in_id, port_out_id;
int bpm = BPM;
int resolution = TICKS_PER_QUARTER;
int osc_port = OSC_PORT;
int start = FALSE;

void usage()
{
	fprintf(stderr,
		"  Receive OSC messages on localhost..  /tempo i, /start, /stop, /continue\n"
		"  Send MIDI clock messages via ALSA midi port\n\n"
		"Usage: \n"
		"  midiclock [-p|--port PORT]\n"
		"            [-r|--resolution PPQ]\n" 
		"            [-s|--start] \n"
		"            [-t|--tempo BPM]\n"
		"\n"
		"Options:\n"
		"  -h, --help         This message\n"
		"  -p, --port         OSC receive port number, default 4040\n"
		"  -r, --resolution   Tick resolution per quarter note (PPQ), default 120\n"
		"  -s, --start        Start MIDI clock automatically, default off\n"
		"  -t, --tempo        Speed, in BPM, default 100\n\n");
}

void open_sequencer()
{
	if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
		fprintf(stderr, "Error opening ALSA sequencer\n");
		exit(EXIT_FAILURE);
	}
	snd_seq_set_client_name(seq_handle, "midiclock");
	if ((port_out_id = snd_seq_create_simple_port(seq_handle, "output",
			SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
			SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC )) < 0) {
		fprintf(stderr, "Error creating output port\n");
		snd_seq_close(seq_handle);
		exit(EXIT_FAILURE);
	}
	if ((port_in_id = snd_seq_create_simple_port(seq_handle, "input",
			SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
			SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC)) < 0) {
		fprintf(stderr, "Error creating input port\n");
		snd_seq_close(seq_handle);
		exit(EXIT_FAILURE);
	}
}

/*
 * Queue commands
 *
 */

void create_queue()
{
 	queue_id = snd_seq_alloc_queue(seq_handle);

}

void set_tempo(int tempo)
{
	snd_seq_queue_tempo_t *queue_tempo;
	int truetempo = (int) ((6e7) / tempo);

	snd_seq_queue_tempo_alloca(&queue_tempo);
	snd_seq_queue_tempo_set_tempo(queue_tempo, truetempo);
	snd_seq_queue_tempo_set_ppq(queue_tempo, resolution);
	snd_seq_set_queue_tempo(seq_handle, queue_id, queue_tempo);
}

void clear_queue()
{
	snd_seq_remove_events_t *remove_ev;

	snd_seq_remove_events_alloca(&remove_ev);
	snd_seq_remove_events_set_queue(remove_ev, queue_id);
	snd_seq_remove_events_set_condition(remove_ev,
					    SND_SEQ_REMOVE_OUTPUT |
					    SND_SEQ_REMOVE_IGNORE_OFF);
	snd_seq_remove_events(seq_handle, remove_ev);
}

void start_queue()
{
	snd_seq_start_queue(seq_handle, queue_id, NULL);
	snd_seq_drain_output(seq_handle);
	
	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);
	ev.type = SND_SEQ_EVENT_START;
	snd_seq_ev_schedule_tick(&ev, queue_id, 1, 0);
	snd_seq_ev_set_source(&ev, port_out_id);
	snd_seq_ev_set_subs(&ev);
	snd_seq_event_output_direct(seq_handle, &ev);

}

void stop_queue()
{
	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);
	ev.type = SND_SEQ_EVENT_STOP;
	snd_seq_ev_schedule_tick(&ev, queue_id, 1, 0);
	snd_seq_ev_set_source(&ev, port_out_id);
	snd_seq_ev_set_subs(&ev);
	snd_seq_event_output_direct(seq_handle, &ev);

	snd_seq_stop_queue(seq_handle, queue_id, NULL);
	snd_seq_drain_output(seq_handle);
}

void continue_queue()
{
	snd_seq_continue_queue(seq_handle, queue_id, NULL);
	snd_seq_drain_output(seq_handle);

	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);
	ev.type = SND_SEQ_EVENT_CONTINUE;
	snd_seq_ev_schedule_tick(&ev, queue_id, 1, 0);
	snd_seq_ev_set_source(&ev, port_out_id);
	snd_seq_ev_set_subs(&ev);
	snd_seq_event_output_direct(seq_handle, &ev);

}

/*
 * Event commands
 *
 */

void make_repeat(int tick)
{
	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);
	ev.type = SND_SEQ_EVENT_USR1;
	snd_seq_ev_schedule_tick(&ev, queue_id, 1, tick);
	snd_seq_ev_set_dest(&ev, snd_seq_client_id(seq_handle), port_in_id);
	snd_seq_event_output_direct(seq_handle, &ev);
}

void make_clock(int tick)
{
	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);
	ev.type = SND_SEQ_EVENT_CLOCK;
	snd_seq_ev_schedule_tick(&ev, queue_id, 1, tick);
	snd_seq_ev_set_source(&ev, port_out_id);
	snd_seq_ev_set_subs(&ev);
	snd_seq_event_output_direct(seq_handle, &ev);
}
 
void pattern()
{
	int j, tick, duration;
	/* MIDI clock events.....24 times per quarter note */
	duration = resolution / 24;
	for (tick = 0; tick < resolution; tick += duration) {
		make_clock(tick);
	}
	/* schedule next quarter note */
	tick = resolution;
	make_repeat(tick);
}
void midi_action()
{
	snd_seq_event_t *ev;

	do {
		snd_seq_event_input(seq_handle, &ev);
		switch (ev->type) {
		case SND_SEQ_EVENT_USR1:
			pattern();
			break;
		case SND_SEQ_EVENT_START:
			start_queue();
			pattern();
			break;
		case SND_SEQ_EVENT_CONTINUE:
			continue_queue();
			break;
		case SND_SEQ_EVENT_STOP:
			stop_queue();
			break;
		}
	} while (snd_seq_event_input_pending(seq_handle, 0) > 0);
}

/* 
 * OSC message handlers 
 *
 */
int tempo_handler(const char *path, const char *types, lo_arg ** argv,
                int argc, void *data, void *user_data)
{
	int new_tempo = argv[0]->i;
	if ((new_tempo >= 40)&&(new_tempo <=240))
	{
		bpm = new_tempo;
		set_tempo(bpm);
	}
	return 0;
}

int start_handler(const char *path, const char *types, lo_arg ** argv,
                int argc, void *data, void *user_data)
{
	start_queue();
	pattern();
}

int stop_handler(const char *path, const char *types, lo_arg ** argv,
                int argc, void *data, void *user_data)
{
	stop_queue();
}

int continue_handler(const char *path, const char *types, lo_arg ** argv,
                int argc, void *data, void *user_data)
{
	continue_queue();
}

void sigterm_exit(int sig)
{
	clear_queue();
	sleep(1);
	snd_seq_stop_queue(seq_handle, queue_id, NULL);
	snd_seq_free_queue(seq_handle, queue_id);
	snd_seq_close(seq_handle);
	exit(0);
}

int check_range(int val, int min, int max, char *msg)
{
	if ((val < min) | (val > max)) {
		fprintf(stderr, "Invalid %s, range is %d to %d\n", msg, min, max);
		return 1;
	}
	return 0;
}

int parse_options(int argc, char *argv[])
{
	int c;
	long x;
	char *sep;
	int option_index = 0;
	struct option long_options[] = {
		{"help",        0, 0, 'h'},
		{"port",	1, 0, 'p'},
		{"resolution",  1, 0, 'r'},
		{"start", 	0, 0, 's'},
		{"tempo",       1, 0, 't'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long(argc, argv, "hp:r:st:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'p':
			osc_port = atoi(optarg);
			if (check_range(osc_port, 0, 65535, "port"))
				return 1;
			break;
		case 'r':
			resolution = atoi(optarg);
			if (check_range(resolution, 48, 480, "resolution"))
				return 1;
			break;
		case 's':
			start = TRUE;
			break;
		case 't':
			bpm = atoi(optarg);
			if (check_range(bpm, 16, 240, "tempo"))
				return 1;
			break;
		case 0:
		case 'h':
		default:
			return 1;
		}
	}
	return 0;
}

void liblo_error(int num, const char *msg, const char *path)
{
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
    fflush(stdout);
}


int main(int argc, char *argv[])
{
	int npfd, j;
	struct pollfd *pfd;

	if (parse_options(argc, argv) != 0) {
		usage();
		return EXIT_FAILURE;
	}

	signal(SIGINT, sigterm_exit);
	signal(SIGTERM, sigterm_exit);

	open_sequencer();
	create_queue();

	npfd = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
	pfd = (struct pollfd *) alloca(npfd * sizeof(struct pollfd));
	snd_seq_poll_descriptors(seq_handle, pfd, npfd, POLLIN);

	set_tempo(bpm);
	
	if (start){
		start_queue();
		pattern();
	}

	/* start a new OSC server */
	char str_osc_port[20];
	sprintf(str_osc_port,"%d",osc_port);
	lo_server_thread st = lo_server_thread_new(str_osc_port, liblo_error);
    	/* add method that will match the path /tempo with an int */
	lo_server_thread_add_method(st, "/tempo", "i", tempo_handler, NULL);
	lo_server_thread_add_method(st, "/start", "", start_handler, NULL);
	lo_server_thread_add_method(st, "/stop", "", stop_handler, NULL);
	lo_server_thread_add_method(st, "/continue", "", continue_handler, NULL);
	lo_server_thread_start(st);

	while (1) {
		if (poll(pfd, npfd, 1000) > 0) {
			for (j = 0; j < npfd; j++) {
				if (pfd[j].revents > 0)
					midi_action();
			}
		}
	}
}
