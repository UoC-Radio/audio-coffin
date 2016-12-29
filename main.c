/*
 * Audio Coffin - A simple audio recorder/logger on top of Jack,
 * libsndfile and libsoxr. Main Programm
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
#include <string.h>		/* For memset() */
#include <limits.h>		/* For PATH_MAX */
#include <stdio.h>		/* For printf/fprintf/perror */
#include <stdlib.h>		/* For exit() */
#include <errno.h>		/* For EINVAL */
#include <sys/stat.h>		/* For stat() etc */
#include <pwd.h>		/* For getpuid() etc */

void
usage(char *name)
{
	printf("Audio Coffin a simple audio recorder and logger for Jack\n");
	printf("\nUsage: %s -h or [<parameter> <value>] pairs\n", name);
	printf("\nParameters:\n"
	       "\t-h\t\tShow this list\n"
	       "\t-p   <string>\tSet output directory for storing files (default: ~/Recordings and ~/AudioLogs)\n"
	       "\t-m   <int>\tSet operation mode, valid values are 1 for recorder (default) and 2 for logger'\n"
	       "\t-t   <int>\tSet time interval in mins for log rotation (default is 1 hour, max is 24h), only valid for logger'\n"
	       "\t-s   <boolean>\tEnable / disable stereo operation, valid values are 0 and 1 (default)\n"
	       "\t-g   <boolean>\tEnable / disable GUI, valid values are 0 and 1 (default)\n"
	       "\t-r   <int>\tSet output sample rate, default value is 44100\n"
	       "\t-f   <int>\tSet output format, valid values are 1 for FLAC and 2 for Ogg/Vorbis (default)\n"
	       "\t-q   <double>\tSet encoding quality for the vorbis/FLAC encoder, valid values are 0.0 - 1.0 (default: 0.5)\n"
	       "\t-c   <double>\tSet compression level for the vorbis/FLAC encoder, valid values are 0.0 - 1.0 (default: 0.75)\n");
}

