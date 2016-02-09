/*
 * Audio Coffin - A simple audio recorder/logger on top of Jack,
 * libsndfile and libsoxr. Audior Recorder / Logger backend
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
#include "acoffin.h"
#include <stdlib.h>		/* For malloc/free */
#include <jack/thread.h>	/* For thread handling through jack */
#include <pthread.h>		/* For pthread mutex / conditional */
#include <string.h>		/* For memcpy() */
#include <limits.h>		/* For PATH_MAX */
#include <time.h>		/* For clock_* functions */
#include <signal.h>		/* For pthread_kill and signals */
#include <errno.h>		/* For EINTR */

pthread_mutex_t consumer_process_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t consumer_process_trigger = PTHREAD_COND_INITIALIZER;
volatile sig_atomic_t recorder_state = RECORDER_NOT_INITIALIZED;
static volatile sig_atomic_t consumer_active = 0;
static volatile sig_atomic_t timer_active = 0;

/*********\
* HELPERS *
\*********/

/**
 * Initializes and opens a new file for writing
 */
static SNDFILE *
recorder_open_new_file(struct recorder *rcd)
{
	int ret = 0;
	time_t curr_time = 0;
	struct tm *curr_time_info = { 0 };
	char date_time[26] = { 0 };
	char filepath[PATH_MAX] = { 0 };
	SNDFILE *out = NULL;
	char *opmode = (rcd->opmode == RECORDER_LOGGER) ? "Log" : "Live";
	char *chan_mode = (rcd->stereo) ? "stereo" : "mono";
	char *ext = (rcd->format == RECORDER_FORMAT_FLAC) ? "flac" : "ogg";

	/* Create file name based on current date and time */
	memset(filepath, 0, PATH_MAX * sizeof(char));
	time(&curr_time);
	curr_time_info = localtime(&curr_time);
	strftime(date_time, 26, "[%F]-[%T]", curr_time_info);
	snprintf(filepath, PATH_MAX, "%s/%s-%s-(%s).%s", rcd->storage_path,
		 opmode, date_time, chan_mode, ext);

	/* Open file with libsndfile for writing */
	out = sf_open(filepath, SFM_WRITE, &rcd->info);
	if (!out) {
		perror("cannot open file for writing");
		ret = RECORDER_SNDFILE_ERR;
		goto cleanup;
	}

	ret = sf_command(out, SFC_SET_VBR_ENCODING_QUALITY,
			 &rcd->quality, sizeof(double));
	if (ret != SF_TRUE) {
		ret = RECORDER_SNDFILE_ERR;
		goto cleanup;
	}

	ret = sf_command(out, SFC_SET_COMPRESSION_LEVEL,
			 &rcd->comp_level, sizeof(double));
	if (ret != SF_TRUE) {
		ret = RECORDER_SNDFILE_ERR;
		goto cleanup;
	}

 cleanup:
	if (ret < 0) {
		sf_close(out);
		out = NULL;
	}
	return out;
}

/**
 * Closes the current active file pointed by rcd->out
 */
static void
recorder_close_file(struct recorder *rcd)
{
	if (rcd->out) {
		sf_close(rcd->out);
		rcd->out = NULL;
	}
	return;
}

/**
 * Creates a new file for output and switches rcd->out to it
 */
static int
recorder_switch_file(struct recorder *rcd)
{
	int ret = 0;
	SNDFILE *new = NULL;
	SNDFILE *old = rcd->out;

	/* This function should not be called on
	 * state transitions. It's meant to be used
	 * for switching to a new file without messing
	 * with the recorder's state. */
	if (recorder_state == RECORDER_TRANSITION)
		return RECORDER_AGAIN;

	/* This doesn't make sense, in order to switch
	 * to a new file a file should be already open
	 * and active */
	if (recorder_state != RECORDER_RUNNING)
		return RECORDER_AGAIN;

	new = recorder_open_new_file(rcd);
	if (!new)
		return RECORDER_SNDFILE_ERR;

	old = rcd->out;

	/* New file opened, update the pointer on
	 * rcd->out */
	pthread_mutex_lock(&consumer_process_mutex);
	rcd->out = new;
	pthread_mutex_unlock(&consumer_process_mutex);

	/* Close the previous one */
	sf_close(old);

	/* Reset the timer */
	rcd->secs_recorded = 0;

	rcd->rotations++;

	return 0;
}


/***********************\
* GUI CALLBACK WRAPPERS *
\***********************/

/* GUI changes should be done on the main thread, so run these
 * through g_main_context_invoke */

