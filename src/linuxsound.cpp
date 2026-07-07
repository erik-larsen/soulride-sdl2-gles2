/*
    Copyright 2000, 2001, 2002, 2003 Slingshot Game Technology, Inc.

    This file is part of The Soul Ride Engine, see http://soulride.com

    The Soul Ride Engine is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2 of
    the License, or (at your option) any later version.

    The Soul Ride Engine is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
// linuxsound.hpp	-thatcher 1/28/2001 Copyright Thatcher Ulrich

// Interface code to Linux sound stuff.  Implements the interface defined by the Sound namespace.


#include <SDL.h>
#include <SDL_mixer.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#ifdef MACOSX
#include "macosxworkaround.hpp"
#endif



#include "utility.hpp"
#include "sound.hpp"
#include "config.hpp"
#include "console.hpp"
#include "gg_audio.h"	// for GG_SoundBuffer (movie sound effects)


namespace Sound {


bool	IsOpen = false;


unsigned long	OutputFrequency = 22050;	// Samples/sec of the output channel.



// cd interface.
namespace cd {
	void	open();
	void	play(int track);
	void	pause();
	void	stop();
	void	close();
	CDMode	get_mode();
	int	track_count();
	void	get_drive_name(char* result);
};


// mixer interface.
void	mixer_open();
void	mixer_close();
void	mixer_setcdvolume(uint8 vol);
void	mixer_setwavevolume(uint8 vol);
void	mixer_setmastervolume(uint8 vol);


BagOf<Mix_Chunk*>	Buffers;

	
void	Open()
// Open the Sound:: interface.
{
	if (Config::GetBoolValue("Sound") == false) return;

	int	result;
	result = Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 1024);
	if (result < 0) {
		printf("Mix_OpenAudio returned %d, %s\n", result, SDL_GetError());
		fflush(stdout);
		return;
	}
	Console::Printf("Sound::Open: audio device opened.\n");
	
	mixer_open();
	cd::open();
	
	IsOpen = true;

	// Set initial volume levels if they're specified.
	if (Config::GetValue("MasterVolume")) SetMasterVolume(uint8(Config::GetFloat("MasterVolume") * 255.0f / 100.0f));
	if (Config::GetValue("SFXVolume")) SetSFXVolume(uint8(Config::GetFloat("SFXVolume") * 255.0f / 100.0f));
}


void	Close()
// Close the Sound:: interface.
{
	if (!IsOpen) return;
	
	cd::stop();
	cd::close();
	mixer_close();
	
	Mix_CloseAudio();

	IsOpen = false;
}


void	SetMasterVolume(uint8 vol)
// Sets the master volume level.
{
	mixer_setmastervolume(vol);
}


void	SetSFXVolume(uint8 vol)
// Sets the sound effects volume level.
{
	mixer_setwavevolume(vol);
//	alListenerf(AL_GAIN, 0.25 / 255.0 * vol);
}


// Keep a list of sources which are currently active.  Periodically
// check for played-out sources, and delete them.  A played-out sound
// is a non-looped sound which has reached the end of its buffer.
const int LIVE_SOURCE_CT = 100;
int	LiveSources[LIVE_SOURCE_CT];
int	NextSourceIndex = 0;


// Natural base of logarithms.
#define	LN_BASE	2.718281828

int	sdl_volume(float vol)
// Given a volume level from 0 to 1, returns an SDL_mixer volume level
// from 0 to 127.  Un-does the odious logarithmic mapping.
{
	if (vol < 0.001) {
		return 0;
	} else {
		return int(((exp(vol) - 1) / (LN_BASE - 1)) * 127);
	}
}


/**
 * Plays the sound specified by the given resource name.
 *
 * @param ResourceName Filename
 * @param Parameters   Controls to specific parameters
 *
 * @return An "event id" for the sound event started by this call. This id can
 * be used later in calls to Sound::Adjust() or Sound::Release() to change the
 * sound's parameters.
 * @return 0 on failure.
 */
