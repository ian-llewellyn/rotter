/*

	twolame.c
	Recording of Transmission / Audio Logger
	Copyright (C) 2006  Nicholas J. Humfrey
	
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
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <getopt.h>
#include <errno.h>
#include <stdarg.h>
#include <twolame.h>

#include "rotter.h"
#include "config.h"



// ------ Globals ---------
twolame_options *twolame_opts = NULL;
jack_default_audio_sample_t *pcm_buffer[2]= {NULL,NULL};
unsigned char *mpeg_buffer=NULL;
FILE* output_file = NULL;


#define SAMPLES_PER_FRAME 		(1152)


// Encode a frame of audio
static int encode()
{
	jack_nframes_t samples = SAMPLES_PER_FRAME;
	size_t desired = samples * sizeof( jack_default_audio_sample_t );
	int channels = twolame_get_num_channels(twolame_opts);
	int bytes_read=0, bytes_encoded=0, bytes_written=0;
	int c=0;
	
	// Check that the output file is open
	if (output_file==NULL) {
		rotter_error( "Warning: output file isn't open, while trying to encode.");
		// Try again later
		return 0;
	
	}
	
	// Is the enough in the ring buffer?
	if (jack_ringbuffer_read_space( ringbuffer[0] ) < desired) {
		// Try again later
		return 0;
	}
	

	// Take audio out of the ring buffer
    for (c=0; c<channels; c++)
    {    
 		// Ensure the temporary buffer is big enough
		pcm_buffer[c] = (jack_default_audio_sample_t*)realloc(pcm_buffer[c], desired );
		if (!pcm_buffer[c]) rotter_fatal( "realloc on tmp_buffer failed" );

		// Copy frames from ring buffer to temporary buffer
        bytes_read = jack_ringbuffer_read( ringbuffer[c], (char*)pcm_buffer[c], desired);
		if (bytes_read != desired) {
			rotter_fatal( "Failed to read desired number of bytes from ringbuffer." );
		}
    }


	// Encode it
	bytes_encoded = twolame_encode_buffer_float32( twolame_opts, 
						pcm_buffer[0], pcm_buffer[1],
						samples, mpeg_buffer, WRITE_BUFFER_SIZE );
	if (bytes_encoded<=0) {
		rotter_error( "Warning: failed to encode any audio.");
		return -1;
	}
	
	
	// Write it to disk
	bytes_written = fwrite( mpeg_buffer, 1, bytes_encoded, output_file);
	if (bytes_written != bytes_encoded) {
		rotter_error( "Warning: failed to write encoded audio to disk.");
		return -1;
	}
	

	// Success
	return bytes_written;

}



static int close_file()
{
	if (output_file==NULL) return -1;
	
	// Write ID3v1 tags
	write_id3v1( output_file );


	rotter_debug("Closing MP2 output file.");

	if (fclose(output_file)) {
		rotter_error( "Failed to close output file: %s", strerror(errno) );
		return -1;
	}
	
	// File is now closed
	output_file=NULL;

	// Success
	return 0;
}


static int open_file( char* filepath )
{

	rotter_debug("Opening MP2 output file: %s", filepath);
	if (output_file) {
		rotter_error("Warning: already open while opening output file."); 
		close_file();
	}

	output_file = fopen( filepath, "ab" );
	if (output_file==NULL) {
		rotter_error( "Failed to open output file: %s", strerror(errno) );
		return -1;
	}

	// Success
	return 0;
}


static void shutdown()
{

	rotter_debug("Closing down TwoLAME encoder.");
	twolame_close( &twolame_opts );

	if (pcm_buffer[0]) {
		free(pcm_buffer[0]);
		pcm_buffer[0]=NULL;
	}
	
	if (pcm_buffer[1]) {
		free(pcm_buffer[1]);
		pcm_buffer[1]=NULL;
	}
	
	if (mpeg_buffer) {
		free(mpeg_buffer);
		mpeg_buffer=NULL;
	}
	
	if (output_file) close_file();

}


encoder_funcs_t* init_twolame( int channels, int bitrate )
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

	if ( 0 > twolame_init_params( twolame_opts ) ) {
		rotter_error("TwoLAME error: failed to initialize parameters.");
		return NULL;
    }
    

	rotter_info( "Encoding using libtwolame version %s.", get_twolame_version() );
	rotter_debug( "  Input: %d Hz, %d channels",
						twolame_get_in_samplerate(twolame_opts),
						twolame_get_num_channels(twolame_opts));
	rotter_debug( "  Output: %s Layer 2, %d kbps, %s",
						twolame_get_version_name(twolame_opts),
						twolame_get_bitrate(twolame_opts),
						twolame_get_mode_name(twolame_opts));

	// Allocate memory for encoded audio
	mpeg_buffer = malloc( WRITE_BUFFER_SIZE );
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
	

	funcs->file_suffix = ".mp2";
	funcs->open = open_file;
	funcs->close = close_file;
	funcs->encode = encode;
	funcs->shutdown = shutdown;


	return funcs;
}