static void
recorder_update_gui_timer_label(struct recorder *rcd)
{
	if (gui_state != GUI_READY)
		return;
	g_main_context_invoke(NULL, gui_update_timer_label, (gpointer) rcd);
	return;
}

static void
recorder_update_gui_button_state(struct recorder *rcd, int state)
{
	if (gui_state != GUI_READY)
		return;
	button_state = state;
	g_main_context_invoke(NULL, gui_update_button_state, (gpointer) rcd);
	return;
}

static void
recorder_update_gui_meters(struct recorder *rcd, float left_amp,
				       float right_amp)
{
	if (gui_state != GUI_READY)
		return;
	rcd->left_amp = left_amp;
	rcd->right_amp = right_amp;
	g_main_context_invoke(NULL, gui_update_meters, (gpointer) rcd);
	return;
}

static void
recorder_cleanup_gui(struct recorder *rcd)
{
	if (gui_state != GUI_READY)
		return;
	g_main_context_invoke(NULL, gui_cleanup, (gpointer) rcd);
	return;
}


/**************\
* TIMER THREAD *
\**************/

/*
 * Increases the timer by one second periodicaly, updates
 * the GUI and switches output file in case we run on
 * logger mode and the rotate interval has passed. Also
 * handles the delayed stop time counting / triggering.
 */

static void *
recorder_timer_loop(void *arg)
{
	struct recorder *rcd = (struct recorder *)arg;
	int ret = 0;
	struct timespec tv = { 0 };

	/* Reset timer value */
	rcd->secs_recorded = 0;
	clock_gettime(CLOCK_MONOTONIC, &tv);
	timer_active = 1;
	while (timer_active) {
		tv.tv_sec++;
		if (!rcd->headless) {
			if (recorder_state == RECORDER_RUNNING)
				recorder_update_gui_timer_label(rcd);
			if (recorder_state == RECORDER_DELAYED_STOP &&
			    rcd->secs_recorded >= RECORDER_STOP_DELAY_SECS)
				break;
		}
		if ((rcd->opmode == RECORDER_LOGGER) &&
		    (rcd->secs_recorded >= rcd->logrotate_interval_secs)) {
			ret = recorder_switch_file(rcd);
			if (ret < 0)
				break;
		}
		ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
				      &tv, NULL);
		if (ret == EINTR) {
			ret = 0;
			break;
		}
		rcd->secs_recorded++;
	}

	timer_active = 0;

	if (recorder_state == RECORDER_DELAYED_STOP || ret < 0)
		recorder_stop(rcd);

	return NULL;
}

/**
 * Starts and stops the timer thread
 */
static int
recorder_set_timer_state(struct recorder *rcd, int state)
{
	int ret = 0;
	static jack_native_thread_t timer_tid = 0;

	if (state) {
		/* Already running  */
		if (timer_active)
			return 0;

		ret = jack_client_create_thread(rcd->client, &timer_tid,
						rcd->rtprio, 1,
						recorder_timer_loop,
						(void *)rcd);
		if (ret < 0)
			return RECORDER_TIMER_ERR;
	} else {
		/* Already stopped */
		if (!timer_active)
			return 0;

		ret = pthread_cancel(timer_tid);
		if (ret != 0)
			return RECORDER_TIMER_ERR;

		timer_active = 0;
	}

	return ret;
}


/*****************\
* CONSUMER THREAD *
\*****************/

/**
 * Writes data to an open file, gets triggered by the process
 * callback and runs as a different thread.
 */
static int
recorder_consume(struct recorder *rcd)
{
	int ret = 0;
	uint32_t frames_generated = 0;

	pthread_mutex_lock(&consumer_process_mutex);
	while (pthread_cond_wait
	       (&consumer_process_trigger, &consumer_process_mutex) != 0) ;

	/* Don't attempt to write to the file because it might
	 * be closed or not exist yet. This also handles propper
	 * exit of the consumer thread (without calling recorder_stop()
	 * again), when switching states. */
	if (recorder_state != RECORDER_RUNNING)
		return 0;

	/* Resample audio to the requested output sampling rate */
	rcd->resampler_data.data_in = rcd->inbuff_copy;
	rcd->resampler_data.data_out = rcd->outbuff;
	rcd->resampler_data.input_frames = rcd->num_frames;
	rcd->resampler_data.output_frames = rcd->max_out_frames;
	rcd->resampler_data.end_of_input = 0;
	rcd->resampler_data.src_ratio = rcd->resampler_ratio;

	ret = src_process(rcd->resampler_state, &rcd->resampler_data);
	if (ret != 0) {
		fprintf(stderr, "resampler: %s (%i)\n", src_strerror(ret), ret);
		ret = RECORDER_RESAMPLER_ERR;
		goto cleanup;
	}
	frames_generated = rcd->resampler_data.output_frames_gen;

	/* Write data to file */
	ret = sf_writef_float(rcd->out, rcd->outbuff, frames_generated);
	if (ret != frames_generated)
		ret = RECORDER_SNDFILE_ERR;

 cleanup:
	pthread_mutex_unlock(&consumer_process_mutex);
	return ret;
}