int
Play(const char* ResourceName, const Controls& Parameters)
{
	if (!IsOpen) return 0;

	Mix_Chunk*	chunk;
	bool	got_it = Buffers.GetObject(&chunk, ResourceName);
	if (!got_it) {
		// Need to load and initialize the resource.
		chunk = Mix_LoadWAV(ResourceName);
		if (chunk == NULL) {
			Console::Printf("Sound::Play: can't load '%s': %s\n", ResourceName, SDL_GetError());
		}

		// Remember this chunk -- also on failure (NULL), so we
		// don't hit the disk again on every retry.
		Buffers.Add(chunk, ResourceName);
	}
	if (chunk == NULL) return 0;

	int	channel = Mix_PlayChannel(-1, chunk, Parameters.Looped ? -1 : 0);
	if (channel >= 0) {
		Mix_Volume(channel, sdl_volume(Parameters.Volume));
		// pan
		// pitch
	}

	return channel + 1;
}


void	Adjust(int EventID, const Controls& NewParameters)
// Use this call to change the parameters of a sound that's currently playing.
{
	if (!IsOpen) return;
	if (EventID == 0) return;

	int	channel = EventID - 1;
	Mix_Volume(channel, sdl_volume(NewParameters.Volume));

//	//xxxxxxxx
//	float	newvol;
//	alGetSourcefv(EventID, AL_GAIN_LINEAR_LOKI, &newvol);
//	printf("new vol = %f\n", newvol);//xxxxxxxx
}


void	Release(int EventID)
// Use this call to stop a playing sound, and release its channel.
{
	if (!IsOpen) return;
	if (EventID == 0) return;

	int	channel = EventID - 1;
	Mix_HaltChannel(channel);
}


int	GetStatus(int EventID)
// Returns status information about the specified sound event.
{
	if (!IsOpen) return 0;
	if (EventID == 0) return 0;

	int	channel = EventID - 1;
	if (Mix_Playing(channel)) return 1;
	return 0;
}


CDMode	GetCDMode()
// Returns the current mode of the cd player.
{
	return cd::get_mode();
}


int	GetCDTrackCount()
// Returns the number of available audio tracks on the current cd.
{
	return cd::track_count();
}

void	GetCDDriveName(char* result)
// Returns the drive name of the cd drive.
{
	cd::get_drive_name(result);
}


void	PlayCDTrack(int TrackID)
// Plays the specified cd-audio track.  Track numbering starts with 1.
{
	cd::play(TrackID);
}


void	SetMusicVolume(uint8 vol)
// Sets the volume control of the CD-Audio output.
{
	mixer_setcdvolume(vol);
}



#if 0


//
// some utility functions for loading .WAV files.  Stolen from MS DirectX samples.
//


int WaveOpenFile(
	char *pszFileName,                              // (IN)
	HMMIO *phmmioIn,                                // (OUT)
	WAVEFORMATEX **ppwfxInfo,                       // (OUT)
	MMCKINFO *pckInRIFF                             // (OUT)
			)