int
main(int argc, char *argv[])
{
	int ret = 0;
	int opt = 0;
	double tmp = 0;
	struct recorder rcd = { 0 };
	char filepath[PATH_MAX] = { 0 };
	char *resolved_path = NULL;
	struct stat st = {0};
	struct passwd *pw = NULL;
	const char *homedir = NULL;

	/* Set default values */
	rcd.storage_path = ".";
	rcd.opmode = RECORDER_LIVE;
	rcd.logrotate_interval_secs = 60 * 60;
	rcd.stereo = 1;
	rcd.headless = 0;
	rcd.sample_rate = 44100;
	rcd.format = RECORDER_FORMAT_OGG_VORBIS;
	rcd.quality = 0.5;
	rcd.comp_level = 0.75;

	/* Grab user arguments */
	while ((opt = getopt(argc, argv, "p:m:t:s:g:r:f:q:c:")) != -1)
		switch (opt) {
		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		case 'p':
			snprintf(filepath, PATH_MAX, "%s", optarg);
			resolved_path = realpath(filepath, resolved_path);
			if (!resolved_path) {
				fprintf(stderr,
					"Invalid or inaccessible path: %s\n",
					optarg);
				perror("realpath()");
				ret = -errno;
				goto cleanup;
			} else
				rcd.storage_path = resolved_path;
			break;
		case 'm':
			ret = atoi(optarg);
			if (!(ret & 0x3)) {
				fprintf(stderr, "Invalid operation mode: %s\n",
					optarg);
				ret = -EINVAL;
				goto cleanup;
			} else
				rcd.opmode =
				    (ret ==
				     1) ? RECORDER_LIVE : RECORDER_LOGGER;
			break;
		case 't':
			ret = atoi(optarg);
			if (ret > (24 * 60) || ret < 0) {
				fprintf(stderr, "Invalid time interval: %s\n",
					optarg);
				ret = -EINVAL;
				goto cleanup;
			} else
				rcd.logrotate_interval_secs = ret * 60;
			break;
		case 's':
			ret = atoi(optarg);
			if (ret > 1 || ret < 0) {
				fprintf(stderr,
					"Invalid value for stereo setting: %s\n",
					optarg);
				ret = -EINVAL;
				goto cleanup;
			} else
				rcd.stereo = ret;
			break;
		case 'g':
			ret = atoi(optarg);
			if (ret > 1 || ret < 0) {
				fprintf(stderr,
					"Invalid value for GUI setting: %s\n",
					optarg);
				ret = -EINVAL;
				goto cleanup;
			} else
				rcd.headless = ret == 0 ? 1 : 0;
			break;
		case 'r':
			ret = atoi(optarg);
			/* XXX: More checks ? */
			if (ret < 0) {
				fprintf(stderr, "Invalid sample rate: %s\n",
					optarg);
				ret = -EINVAL;
				goto cleanup;
			} else
				rcd.sample_rate = ret;
			break;
		case 'f':
			ret = atoi(optarg);
			if (!(ret & 0x3)) {
				fprintf(stderr, "Invalid format: %s\n", optarg);
				ret = -EINVAL;
				goto cleanup;
			} else
				rcd.format =
				    (ret ==
				     1) ? RECORDER_FORMAT_FLAC :
				    RECORDER_FORMAT_OGG_VORBIS;
			break;
		case 'q':
			tmp = atof(optarg);
			if (tmp > 1.0 || tmp < 0.0) {
				fprintf(stderr,
					"Invalid encoding quality: %s\n",
					optarg);
				ret = -EINVAL;
				goto cleanup;
			} else
				rcd.quality = tmp;
			break;
		case 'c':
			tmp = atof(optarg);
			if (tmp > 1.0 || tmp < 0.0) {
				fprintf(stderr,
					"Invalid compression level: %s\n",
					optarg);
				ret = -EINVAL;
				goto cleanup;
			} else
				rcd.comp_level = tmp;
			break;
		default:	/* '?' */
			usage(argv[0]);
			ret = -EINVAL;
			goto cleanup;
		}

	/* Case user inputs non-option args only */
	if (argc > 1 && optind == 1) {
		usage(argv[0]);
		exit(-EINVAL);
	}

	/* Create default output directory if it doesn't exist
	 * and if output directory is not set.
	 * Use ~/Recordings for recordings and ~/AudioLogs
	 * for audio logs */
	if(!resolved_path) {
		pw = getpwuid(getuid());
		if(!pw) {
			fprintf(stderr, "Unable to get home directory\n");
			perror("getpuid()");
			ret = -errno;
			goto cleanup;
		}
		homedir = pw->pw_dir;
		if (rcd.opmode == RECORDER_LOGGER)
			snprintf(filepath, PATH_MAX, "%s/%s", homedir, "AudioLogs");
		else
			snprintf(filepath, PATH_MAX, "%s/%s", homedir, "Recordings");

		/* Cleanup pwd structure asap as it contains user private infos */
		memset(pw, 0, sizeof(pw));

		if (stat(filepath, &st) == -1) {
			ret = mkdir(filepath, 0700);
			if (ret < 0) {
				fprintf(stderr, "Unable to create output directory %s\n",
					filepath);
				perror("mkdir()");
				ret = -errno;
				goto cleanup;
			}
		}

		resolved_path = realpath(filepath, resolved_path);
		if (!resolved_path) {
			fprintf(stderr,
				"Invalid or inaccessible path: %s\n",
				filepath);
			perror("realpath()");
			ret = -errno;
			goto cleanup;
		} else
			rcd.storage_path = resolved_path;
	}

	/* Initialize the recorder */
	ret = recorder_initialize(&rcd);
	if (ret < 0)
		goto cleanup;

	if (!rcd.headless)
		ret = gui_initialize(argc, argv, &rcd);
	else {
		/* Logger starts on recorder_initialize
		 * with or without gui */
		if(rcd.opmode != RECORDER_LOGGER) {
			ret = recorder_start(&rcd);
			if (ret < 0)
				goto cleanup;
		}

		while (recorder_state)
			sleep(1);
	}

 cleanup:
	if (recorder_state)
		recorder_cleanup(&rcd);
	if(resolved_path)
		free(resolved_path);
	return ret;
}