/**
 * The consumer thread
 */
static void *
recorder_consumer_main_loop(void *arg)
{
	struct recorder *rcd = (struct recorder *)arg;
	int ret = 0;

	consumer_active = 1;
	while (consumer_active) {
		ret = recorder_consume(rcd);
		if (ret < 0)
			break;
	}

	if (ret < 0) {
		consumer_active = 0;
		recorder_stop(rcd);
	}

	return NULL;
}

/**
 * Starts and stops the timer thread
 */
static int
recorder_set_consumer_state(struct recorder *rcd, int state)
{
	int ret = 0;
	static jack_native_thread_t consumer_tid = 0;

	if (state) {
		/* Already started */
		if (consumer_active)
			return 0;

		ret = jack_client_create_thread(rcd->client, &consumer_tid,
						rcd->rtprio, 1,
						recorder_consumer_main_loop,
						(void *)rcd);
		if (ret < 0)
			return RECORDER_CONSUMER_ERR;
	} else {
		/* Already stopped */
		if (!consumer_active)
			return 0;

		/* Don't try to stop the consumer while the recorder
		 * is running */
		if (recorder_state == RECORDER_RUNNING)
			return RECORDER_AGAIN;

		consumer_active = 0;

		/* Unblock the consumer thread so that it exits */
		pthread_mutex_lock(&consumer_process_mutex);
		pthread_cond_signal(&consumer_process_trigger);
		pthread_mutex_unlock(&consumer_process_mutex);

		/* Wait for the consumer thread to exit */
		pthread_join(consumer_tid, NULL);
	}

	return ret;
}


/****************\
* JACK CALLBACKS *
\****************/

/**
 * Main process callback
 */