/* This function will open a wave input file and prepare it for reading,
 * so the data can be easily
 * read with WaveReadFile. Returns 0 if successful, the error code if not.
 *      pszFileName - Input filename to load.
 *      phmmioIn    - Pointer to handle which will be used
 *          for further mmio routines.
 *      ppwfxInfo   - Ptr to ptr to WaveFormatEx structure
 *          with all info about the file.                        
 *      
*/
{
	HMMIO           hmmioIn;
	MMCKINFO        ckIn;           // chunk info. for general use.
	PCMWAVEFORMAT   pcmWaveFormat;  // Temp PCM structure to load in.       
	WORD            cbExtraAlloc;   // Extra bytes for waveformatex 
	int             nError;         // Return value.


	// Initialization...
	*ppwfxInfo = NULL;
	nError = 0;
	hmmioIn = NULL;
	
	if ((hmmioIn = mmioOpen(pszFileName, NULL, MMIO_ALLOCBUF | MMIO_READ)) == NULL)
		{
		nError = -1 /* ER_CANNOTOPEN */;
		goto ERROR_READING_WAVE;
		}

	if ((nError = (int)mmioDescend(hmmioIn, pckInRIFF, NULL, 0)) != 0)
		{
		goto ERROR_READING_WAVE;
		}


	if ((pckInRIFF->ckid != FOURCC_RIFF) || (pckInRIFF->fccType != mmioFOURCC('W', 'A', 'V', 'E')))
		{
		nError = -1 /* ER_NOTWAVEFILE */;
		goto ERROR_READING_WAVE;
		}
			
	/* Search the input file for for the 'fmt ' chunk.     */
    ckIn.ckid = mmioFOURCC('f', 'm', 't', ' ');
    if ((nError = (int)mmioDescend(hmmioIn, &ckIn, pckInRIFF, MMIO_FINDCHUNK)) != 0)
		{
		goto ERROR_READING_WAVE;                
		}
					
	/* Expect the 'fmt' chunk to be at least as large as <PCMWAVEFORMAT>;
    * if there are extra parameters at the end, we'll ignore them */
    
    if (ckIn.cksize < (long) sizeof(PCMWAVEFORMAT))
		{
		nError = -1 /* ER_NOTWAVEFILE */;
		goto ERROR_READING_WAVE;
		}
															
	/* Read the 'fmt ' chunk into <pcmWaveFormat>.*/     
    if (mmioRead(hmmioIn, (HPSTR) &pcmWaveFormat, (long) sizeof(pcmWaveFormat)) != (long) sizeof(pcmWaveFormat))
		{
		nError = -1 /* ER_CANNOTREAD */;
		goto ERROR_READING_WAVE;
		}
							

	// Ok, allocate the waveformatex, but if its not pcm
	// format, read the next word, and thats how many extra
	// bytes to allocate.
	if (pcmWaveFormat.wf.wFormatTag == WAVE_FORMAT_PCM)
		cbExtraAlloc = 0;                               
							
	else
		{
		// Read in length of extra bytes.
		if (mmioRead(hmmioIn, (LPSTR) &cbExtraAlloc,
			(long) sizeof(cbExtraAlloc)) != (long) sizeof(cbExtraAlloc))
			{
			nError = -1 /* ER_CANNOTREAD */;
			goto ERROR_READING_WAVE;
			}

		}
							
	// Ok, now allocate that waveformatex structure.
	if ((*ppwfxInfo = (struct tWAVEFORMATEX*) GlobalAlloc(GMEM_FIXED, sizeof(WAVEFORMATEX)+cbExtraAlloc)) == NULL)
		{
		nError = -1 /* ER_MEM */;
		goto ERROR_READING_WAVE;
		}

	// Copy the bytes from the pcm structure to the waveformatex structure
	memcpy(*ppwfxInfo, &pcmWaveFormat, sizeof(pcmWaveFormat));
	(*ppwfxInfo)->cbSize = cbExtraAlloc;

	// Now, read those extra bytes into the structure, if cbExtraAlloc != 0.
	if (cbExtraAlloc != 0)
		{
		if (mmioRead(hmmioIn, (LPSTR) (((BYTE*)&((*ppwfxInfo)->cbSize))+sizeof(cbExtraAlloc)),
			(long) (cbExtraAlloc)) != (long) (cbExtraAlloc))
			{
			nError = -1 /* ER_NOTWAVEFILE */;
			goto ERROR_READING_WAVE;
			}
		}

	/* Ascend the input file out of the 'fmt ' chunk. */                                                            
	if ((nError = mmioAscend(hmmioIn, &ckIn, 0)) != 0)
		{
		goto ERROR_READING_WAVE;

		}
	

	goto TEMPCLEANUP;               

ERROR_READING_WAVE:
	if (*ppwfxInfo != NULL)
		{
		GlobalFree(*ppwfxInfo);
		*ppwfxInfo = NULL;
		}               

	if (hmmioIn != NULL)
	{
	mmioClose(hmmioIn, 0);
		hmmioIn = NULL;
		}
	
TEMPCLEANUP:
	*phmmioIn = hmmioIn;

	return(nError);

}

/*      This routine has to be called before WaveReadFile as it searchs for the chunk to descend into for
	reading, that is, the 'data' chunk.  For simplicity, this used to be in the open routine, but was
	taken out and moved to a separate routine so there was more control on the chunks that are before
	the data chunk, such as 'fact', etc... */

int WaveStartDataRead(
					HMMIO *phmmioIn,
					MMCKINFO *pckIn,
					MMCKINFO *pckInRIFF
					)
{
	int                     nError;

	nError = 0;
	
	// Do a nice little seek...
	if ((nError = mmioSeek(*phmmioIn, pckInRIFF->dwDataOffset + sizeof(FOURCC), SEEK_SET)) == -1)
	{
//		Assert(FALSE);
	}

	nError = 0;
	//      Search the input file for for the 'data' chunk.
	pckIn->ckid = mmioFOURCC('d', 'a', 't', 'a');
	if ((nError = mmioDescend(*phmmioIn, pckIn, pckInRIFF, MMIO_FINDCHUNK)) != 0)
		{
		goto ERROR_READING_WAVE;
		}

	goto CLEANUP;

ERROR_READING_WAVE:

CLEANUP:        
	return(nError);
}


