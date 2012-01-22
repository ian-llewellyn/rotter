/*

	twolame.c
	
	rotter: Recording of Transmission / Audio Logger
	Copyright (C) 2006-2009  Nicholas J. Humfrey
	
	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "rotter.h"


#ifdef HAVE_TWOLAME

#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>

#include <twolame.h>



// ------ Globals ---------
static twolame_options *twolame_opts = NULL;
static jack_default_audio_sample_t *pcm_buffer[2]= {NULL,NULL};
static unsigned char *mpeg_buffer=NULL;




/*
	Encode and write some audio from the ring buffer to disk
*/
static int write_twolame()
{
	jack_nframes_t samples = TWOLAME_SAMPLES_PER_FRAME;
	size_t desired = samples * sizeof( jack_default_audio_sample_t );
	int bytes_read=0, bytes_encoded=0, bytes_written=0;
	int c=0;
	
	// Check that the output file is open
	if (mpegaudio_file==NULL) {
		rotter_error( "Warning: output file isn't open, while trying to encode.");
		// Try again later
		return 0;
	
	}
	
	
	// Is there enough in the ring buffers?
	for (c=0; c<channels; c++) {
		if (jack_ringbuffer_read_space( ringbuffer[c] ) < desired) {
			// Try again later
			return 0;
		}
	}

	// Take audio out of the ring buffer
	for (c=0; c<channels; c++) {
		// Ensure the temporary buffer is big enough
		pcm_buffer[c] = (jack_default_audio_sample_t*)realloc(pcm_buffer[c], desired );
		if (!pcm_buffer[c]) rotter_fatal( "realloc on tmp_buffer failed" );

		// Copy frames from ring buffer to temporary buffer
		bytes_read = jack_ringbuffer_read( ringbuffer[c], (char*)pcm_buffer[c], desired);
		if (bytes_read != desired) {
			rotter_fatal( "Failed to read desired number of bytes from ringbuffer %d.", c );
		}
	}


	// Encode it
	bytes_encoded = twolame_encode_buffer_float32( twolame_opts, 
						pcm_buffer[0], pcm_buffer[1],
						samples, mpeg_buffer, WRITE_BUFFER_SIZE );
	if (bytes_encoded<=0) {
		if (bytes_encoded<0)
			rotter_error( "Warning: failed to encode audio: %d", bytes_encoded);
		return bytes_encoded;
	}
	
	
	// Write it to disk
	bytes_written = fwrite( mpeg_buffer, 1, bytes_encoded, mpegaudio_file);
	if (bytes_written != bytes_encoded) {
		rotter_error( "Warning: failed to write encoded audio to disk.");
		return -1;
	}
	

	// Success
	return bytes_written;

}




static void deinit_twolame()
{
	int c;
	rotter_debug("Shutting down TwoLAME encoder.");
	twolame_close( &twolame_opts );

	for( c=0; c<2; c++) {
		if (pcm_buffer[c]) {
			free(pcm_buffer[c]);
			pcm_buffer[c]=NULL;
		}
	}
	
	if (mpeg_buffer) {
		free(mpeg_buffer);
		mpeg_buffer=NULL;
	}
	
}


encoder_funcs_t* init_twolame( const char* format, int channels, int bitrate )
{
	encoder_funcs_t* funcs = NULL;

	twolame_opts = twolame_init();
	if (twolame_opts==NULL) {
		rotter_error("TwoLAME error: failed to initialise.");
		return NULL;
	}
	
	if ( 0 > twolame_set_num_channels( twolame_opts, channels ) ) {
		rotter_error("TwoLAME error: failed to set number of channels.");
		return NULL;
    }

	if ( 0 > twolame_set_in_samplerate( twolame_opts, jack_get_sample_rate( client ) )) {
		rotter_error("TwoLAME error: failed to set input samplerate.");
		return NULL;
    }

	if ( 0 > twolame_set_out_samplerate( twolame_opts, jack_get_sample_rate( client ) )) {
		rotter_error("TwoLAME error: failed to set output samplerate.");
		return NULL;
    }

	if ( 0 > twolame_set_brate( twolame_opts, bitrate) ) {
		rotter_error("TwoLAME error: failed to set bitrate.");
		return NULL;
	}

	if ( 0 > twolame_set_energy_levels( twolame_opts, TRUE) ) {
		rotter_error("TwoLAME error: failed to set energy levels on.");
		return NULL;
	}

	if ( 0 > twolame_init_params( twolame_opts ) ) {
		rotter_error("TwoLAME error: failed to initialize parameters.");
		return NULL;
    }
    

	rotter_debug( "Encoding using libtwolame version %s.", get_twolame_version() );
	rotter_debug( "  Input: %d Hz, %d channels",
						twolame_get_in_samplerate(twolame_opts),
						twolame_get_num_channels(twolame_opts));
	rotter_debug( "  Output: %s Layer 2, %d kbps, %s",
						twolame_get_version_name(twolame_opts),
						twolame_get_bitrate(twolame_opts),
						twolame_get_mode_name(twolame_opts));

	// Allocate memory for encoded audio
	mpeg_buffer = malloc( 1.25*TWOLAME_SAMPLES_PER_FRAME + 7200 );
	if ( mpeg_buffer==NULL ) {
		rotter_error( "Failed to allocate memery for encoded audio." );
		return NULL;
    }

	// Allocate memory for callback functions
	funcs = calloc( 1, sizeof(encoder_funcs_t) );
	if ( funcs==NULL ) {
		rotter_error( "Failed to allocate memery for encoder callback functions structure." );
		return NULL;
    }
	

	funcs->file_suffix = "mp2";
	funcs->open = open_mpegaudio_file;
	funcs->close = close_mpegaudio_file;
	funcs->write = write_twolame;
	funcs->deinit = deinit_twolame;


	return funcs;
}

#endif   // HAVE_TWOLAME