static int
recorder_process(jack_nframes_t nframes, void *arg)
{
	int ret = 0;
	struct recorder *rcd = (struct recorder *)arg;
	jack_default_audio_sample_t *left_in, *right_in;
	int i = 0;
	int c = 0;
	float peak_left = 0.0f;
	float peak_right = 0.0f;

	/* Recorder not ready */
	if (!recorder_state)
		return 0;

	if (rcd->stereo) {
		/* Grab input */
		left_in = (float *)jack_port_get_buffer(rcd->inL, nframes);
		right_in = (float *)jack_port_get_buffer(rcd->inR, nframes);

		if (left_in == NULL || right_in == NULL)
			return -1;

		/* Put frames on the buffer and update the peak levels */
		for (i = 0, c = 0; i < nframes; i++) {
			rcd->inbuff[c] = left_in[i];
			rcd->inbuff[c + 1] = right_in[i];
			c += 2;
			if (rcd->headless)
				continue;
			peak_left =
			    (left_in[i] > peak_left) ? left_in[i] : peak_left;
			peak_right =
			    (right_in[i] >
			     peak_right) ? right_in[i] : peak_right;
		}

		if (!rcd->headless)
			recorder_update_gui_meters(rcd, peak_left, peak_right);
	} else {
		left_in = (float *)jack_port_get_buffer(rcd->inL, nframes);

		if (left_in == NULL)
			return -1;

		for (i = 0; i < nframes; i++) {
			rcd->inbuff[i] = left_in[i];
			if (rcd->headless)
				continue;
			peak_left =
			    (left_in[i] > peak_left) ? left_in[i] : peak_left;
		}

		if (!rcd->headless)
			recorder_update_gui_meters(rcd, peak_left, 0);
	}

	if (recorder_state != RECORDER_RUNNING)
		return 0;

	/* Wait for the previous write to complete, copy the current
	 * buffer to inbuff_copy and trigger the next write */
	pthread_mutex_lock(&consumer_process_mutex);
	memcpy(rcd->inbuff_copy, rcd->inbuff, rcd->inbuff_size);
	rcd->num_frames = nframes;
	pthread_cond_signal(&consumer_process_trigger);
	pthread_mutex_unlock(&consumer_process_mutex);

	return 0;
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
static void
recorder_shutdown(void *arg)
{
	struct recorder *rcd = (struct recorder *)arg;

	recorder_state = RECORDER_NOT_INITIALIZED;
	recorder_set_consumer_state(rcd, 0);
	recorder_set_timer_state(rcd, 0);

	/* Close output file */
	recorder_close_file(rcd);

	/* Free buffers */
	if (rcd->inbuff)
		free(rcd->inbuff);
	if (rcd->inbuff_copy)
		free(rcd->inbuff_copy);
	if (rcd->outbuff)
		free(rcd->outbuff);

	/* Clean up GUI resources */
	if (!rcd->headless)
		recorder_cleanup_gui(rcd);

	return;
}


/**************\
* ENTRY POINTS *
\**************/

/**
 * Stops an active recording
 */
int
recorder_stop(struct recorder *rcd)
{
	int ret = 0;

	/* Avoid re-closing an already closed file and don't try to
	 * stop when switching states */
	if (recorder_state == RECORDER_STOPPED
	    || recorder_state == RECORDER_TRANSITION)
		return RECORDER_AGAIN;

	recorder_state = RECORDER_TRANSITION;

	/* Make sure that at least RECORDER_STOP_DELAY_SECS
	 * seconds have passed since the recording was started.
	 * This is to prevent users from rapidly clicking the toggle
	 * button. */
	if (!rcd->headless) {
		/* Prevent user from interacting with the button */
		if (recorder_state != RECORDER_DELAYED_STOP)
			recorder_update_gui_button_state(rcd,
							 GUI_BUTTON_DISABLED);

		/* Since the user can't interact with the button
		 * we need another way to come back here to stop
		 * the recorder. Set the state to RECORDER_DELAYED_STOP
		 * and let the timer thread make the call when it
		 * exits.*/
		if (rcd->secs_recorded < RECORDER_STOP_DELAY_SECS) {
			recorder_state = RECORDER_DELAYED_STOP;
			return RECORDER_AGAIN;
		}
	}

	/* If there is no GUI or we operate on logger
	 * mode (so no button) shut down instead */
	if (rcd->headless || rcd->opmode == RECORDER_LOGGER) {
		recorder_shutdown((void *)rcd);
		return 0;
	}

	recorder_close_file(rcd);
	recorder_set_timer_state(rcd, 0);
	recorder_state = RECORDER_STOPPED;

	if (!rcd->headless)
		recorder_update_gui_button_state(rcd, GUI_BUTTON_RAISED);

	return ret;
}

/**
 * Starts a new recording
 */
int
recorder_start(struct recorder *rcd)
{
	int ret = 0;

	/* Already running or switching states */
	if (recorder_state == RECORDER_RUNNING
	    || recorder_state == RECORDER_TRANSITION)
		return RECORDER_AGAIN;

	recorder_state = RECORDER_TRANSITION;

	if (!rcd->headless)
		recorder_update_gui_button_state(rcd, GUI_BUTTON_DISABLED);

	/* Open a new file to write to */
	rcd->out = NULL;
	rcd->out = recorder_open_new_file(rcd);
	if (!rcd->out)
		return RECORDER_SNDFILE_ERR;

	/* Create the timer thread if needed */
	ret = recorder_set_timer_state(rcd, 1);
	if (ret < 0)
		goto cleanup;

	/* Create the consumer thread if needed */
	ret = recorder_set_consumer_state(rcd, 1);
	if (ret < 0)
		goto cleanup;

 cleanup:
	if (ret < 0) {
		recorder_set_consumer_state(rcd, 0);
		recorder_set_timer_state(rcd, 0);
		recorder_close_file(rcd);
		recorder_state = RECORDER_STOPPED;
		if (!rcd->headless)
			recorder_update_gui_button_state(rcd,
							 GUI_BUTTON_RAISED);
	} else {
		recorder_state = RECORDER_RUNNING;
		if (!rcd->headless)
			recorder_update_gui_button_state(rcd,
							 GUI_BUTTON_PRESSED);
	}

	return ret;
}

/**
 * Initialize the recorder
 */
int
recorder_initialize(struct recorder *rcd)
{
	int ret = 0;
	jack_status_t status = 0;
	jack_options_t options = JackNoStartServer;
	char *client_name = NULL;
	int jack_samplerate = 0;
	int maxframes = 0;
	int num_channels = 0;

	recorder_state = RECORDER_NOT_INITIALIZED;

	/* Open a client connection to the default JACK server */
	rcd->client = jack_client_open("Audio Coffin", options, &status, NULL);
	if (rcd->client == NULL) {
		fprintf(stderr,
			"jack_client_open() failed, status = 0x%2.0x\n",
			status);
		if (status & JackServerFailed)
			fprintf(stderr, "Unable to connect to JACK server\n");
		return RECORDER_JACKD_ERR;
	}

	if (status & JackServerStarted)
		fprintf(stderr, "JACK server started\n");

	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(rcd->client);
		fprintf(stderr, "Unique name `%s' assigned\n", client_name);
	}


	/* Get maximum real time priority of jack threads */
	rcd->rtprio = jack_client_max_real_time_priority(rcd->client);
	if (rcd->rtprio < 0) {
		ret = RECORDER_JACKD_ERR;
		goto cleanup;
	}


	/* Register callbacks on JACK */
	jack_set_process_callback(rcd->client, recorder_process, rcd);
	jack_on_shutdown(rcd->client, recorder_shutdown, rcd);


	/* Register ports */
	rcd->inL = jack_port_register(rcd->client, "AudioL",
				      JACK_DEFAULT_AUDIO_TYPE,
				      JackPortIsInput, 0);
	if (rcd->inL == NULL) {
		ret = RECORDER_JACKD_ERR;
		goto cleanup;
	}

	if (rcd->stereo) {
		rcd->inR = jack_port_register(rcd->client, "AudioR",
					      JACK_DEFAULT_AUDIO_TYPE,
					      JackPortIsInput, 0);
		if (rcd->inR == NULL) {
			ret = RECORDER_JACKD_ERR;
			goto cleanup;
		}
	}


	/* Initialize output format */
	num_channels = (rcd->stereo) ? 2 : 1;
	rcd->info.samplerate = rcd->sample_rate;
	rcd->info.channels = num_channels;
	switch (rcd->format) {
	case RECORDER_FORMAT_FLAC:
		rcd->info.format = SF_FORMAT_FLAC | SF_FORMAT_FLOAT;
		break;
	case RECORDER_FORMAT_OGG_VORBIS:
		rcd->info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
		break;
	default:
		ret = RECORDER_INVALID;
		goto cleanup;
	}

	if (!sf_format_check(&rcd->info)) {
		fprintf(stderr, "output file format error\n");
		ret = RECORDER_SNDFILE_ERR;
		goto cleanup;
	}


	/* Initialize resampler */
	jack_samplerate = jack_get_sample_rate(rcd->client);
	rcd->resampler_ratio =
	    (double)rcd->sample_rate / (double)jack_samplerate;
	rcd->resampler_state = src_new(SRC_SINC_FASTEST, num_channels, &ret);
	if (ret != 0) {
		printf("resampler: %s\n", src_strerror(ret));
		return -1;
	}


	/* Initialize buffers */
	maxframes = jack_get_buffer_size(rcd->client);
	rcd->inbuff_size = num_channels * maxframes * sizeof(float);
	rcd->inbuff = malloc(rcd->inbuff_size);
	if (rcd->inbuff == NULL) {
		ret = RECORDER_NOMEM;
		goto cleanup;
	}
	rcd->inbuff_copy = malloc(rcd->inbuff_size);
	if (rcd->inbuff_copy == NULL) {
		ret = RECORDER_NOMEM;
		goto cleanup;
	}
	rcd->max_out_frames =
	    ((int)(((double)rcd->sample_rate / (double)jack_samplerate) + 1.0))
	    * num_channels * maxframes;
	rcd->outbuff = malloc(rcd->max_out_frames * sizeof(float));
	if (rcd->outbuff == NULL) {
		ret = RECORDER_NOMEM;
		goto cleanup;
	}


	/* Tell the JACK server that we are ready to roll.  Our
	 * process() callback will start running now. */
	ret = jack_activate(rcd->client);
	if (ret != 0) {
		ret = RECORDER_JACKD_ERR;
		goto cleanup;
	}


	/* No interaction when on logger mode, start the recorder
	 * immediately */
	if (rcd->opmode == RECORDER_LOGGER)
		recorder_start(rcd);
	else
		recorder_state = RECORDER_STOPPED;

 cleanup:
	if (ret < 0) {
		jack_client_close(rcd->client);
		recorder_shutdown((void *)rcd);
	}

	return ret;
}

void
recorder_cleanup(struct recorder *rcd)
{
	recorder_shutdown((void *)rcd);
	return;
}