/*      This will read wave data from the wave file.  Makre sure we're descended into
	the data chunk, else this will fail bigtime!
	hmmioIn         - Handle to mmio.
	cbRead          - # of bytes to read.   
	pbDest          - Destination buffer to put bytes.
	cbActualRead- # of bytes actually read.

		

*/


int WaveReadFile(
		HMMIO hmmioIn,                          // IN
		UINT cbRead,                            // IN           
		BYTE *pbDest,                           // IN
		MMCKINFO *pckIn,                        // IN.
		UINT *cbActualRead                      // OUT.
		
		)
{

	MMIOINFO    mmioinfoIn;         // current status of <hmmioIn>
	int                     nError;
	UINT                    cT, cbDataIn, uCopyLength;

	nError = 0;

	if (nError = mmioGetInfo(hmmioIn, &mmioinfoIn, 0) != 0)
		{
		goto ERROR_CANNOT_READ;
		}
				
	cbDataIn = cbRead;
	if (cbDataIn > pckIn->cksize) 
		cbDataIn = pckIn->cksize;       

	pckIn->cksize -= cbDataIn;
	
	for (cT = 0; cT < cbDataIn; )
		{
		/* Copy the bytes from the io to the buffer. */
		if (mmioinfoIn.pchNext == mmioinfoIn.pchEndRead)
			{
	    if ((nError = mmioAdvance(hmmioIn, &mmioinfoIn, MMIO_READ)) != 0)
				{
		goto ERROR_CANNOT_READ;
				} 
	    if (mmioinfoIn.pchNext == mmioinfoIn.pchEndRead)
				{
				nError = -1 /* ER_CORRUPTWAVEFILE */;
		goto ERROR_CANNOT_READ;
				}
			}
			
		// Actual copy.
		uCopyLength = (UINT)(mmioinfoIn.pchEndRead - mmioinfoIn.pchNext);
		if((cbDataIn - cT) < uCopyLength )
			uCopyLength = cbDataIn - cT;
		memcpy( (BYTE*)(pbDest+cT), (BYTE*)mmioinfoIn.pchNext, uCopyLength );
		cT += uCopyLength;
		mmioinfoIn.pchNext += uCopyLength;
		}

	if ((nError = mmioSetInfo(hmmioIn, &mmioinfoIn, 0)) != 0)
		{
		goto ERROR_CANNOT_READ;
		}

	*cbActualRead = cbDataIn;
	goto FINISHED_READING;

ERROR_CANNOT_READ:
	*cbActualRead = 0;

FINISHED_READING:
	return(nError);

}

/*      This will close the wave file openned with WaveOpenFile.  
	phmmioIn - Pointer to the handle to input MMIO.
	ppwfxSrc - Pointer to pointer to WaveFormatEx structure.

	Returns 0 if successful, non-zero if there was a warning.

*/
int WaveCloseReadFile(
			HMMIO *phmmio,                                  // IN
			WAVEFORMATEX **ppwfxSrc                 // IN
			)
{

	if (*ppwfxSrc != NULL)
		{
		GlobalFree(*ppwfxSrc);
		*ppwfxSrc = NULL;
		}

	if (*phmmio != NULL)
		{
		mmioClose(*phmmio, 0);
		*phmmio = NULL;
		}

	return(0);

}


#endif // 0


//
// cd stuff
//


