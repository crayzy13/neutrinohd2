/*
	Neutrino-GUI  -   DBoxII-Project
	
	$Id: audioplay.h 2013/10/12 mohousch Exp $

	Homepage: http://www.dbox2.info/

	Kommentar:

	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#ifndef __AUDIO_PLAY__
#define __AUDIO_PLAY__

#include <unistd.h>
#include <pthread.h>

#include <driver/audiodec/basedec.h>
#include <driver/audiofile.h>
#include <driver/audiometadata.h>
#include <string>

#include <driver/audiodec/netfile.h>

#if 0
typedef struct
{
	void	(*cb)(void *);		/* user provided callback function */
	void	*user;			/* user date hook point */
	int	state;			/* CONNECTING, BUFFERING, RUNNING */
	int	bitrate;
	int	buffered;			/* "waterlevel" in the cache; 0 ... 65535 */
	char 	station_url[1024];	/*station url */
	char	station[1024];		/* station name */
	char	genre[4096];		/* station genre */
	char	artist[4096];		/* artist currently playing */
	char	title[4096];			/* title currently playing */
} CSTATE;
#endif

class CAudioPlayer
{
	private:		
		time_t m_played_time;	
		int  m_sc_buffered;
		pthread_t	thrPlay;
		FILE		*fp;
		CBaseDec::State state;
		static void *PlayThread(void*);
		void clearFileData();
		unsigned int m_SecondsToSkip;

	public:
		CAudiofile m_Audiofile;
		
		static CAudioPlayer* getInstance();
		bool play(const CAudiofile*, const bool highPrio = false);
		void stop();
		void pause();
		void init();
		void ff(unsigned int seconds = 0);
		void rev(unsigned int seconds = 0);
		CAudioMetaData getMetaData();
		bool hasMetaDataChanged();
		bool readMetaData(CAudiofile* const, const bool);
		time_t getTimePlayed(){return m_played_time;}
		time_t getTimeTotal(){return m_Audiofile.MetaData.total_time;}
		int getScBuffered(){return m_sc_buffered;}
		void sc_callback(void *arg); // see comment in .cpp
		CBaseDec::State getState(){return state;}

		CAudioPlayer();
		~CAudioPlayer();
};

#endif

