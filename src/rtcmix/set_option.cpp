/* RTcmix - Copyright (C) 2000 The RTcmix Development Team
   See ``AUTHORS'' for a list of contributors. See ``LICENSE'' for
   the license to this software and for a DISCLAIMER OF ALL WARRANTIES.
*/
/* The set_option function, called from a script, lets the user override
   default options (and those stored in the .rtcmixrc file).  The options
   are kept in the <options> object (see Option.h).      -JGG, 6/30/04
*/
#include <globals.h>	// for options object
#include <ugens.h>		// for die()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <Option.h>

typedef enum {
	DEVICE,
	INDEVICE,
	OUTDEVICE,
	AUDIO,
	RECORD,
	CLOBBER,
	REPORT_CLIPPING,
	CHECK_PEAKS,
	FULL_DUPLEX,
} ParamType;

#define OPT_STRLEN 128

typedef struct {
	char arg[OPT_STRLEN];	 
	ParamType type;
	bool value;		// use false if not relevant, i.e. for key=value style
} Param;

static Param param_list[] = {
	{ "DEVICE", DEVICE, false},
	{ "INDEVICE", INDEVICE, false},
	{ "OUTDEVICE", OUTDEVICE, false},
	{ "AUDIO_ON", AUDIO, true},
	{ "AUDIO_OFF", AUDIO, false},
	{ "RECORD_ON", RECORD, true},
	{ "RECORD_OFF", RECORD, false},
	{ "PLAY_ON", AUDIO, true},
	{ "PLAY_OFF", AUDIO, false},
	{ "CLOBBER_ON", CLOBBER, true},
	{ "CLOBBER_OFF", CLOBBER, false},
	{ "REPORT_CLIPPING_ON", REPORT_CLIPPING, true},
	{ "REPORT_CLIPPING_OFF", REPORT_CLIPPING, false},
	{ "CHECK_PEAKS_ON", CHECK_PEAKS, true},
	{ "CHECK_PEAKS_OFF", CHECK_PEAKS, false},
	{ "FULL_DUPLEX_ON", FULL_DUPLEX, true},
	{ "FULL_DUPLEX_OFF", FULL_DUPLEX, false},
};
static int num_params = sizeof(param_list) / sizeof(Param);


extern "C" {

double set_option(float *p, short nargs, double pp[])
{
	int  j;
	char opt[OPT_STRLEN];

	for (int i = 0; i < nargs; i++) {
		int matched;
		int space_state;

		char *p, *arg = (char *) ((int) pp[i]);		// cast pfield to string

		// Strip white-space chars from text to the left of any '=' and between
		// the '=' and the next non-white space.  (The reason is to preserve
		// options that must include spaces, such as "MOTU 828".)  Store result
		// into <opt>.
		int len = arg ? strlen(arg) : 0;
		if (len > OPT_STRLEN - 1)
			len = OPT_STRLEN - 1;
		space_state = 0;
		for (j = 0, p = opt; j < len; j++) {
			if (space_state > 1)
				*p++ = arg[j];
			else if (!isspace(arg[j])) {
				if (space_state == 1)
					space_state++;
				else if (arg[j] == '=')
					space_state = 1;
				*p++ = arg[j];
			}
		}
		*p = '\0';

		// Two styles of option string: a single "value" and a "key=value" pair.
		matched = 0;
		p = strchr(opt, '=');					// check for "key=value"
		if (p) {
			*p++ = '\0';						// <opt> is now key only
			if (*p == '\0') {
				 die("set_option", "Missing value for key \"%s\"", opt);
				 return -1.0;
			}
			// p now points to value string
			for (j = 0; j < num_params; j++) {
				if (strcasecmp(param_list[j].arg, opt) == 0) {
					matched = 1;
					break;
				}
			}
		}
		else {									// check for single "value"
			 for (j = 0; j < num_params; j++) {
				 if (strcasecmp(param_list[j].arg, opt) == 0) {
					 matched = 1;
					 break;
				 }
			 }
		}
		if (!matched) {
			die("set_option", "Unrecognized argument \"%s\"", opt);
			return -1.0;
		}
		
		switch (param_list[j].type) {
		case DEVICE:
			if (p == NULL) {
				 die("set_option", "No value for \"device\"");
				 return -1.0;
			}
			options.device(p);
			break;
		case INDEVICE:
			if (p == NULL) {
				 die("set_option", "No value for \"indevice\"");
				 return -1.0;
			}
			options.inDevice(p);
			break;
		case OUTDEVICE:
			if (p == NULL) {
				 die("set_option", "No value for \"outdevice\"");
				 return -1.0;
			}
			options.outDevice(p);
			break;
		case AUDIO:
			options.play(param_list[j].value);
			break;
		case RECORD:
			options.record(param_list[j].value);
			break;
		case CLOBBER:
			options.clobber(param_list[j].value);
			break;
		case REPORT_CLIPPING:
			options.reportClipping(param_list[j].value);
			break;
		case CHECK_PEAKS:
			options.checkPeaks(param_list[j].value);
			break;
		case FULL_DUPLEX:
			bool full_duplex = param_list[j].value;
			if (full_duplex && rtsetparams_called) {
				die("set_option", "Turn on full duplex BEFORE calling rtsetparams.");
				return -1.0;
			}
			// The full duplex state has now been broken up into the <play> and
			// <record> options, used during audio setup. rtsetparams() checks
			// <record>.
			if (full_duplex)
				options.record(true);
			else {
				// If not play, then record.
				bool state = options.record() && !options.play();
				options.record(state);
			}
			// Same check as above, for record.
			if (options.record() && rtsetparams_called) {
				die("set_option", "Turn on record BEFORE calling rtsetparams.");
				return -1.0;
			}
			break;
		}
	}
	return 0.0;
}

} // extern "C"