// SDL2 dropped the SDL 1.2 CD-audio API.  Instead, emulate the music
// "CD" with files (.ogg/.mp3/.wav/.flac) from the directory named by
// the MusicPath config value (default "music", relative to the data
// directory, which is the game's working directory).  Track ordering
// is alphabetical; the Music:: module shuffles on top of that, just
// like it shuffled CD tracks.
namespace cd {
;

bool	MusicIsOpen = false;

const int	MAX_MUSIC_TRACKS = 100;
char*	MusicFile[MAX_MUSIC_TRACKS];
int	MusicFileCount = 0;
char	MusicDir[300] = "music";

Mix_Music*	CurrentMusic = NULL;
bool	MusicPaused = false;


static bool	has_music_extension(const char* name)
// Returns true if the filename looks like a playable music file.
{
	const char*	dot = strrchr(name, '.');
	if (dot == NULL) return false;
	return strcasecmp(dot, ".ogg") == 0
		|| strcasecmp(dot, ".mp3") == 0
		|| strcasecmp(dot, ".wav") == 0
		|| strcasecmp(dot, ".flac") == 0;
}


static int	compare_names(const void* a, const void* b)
{
	return strcasecmp(*(const char* const*) a, *(const char* const*) b);
}


void	open()
// Scan the music directory for playable tracks.
{
	if (MusicIsOpen) return;

	const char*	dirname = Config::GetValue("MusicPath");
	if (dirname) {
		strncpy(MusicDir, dirname, sizeof(MusicDir) - 2);
		MusicDir[sizeof(MusicDir) - 2] = 0;
	}

	DIR*	dir = opendir(MusicDir);
	if (dir == NULL) return;

	struct dirent*	ent;
	while ((ent = readdir(dir)) && MusicFileCount < MAX_MUSIC_TRACKS) {
		if (has_music_extension(ent->d_name)) {
			MusicFile[MusicFileCount] = strdup(ent->d_name);
			MusicFileCount++;
		}
	}
	closedir(dir);

	if (MusicFileCount == 0) return;

	// Alphabetical order, so track numbers are stable.
	qsort(MusicFile, MusicFileCount, sizeof(MusicFile[0]), compare_names);

	Console::Printf("Music: %d track(s) in '%s'\n", MusicFileCount, MusicDir);

	MusicIsOpen = true;
}


void	play(int track)
// Play the specified track.  Track numbering starts at 1.
{
	if (MusicIsOpen == false) return;
	if (track < 1 || track > MusicFileCount) return;

	if (CurrentMusic) {
		Mix_HaltMusic();
		Mix_FreeMusic(CurrentMusic);
		CurrentMusic = NULL;
	}

	char	path[600];
	snprintf(path, sizeof(path), "%s%s%s", MusicDir, PATH_SEPARATOR, MusicFile[track - 1]);

	CurrentMusic = Mix_LoadMUS(path);
	if (CurrentMusic == NULL) {
		Console::Printf("Music: can't load '%s': %s\n", path, SDL_GetError());
		return;
	}

	Mix_PlayMusic(CurrentMusic, 1);
	MusicPaused = false;
	Console::Printf("Music: playing '%s'\n", path);
}


void	pause()
// Pause music playback.
{
	if (MusicIsOpen == false) return;
	Mix_PauseMusic();
	MusicPaused = true;
}


void	stop()
// Stop music playback.
{
	if (MusicIsOpen == false) return;
	Mix_HaltMusic();
	MusicPaused = false;
}


void	close()
// Close the music interface.
{
	if (!MusicIsOpen) return;

	stop();
	if (CurrentMusic) {
		Mix_FreeMusic(CurrentMusic);
		CurrentMusic = NULL;
	}

	int	i;
	for (i = 0; i < MusicFileCount; i++) {
		free(MusicFile[i]);
		MusicFile[i] = NULL;
	}
	MusicFileCount = 0;

	MusicIsOpen = false;
}


CDMode	get_mode()
// Returns the current mode (i.e. CD_NOT_READY, CD_PLAY, CD_STOP, CD_PAUSE, etc).
{
	if (MusicIsOpen == false) return CD_NOT_READY;

	if (MusicPaused) return CD_PAUSE;
	if (Mix_PlayingMusic()) return CD_PLAY;
	return CD_STOP;
}


int	track_count()
// Returns the number of available music tracks.
{
	if (!MusicIsOpen) return 0;
	return MusicFileCount;
}


void	get_drive_name(char* result)
// Fills result[] with the directory the music comes from (used to
// locate cdaindex.txt with track names).  Trailing separator included.
{
	result[0] = 0;

	if (!MusicIsOpen) return;

	strcpy(result, MusicDir);
	strcat(result, PATH_SEPARATOR);
}


};	// end namespace cd



#ifdef NOT

MCI_STATUS
   MCI_TRACK | (MCI_STATUS_POSITION | MCI_STATUS_LENGTH),
   dwTrack = track number;


MCI_STATUS
   MCI_STATUS_ITEM | MCI_TRACK
   dwTrack = track number;
   dwItem = MCI_CDA_STATUS_TYPE_TRACK,
   mci sets dwReturn to (MCI_CDA_TRACK_AUDIO | MCI_CDA_TRACK_OTHER);

   
   MCI_STATUS_MEDIA_PRESENT
   dwReturn <-- TRUE | FALSE


   MCI_STATUS_NUMBER_OF_TRACKS 
   dwReturn <-- total number of playable tracks


   MCI_STATUS_MODE 
   dwReturn <--
	   MCI_MODE_NOT_READY
	   MCI_MODE_PAUSE
	   MCI_MODE_PLAY
	   MCI_MODE_STOP
	   MCI_MODE_OPEN
	   MCI_MODE_RECORD
	   MCI_MODE_SEEK 


