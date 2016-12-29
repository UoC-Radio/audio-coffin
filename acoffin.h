/*
 * Audio Coffin - A simple audio recorder/logger on top of Jack,
 * libsndfile and libsoxr. Main Header
 *
 * Copyright (C) 2016 Nick Kossifidis <mickflemm@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdint.h>		/* For typed ints */
#include <gtk/gtk.h>		/* For GTK types */
#include <jack/jack.h>		/* For jack-related types */
#include <sndfile.h>		/* For output handling */
#include <soxr-lsr.h>		/* For the resampler */
#include <signal.h>		/* For sig_atomic_t */

struct recorder {
	uint8_t opmode;
	/* GUI stuff */
	int headless;
	GtkWidget *window;
	GtkWidget *button;
	GtkWidget *button_image;
	GdkPixbuf *active_pbuf;
	GdkPixbuf *inactive_pbuf;
	GtkWidget *timer;
	GtkWidget *level_left;
	GtkWidget *level_right;
	/* Jack-related */
	jack_port_t *inL;
	jack_port_t *inR;
	jack_client_t *client;
	/* Input Info */
	int stereo;
	/* Input buffer */
	float *inbuff;
	size_t inbuff_size;
	/* Amplitude values */
	float left_amp;
	float right_amp;
	/* Output info */
	char *storage_path;
	SNDFILE *out;
	SF_INFO info;
	int format;
	double quality;
	double comp_level;
	uint32_t sample_rate;
	/* Output buffer */
	float *outbuff;
	size_t outbuff_size;
	/* Resampler */
	SRC_STATE *resampler_state;
	SRC_DATA resampler_data;
	double resampler_ratio;
	int max_out_frames;
	/* Consumer */
	float *inbuff_copy;
	int num_frames;
	int rtprio;
	/* Timer */
	uint32_t logrotate_interval_secs;
	uint32_t secs_recorded;
	uint32_t rotations;
};

extern volatile sig_atomic_t recorder_state;

enum recorder_states {
	RECORDER_NOT_INITIALIZED = 0,
	RECORDER_RUNNING = 1,
	RECORDER_STOPPED = 2,
	RECORDER_TRANSITION = 3,
	RECORDER_DELAYED_STOP = 4
};

#define RECORDER_STOP_DELAY_SECS 2

enum recorder_error_codes {
	RECORDER_JACKD_ERR = -1,
	RECORDER_SNDFILE_ERR = -2,
	RECORDER_RESAMPLER_ERR = -3,
	RECORDER_NOMEM = -4,
	RECORDER_INVALID = -5,
	RECORDER_AGAIN = -6,
	RECORDER_TIMER_ERR = -7,
	RECORDER_CONSUMER_ERR = -8,
	RECODER_ERR_MAX = -9
};

enum recorder_modes {
	RECORDER_LIVE = 0,
	RECORDER_LOGGER = 1
};

enum recorder_formats {
	RECORDER_FORMAT_FLAC = 0,
	RECORDER_FORMAT_OGG_VORBIS = 1,
};

extern volatile sig_atomic_t gui_state;

enum gui_states {
	GUI_NOT_INITIALIZED = 0,
	GUI_READY = 1
};

extern volatile sig_atomic_t button_state;

enum gui_button_states {
	GUI_BUTTON_RAISED = 1,
	GUI_BUTTON_PRESSED = 2,
	GUI_BUTTON_DISABLED = 3
};

/* GUI */
gboolean gui_update_meters(gpointer data);
gboolean gui_update_timer_label(gpointer data);
gboolean gui_update_button_state(gpointer data);
int gui_initialize(int argc, char *argv[], struct recorder *rcd);
gboolean gui_cleanup(gpointer data);

/* Recorder */
int recorder_start(struct recorder *rcd);
int recorder_stop(struct recorder *rcd);
int recorder_initialize(struct recorder *rcd);
void recorder_cleanup(struct recorder *rcd);
