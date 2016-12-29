/*
 * Audio Coffin - A simple audio recorder/logger on top of Jack,
 * libsndfile and libsoxr. GTK GUI
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
#include <math.h>		/* For log10 and fabs */

volatile sig_atomic_t button_state = GUI_BUTTON_RAISED;
volatile sig_atomic_t gui_state = GUI_NOT_INITIALIZED;

/**********************\
* TIMER LABEL HANDLING *
\**********************/

/**
 * Updates the timer label to the given value
 */
gboolean
gui_update_timer_label(gpointer data)
{
	struct recorder *rcd = (struct recorder *)data;
	GtkWidget *timer = rcd->timer;
	char str_buff[16] = { 0 };
	uint32_t mins = 0;
	uint32_t hours = 0;
	uint32_t secs = 0;

	mins = rcd->secs_recorded / 60;
	hours = mins / 60;
	mins %= 60;
	secs = rcd->secs_recorded % 60;

	if (rcd->opmode == RECORDER_LOGGER)
		snprintf(str_buff, 16, "[%03i] %02i:%02i:%02i", rcd->rotations,
			 hours, mins, secs);
	else
		snprintf(str_buff, 10, "%02i:%02i:%02i", hours, mins, secs);

	gtk_label_set_label(GTK_LABEL(timer), str_buff);

	/* Always return false or it'll loop */
	return FALSE;
}

/*****************\
* BUTTON HANDLING *
\*****************/

/**
 * Button click callback from GTK
 */
static void
gui_button_action(GtkWidget * widget, gpointer data)
{
	struct recorder *rcd = (struct recorder *)data;
	GtkWidget *record_button = widget;
	GtkWidget *timer = rcd->timer;

	/* Ignore any signals when disabled */
	if (button_state == GUI_BUTTON_DISABLED)
		return;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		recorder_start(rcd);
	else
		recorder_stop(rcd);

	if (recorder_state == RECORDER_RUNNING)
		gtk_image_set_from_pixbuf(GTK_IMAGE(rcd->button_image),
					  rcd->active_pbuf);
	else
		gtk_image_set_from_pixbuf(GTK_IMAGE(rcd->button_image),
					  rcd->inactive_pbuf);

	return;
}

/**
 * Button state update callback from recorder
 */
gboolean
gui_update_button_state(gpointer data)
{
	struct recorder *rcd = (struct recorder *)data;
	GtkToggleButton *button = GTK_TOGGLE_BUTTON(rcd->button);

	switch (button_state) {
	case GUI_BUTTON_PRESSED:
		gtk_toggle_button_set_inconsistent(button, FALSE);
		gtk_toggle_button_set_active(button, TRUE);
		gtk_widget_set_sensitive(rcd->button, TRUE);
		break;
	case GUI_BUTTON_RAISED:
		gtk_toggle_button_set_inconsistent(button, FALSE);
		gtk_toggle_button_set_active(button, FALSE);
		gtk_widget_set_sensitive(rcd->button, TRUE);
		break;
	case GUI_BUTTON_DISABLED:
		gtk_toggle_button_set_inconsistent(button, TRUE);
		gtk_widget_set_sensitive(rcd->button, FALSE);
		break;
	default:
		return FALSE;
	}

	/* Always return false or it'll loop */
	return FALSE;
}

/***************************\
* PEAK LEVEL METER HANDLING *
\***************************/

/**
 * IEC standard dB scaling, borrowed from meterbridge (c) Steve Harris
 */
static float
iec_scale(float db)
{
	float def = 0.0f;	/* Meter deflection %age */

	if (db < -70.0f) {
		def = 0.0f;
	} else if (db < -60.0f) {	/* 0.0 - 2.5 */
		def = (db + 70.0f) * 0.25f;
	} else if (db < -50.0f) {	/* 2.5 - 7.5 */
		def = (db + 60.0f) * 0.5f + 2.5f;
	} else if (db < -40.0f) {	/* 7.5 - 15.0 */
		def = (db + 50.0f) * 0.75f + 7.5f;
	} else if (db < -30.0f) {	/* 15.0 - 30.0 */
		def = (db + 40.0f) * 1.5f + 15.0f;
	} else if (db < -20.0f) {	/* 30.0 - 50.0 */
		def = (db + 30.0f) * 2.0f + 30.0f;
	} else {		/* 50 - 100 */
		def = (db + 20.0f) * 2.5f + 50.0f;
	}

	return def;
}

/**
 * Level state callback from recorder
 */
gboolean
gui_update_meters(gpointer data)
{
	struct recorder *rcd = (struct recorder *)data;
	float db_left = 0.0f;
	float db_right = 0.0f;

	/* Amplitude to db + iec scaling */
	db_left = 20.0f * log10(rcd->left_amp);
	db_left = iec_scale(db_left) / 100;

	if (rcd->stereo) {
		db_right = 20.0f * log10(rcd->right_amp);
		db_right = iec_scale(db_right) / 100;
		gtk_level_bar_set_value(GTK_LEVEL_BAR(rcd->level_right),
					db_right);
	}

	gtk_level_bar_set_value(GTK_LEVEL_BAR(rcd->level_left), db_left);

	/* Always return false or it'll loop */
	return FALSE;
}

/****************\
* INIT / CLEANUP *
\****************/