BOOL mciGetErrorString( DWORD fdwError, 
LPTSTR lpszErrorText, 
UINT cchErrorText 
); 
 

BYTE MCI_TMSF_FRAME(DWORD dwTMSF); // _MINUTE, _SECOND, _TRACK
 
DWORD MCI_MAKE_TMSF(BYTE tracks, BYTE minutes, BYTE seconds, BYTE frames); 
 


#endif // NOT


// Volume control goes through SDL2_mixer rather than the old OSS
// /dev/mixer hardware interface (which also messed with system-wide
// volume settings).


void	mixer_open()
// Open the mixer interface.
{
}


void	mixer_close()
{
}


void	mixer_setcdvolume(uint8 vol)
// Sets the music playback volume.  The value goes from 0 (softest) to
// 255 (loudest).
{
	Mix_VolumeMusic((vol * MIX_MAX_VOLUME) / 255);
}


void	mixer_setwavevolume(uint8 vol)
// Sets the volume on the WAVE audio lines to the specified value.  The value
// goes from 0 (softest) to 255 (loudest).
{
	Mix_Volume(-1, (vol * MIX_MAX_VOLUME) / 255);
}


void	mixer_setmastervolume(uint8 vol)
// Sets the master speaker volume to the specified value.  The value
// goes from 0 (softest) to 255 (loudest).
{
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
	Mix_MasterVolume((vol * MIX_MAX_VOLUME) / 255);
#endif
}


//
// GameGUI movie sound support.  Movies embed SOUND actors whose files
// (.au or .wav) are loaded through this hook and played via
// SDL2_mixer.
//


static int16	mulaw_decode(uint8 u)
// Decode one 8-bit mu-law sample to linear 16-bit PCM.
{
	u = ~u;
	int	t = ((u & 0x0f) << 3) + 0x84;
	t <<= (u & 0x70) >> 4;
	return (int16) ((u & 0x80) ? (0x84 - t) : (t - 0x84));
}


static uint32	be32(const unsigned char* p)
{
	return ((uint32) p[0] << 24) | ((uint32) p[1] << 16) | ((uint32) p[2] << 8) | (uint32) p[3];
}


static Uint8*	load_au_pcm(const char* filename, SDL_AudioFormat* fmt, int* channels, int* rate, uint32* bytes)
// Loads a Sun .au file and returns malloc'd PCM data (mu-law decoded
// to S16).  Returns NULL on failure.
{
	FILE*	fp = fopen(filename, "rb");
	if (fp == NULL) return NULL;

	unsigned char	hdr[24];
	if (fread(hdr, 1, 24, fp) != 24 || memcmp(hdr, ".snd", 4) != 0) {
		fclose(fp);
		return NULL;
	}

	uint32	data_location = be32(hdr + 4);
	uint32	data_size = be32(hdr + 8);
	uint32	data_format = be32(hdr + 12);
	uint32	sampling_rate = be32(hdr + 16);
	uint32	channel_count = be32(hdr + 20);

	// Unknown size: use the rest of the file.
	fseek(fp, 0, SEEK_END);
	uint32	file_size = (uint32) ftell(fp);
	if (data_size == 0xFFFFFFFF || data_location + data_size > file_size) {
		data_size = file_size - data_location;
	}

	fseek(fp, data_location, SEEK_SET);
	unsigned char*	raw = (unsigned char*) malloc(data_size);
	if (raw == NULL || fread(raw, 1, data_size, fp) != data_size) {
		free(raw);
		fclose(fp);
		return NULL;
	}
	fclose(fp);

	*channels = (int) channel_count;
	*rate = (int) sampling_rate;

	switch (data_format) {
	case 1: {	// 8-bit mu-law
		int16*	pcm = (int16*) malloc(data_size * 2);
		if (pcm == NULL) { free(raw); return NULL; }
		uint32	i;
		for (i = 0; i < data_size; i++) {
			pcm[i] = mulaw_decode(raw[i]);
		}
		free(raw);
		*fmt = AUDIO_S16SYS;
		*bytes = data_size * 2;
		return (Uint8*) pcm;
	}
	case 2:		// 8-bit linear (signed)
		*fmt = AUDIO_S8;
		*bytes = data_size;
		return raw;
	case 3:		// 16-bit linear, big-endian
		*fmt = AUDIO_S16MSB;
		*bytes = data_size;
		return raw;
	default:
		Console::Printf("Sound: unsupported .au format %d in '%s'\n", data_format, filename);
		free(raw);
		return NULL;
	}
}


// An SDL2_mixer-backed implementation of the GameGUI sound interface.
class SDLGGSoundBuffer : public GG_SoundBuffer {
public:
	Mix_Chunk*	Chunk;
	Uint8*	Data;	// backing store for Mix_QuickLoad_RAW chunks (NULL for Mix_LoadWAV)
	int	Channel;

	SDLGGSoundBuffer(Mix_Chunk* chunk, Uint8* data)
	{
		Chunk = chunk;
		Data = data;
		Channel = -1;
	}

	~SDLGGSoundBuffer()
	{
		stop();
		if (Chunk) Mix_FreeChunk(Chunk);
		free(Data);
	}

	void	play(bool bLoop)
	{
		if (Chunk == NULL) return;
		Channel = Mix_PlayChannel(-1, Chunk, bLoop ? -1 : 0);
	}

	void	stop()
	{
		// Only halt if our chunk still owns the channel.
		if (Channel >= 0 && Mix_GetChunk(Channel) == Chunk) {
			Mix_HaltChannel(Channel);
		}
		Channel = -1;
	}

	void	setVolume(float vol)
	{
		if (Chunk == NULL) return;
		if (vol < 0) vol = 0;
		if (vol > 1) vol = 1;
		Mix_VolumeChunk(Chunk, (int) (vol * MIX_MAX_VOLUME));
	}
};


GG_SoundBuffer*	NewGGSoundBuffer(const char* filename)
// Loads a movie sound file (.au or .wav).  Returns NULL if sound is
// closed or the file can't be loaded.
{
	if (!IsOpen) return NULL;

	const char*	dot = strrchr(filename, '.');
	if (dot && strcasecmp(dot, ".au") == 0) {
		SDL_AudioFormat	src_fmt;
		int	src_channels, src_rate;
		uint32	src_bytes;
		Uint8*	pcm = load_au_pcm(filename, &src_fmt, &src_channels, &src_rate, &src_bytes);
		if (pcm == NULL) {
			Console::Printf("Sound: can't load '%s'\n", filename);
			return NULL;
		}

		// Convert to the mixer's output format.
		int	dst_rate, dst_channels;
		Uint16	dst_fmt;
		Mix_QuerySpec(&dst_rate, &dst_fmt, &dst_channels);

		SDL_AudioCVT	cvt;
		int	need = SDL_BuildAudioCVT(&cvt, src_fmt, src_channels, src_rate,
						 dst_fmt, dst_channels, dst_rate);
		if (need < 0) {
			Console::Printf("Sound: can't convert '%s' to output format\n", filename);
			free(pcm);
			return NULL;
		}

		Uint8*	data;
		uint32	data_len;
		if (need == 0) {
			data = pcm;
			data_len = src_bytes;
		} else {
			data = (Uint8*) malloc(src_bytes * cvt.len_mult);
			if (data == NULL) { free(pcm); return NULL; }
			memcpy(data, pcm, src_bytes);
			free(pcm);
			cvt.buf = data;
			cvt.len = (int) src_bytes;
			if (SDL_ConvertAudio(&cvt) != 0) {
				Console::Printf("Sound: conversion failed for '%s'\n", filename);
				free(data);
				return NULL;
			}
			data_len = (uint32) cvt.len_cvt;
		}

		Mix_Chunk*	chunk = Mix_QuickLoad_RAW(data, data_len);
		if (chunk == NULL) {
			free(data);
			return NULL;
		}
		Console::Printf("Sound: loaded movie sound '%s'\n", filename);
		return new SDLGGSoundBuffer(chunk, data);
	}

	// Anything else: let SDL2_mixer figure it out (.wav etc).
	Mix_Chunk*	chunk = Mix_LoadWAV(filename);
	if (chunk == NULL) {
		Console::Printf("Sound: can't load '%s': %s\n", filename, SDL_GetError());
		return NULL;
	}
	return new SDLGGSoundBuffer(chunk, NULL);
}


};	// End namespace Sound