int
gui_initialize(int argc, char *argv[], struct recorder *rcd)
{
	int ret = 0;
	GtkWidget *window = NULL;
	GdkPixbuf *pixbuf_app_icon = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *timer = NULL;
	GtkWidget *button_image = NULL;
	GdkPixbuf *pixbuf_img_active = NULL;
	GdkPixbuf *pixbuf_img_inactive = NULL;
	GtkWidget *record_button = NULL;
	GtkWidget *level_left = NULL;
	GtkWidget *separator = NULL;
	GtkWidget *level_right = NULL;
	GtkCssProvider *provider = NULL;
	GdkDisplay *display = NULL;
	GdkScreen *screen = NULL;

	/* Initialize gtk */
	gtk_init(&argc, &argv);

	/* Get application icon and create a pixbuf from it */
	pixbuf_app_icon =
	    gdk_pixbuf_new_from_file_at_scale(DATA_PATH"dracula.png", 96,
					      96, TRUE, NULL);
	if (!pixbuf_app_icon) {
		ret = -1;
		goto cleanup;
	}

	/* Create top level window */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!window)
		return -2;
	gtk_window_set_title(GTK_WINDOW(window), "Audio Coffin");
	if (rcd->opmode == RECORDER_LOGGER)
		gtk_widget_set_size_request(window, 250, 68);
	else
		gtk_widget_set_size_request(window, 250, 250);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

	/* Set the icon of the window */
	gtk_window_set_icon(GTK_WINDOW(window), pixbuf_app_icon);

	/* Add event handler for closing the window */
	g_signal_connect(window, "delete-event", G_CALLBACK(gtk_main_quit),
			 NULL);
	rcd->window = window;

	/* Create a vertical box */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if (!vbox) {
		ret = -3;
		goto cleanup;
	}


	/* Create the label for the timer */
	timer = gtk_label_new("00:00:00");


	/* If we are on logger mode there is no button to click, so skip this part */
	if (rcd->opmode == RECORDER_LOGGER)
		goto nobutton;

	/* Create the image to put inside the record button and initialize the two
	 * pixbufs for active and inactive states */
	pixbuf_img_active =
	    gdk_pixbuf_new_from_file_at_scale(DATA_PATH"record_active.png", 160,
					      160, TRUE, NULL);
	if (!pixbuf_img_active) {
		ret = -4;
		goto cleanup;
	}
	rcd->active_pbuf = pixbuf_img_active;
	pixbuf_img_inactive =
	    gdk_pixbuf_new_from_file_at_scale(DATA_PATH"record_inactive.png", 160,
					      160, TRUE, NULL);
	if (!pixbuf_img_inactive) {
		ret = -5;
		goto cleanup;
	}
	rcd->inactive_pbuf = pixbuf_img_inactive;

	button_image = gtk_image_new_from_pixbuf(pixbuf_img_inactive);
	if (!button_image) {
		ret = -6;
		goto cleanup;
	}
	rcd->button_image = button_image;

	/* Create the record button */
	record_button = gtk_toggle_button_new();
	if (!record_button) {
		ret = -7;
		goto cleanup;
	}
	rcd->button = record_button;
	gtk_button_set_image(GTK_BUTTON(record_button), button_image);
	g_signal_connect(record_button, "toggled",
			 G_CALLBACK(gui_button_action), (gpointer) rcd);

 nobutton:

	/* Create the level indicators */
	level_left = gtk_level_bar_new();
	if (!level_left) {
		ret = -8;
		goto cleanup;
	}
	gtk_level_bar_add_offset_value(GTK_LEVEL_BAR(level_left),
				       GTK_LEVEL_BAR_OFFSET_HIGH, 0.25);
	gtk_level_bar_add_offset_value(GTK_LEVEL_BAR(level_left),
				       GTK_LEVEL_BAR_OFFSET_LOW, 0.85);
	rcd->level_left = level_left;

	if (rcd->stereo) {
		level_right = gtk_level_bar_new();
		if (!level_right) {
			ret = -9;
			goto cleanup;
		}
		gtk_level_bar_add_offset_value(GTK_LEVEL_BAR(level_right),
					       GTK_LEVEL_BAR_OFFSET_HIGH, 0.25);
		gtk_level_bar_add_offset_value(GTK_LEVEL_BAR(level_right),
					       GTK_LEVEL_BAR_OFFSET_LOW, 0.85);
		rcd->level_right = level_right;

		separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		if (!separator) {
			ret = -10;
			goto cleanup;
		}
	}


	/* Set the colors / styles etc */
	provider = gtk_css_provider_new();
	display = gdk_display_get_default();
	screen = gdk_display_get_default_screen(display);

	gtk_style_context_add_provider_for_screen(screen,
						  GTK_STYLE_PROVIDER(provider),
						  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	gtk_css_provider_load_from_data(GTK_CSS_PROVIDER(provider), "GtkLabel {\n\
						font: Andale Mono;\
						font-size: 20px;\
						color: #357EC7;\
					}\n", -1,
					NULL);
	g_object_unref(provider);
	rcd->timer = timer;


	/* Put them all in and draw the window */
	gtk_box_pack_start(GTK_BOX(vbox), timer, 0, 0, 5);
	if (rcd->opmode == RECORDER_LIVE)
		gtk_box_pack_start(GTK_BOX(vbox), record_button, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(vbox), level_left, 0, 0, 1);
	if (rcd->stereo) {
		gtk_box_pack_start(GTK_BOX(vbox), separator, 0, 0, 0);
		gtk_box_pack_start(GTK_BOX(vbox), level_right, 0, 0, 1);
	}
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_widget_show_all(window);


	/* Mark GUI as ready */
	gui_state = GUI_READY;


	/* Start the gtk main loop */
	gtk_main();

 cleanup:
	return ret - RECODER_ERR_MAX;
}

/**
 * Cleanup callback from recorder
 */
gboolean gui_cleanup(gpointer data)
{
	struct recorder *rcd = (struct recorder *)data;
	gtk_widget_destroy(rcd->window);
	gui_state = GUI_NOT_INITIALIZED;
	gtk_main_quit();
}
