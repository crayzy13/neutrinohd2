/*
  Neutrino-GUI  -   DBoxII-Project
  
  $Id: audioplayer.h 2015/07/18 mohousch Exp $

  Copyright (C) 2001 Steffen Hehn 'McClean'
  Homepage: http://dbox.cyberphoria.org/

  Kommentar:

  Diese GUI wurde von Grund auf neu programmiert und sollte nun vom
  Aufbau und auch den Ausbaumoeglichkeiten gut aussehen. Neutrino basiert
  auf der Client-Server Idee, diese GUI ist also von der direkten DBox-
  Steuerung getrennt. Diese wird dann von Daemons uebernommen.


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

#ifndef __audioplayergui__
#define __audioplayergui__


#include "driver/framebuffer.h"
#include "driver/audiofile.h"
#include "driver/audioplay.h"
#include "gui/filebrowser.h"
#include "gui/widget/menue.h"

#include <libxmltree/xmlinterface.h>

#include <string>
#include <set>
#include <map>
#include <cstdlib>
#include <ctime>


typedef std::set<long> CPosList;
typedef std::map<unsigned char, CPosList> CTitle2Pos;
typedef std::pair<unsigned char, CPosList> CTitle2PosItem;

class CAudiofileExt : public CAudiofile
{
	public:
		CAudiofileExt();
		CAudiofileExt(std::string name, CFile::FileExtension extension);
		CAudiofileExt(const CAudiofileExt& src);
		void operator=(const CAudiofileExt& src);

		char firstChar;
};

typedef std::vector<CAudiofileExt> CAudioPlayList;

class RandomNumber
{
	public:
		RandomNumber()
		{
			srand(time(0));
		}

		int operator()(int n)
		{
			return ((long long)n * rand() / RAND_MAX);
		}
};

class CAudioPlayerGui : public CMenuTarget
{
	public:
		enum State
		{
			PLAY = 0,
			STOP,
			PAUSE,
			FF,
			REV
		};

		enum DisplayOrder 
		{
			ARTIST_TITLE = 0, 
			TITLE_ARTIST = 1
		};

	private:
		CFrameBuffer * m_frameBuffer;

		//
		unsigned int   m_selected;
		int            m_current;
		unsigned int   m_liststart;
		unsigned int   m_listmaxshow;
		int            m_fheight; 	// Fonthoehe Playlist-Inhalt
		int            m_theight; 	// Fonthoehe Playlist-Titel
		int            m_sheight; 	// Fonthoehe MP Info
		int            m_buttonHeight;
		int            m_title_height;
		int            m_info_height;
		int            m_width;
		int            m_height;
		int            m_x;
		int            m_y;
		int            m_title_w;
		
		int icon_foot_w;
		int icon_foot_h;
		int icon_head_w;
		int icon_head_h;

		//
		int            m_key_level;
		bool           m_visible;
		State          m_state;
		time_t         m_time_total;
		time_t         m_time_played;
		std::string    m_metainfo;
		bool           m_select_title_by_name;
		bool           m_playlistHasChanged;

		//
		CAudioPlayList m_playlist;
		CTitle2Pos     m_title2Pos;
		CAudiofileExt  m_curr_audiofile;
		std::string    m_Path;

		int            m_LastMode;
		int            m_idletime;
		bool           m_inetmode;
		uint32_t       stimer;
		
		bool 		info_visible;

		SMSKeyInput    m_SMSKeyInput;
		bool hide_playlist;
		bool isURL;

		bool updateMeta;
		bool updateLcd;
		bool updateScreen;

		std::string m_title;
		CFileFilter audiofilefilter;

		//
		void Init(void);

		//
		void paintItem(int pos);
		void paint();
		void paintHead();
		void paintFoot();
		void paintInfo();
		void paintLCD();
		void hide();

		//
		void get_id3(CAudiofileExt * audiofile);
		void get_mp3info(CAudiofileExt * audiofile);
		void paintItemID3DetailsLine(int pos);
		void clearItemID3DetailsLine();
		void play(unsigned int pos);
		void stop();
		void pause();
		void ff(unsigned int seconds = 0);
		void rev(unsigned int seconds = 0);
		int getNext();
		void GetMetaData(CAudiofileExt &File);
		void updateMetaData();
		void updateTimes(const bool force = false);
		void showMetaData();
		bool getNumericInput(neutrino_msg_t& msg,int& val);

		//void removeFromPlaylist(long pos);

		/**
		* Adds an url (icecast, ...) to the to the audioplayer playlist
		*/
		void addUrl2Playlist(const char *url, const char *name = NULL, const time_t bitrate = 0);

		/**
		* Adds a url which points to an .m3u format (playlist, ...) to the audioplayer playlist
		*/
		void processPlaylistUrl(const char *url, const char *name = NULL, const time_t bitrate = 0);

		void selectTitle(unsigned char selectionChar);
		/**
		* Appends the file information to the given string.
		* @param fileInfo a string where the file information will be appended
		* @param file the file to return the information for
		*/
		void getFileInfoToDisplay(std::string& fileInfo, CAudiofileExt &file);
		void printSearchTree();
		void buildSearchTree();
		unsigned char getFirstChar(CAudiofileExt &file);

		/**
		* Saves the current playlist into a .m3u playlist file.
		*/
		void savePlaylist();

		/**
		* Converts an absolute filename to a relative one
		* as seen from a file in fromDir.
		* Example:
		* absFilename: /mnt/audio/A/abc.mp3
		* fromDir: /mnt/audio/B
		* => ../A/abc.mp3 will be returned 
		* @param fromDir the directory from where we want to
		* access the file
		* @param absFilename the file we want to access in a
		* relative way from fromDir (given as an absolute path)
		* @return the location of absFilename as seen from fromDir
		* (relative path)
		*/
		std::string absPath2Rel(const std::string& fromDir, const std::string& absFilename);
	
		/** 
		* Asks the user if the file filename should be overwritten or not
		* @param filename the name of the file
		* @return true if file should be overwritten, false otherwise
		*/
		bool askToOverwriteFile(const std::string& filename);
		//bool clearPlaylist(void);
		bool shufflePlaylist(void);
		bool playNext(bool allow_rotate = false);
		bool playPrev(bool allow_rotate = false);

		//
		bool openFilebrowser(void);
	
	public:
		CAudioPlayerGui();
		~CAudioPlayerGui();
		int show();
		int exec(CMenuTarget *parent, const std::string &actionKey);

		//
		void scanXmlFile(std::string filename);
		void scanXmlData(xmlDocPtr answer_parser, const char *nametag, const char *urltag, const char *bitratetag = NULL, bool usechild = false);
		void addToPlaylist(CAudiofileExt &file);
		void removeFromPlaylist(long pos);
		void clearPlaylist(void);
		void hidePlayList(bool hide = false){hide_playlist = hide;};
		void setTitle(const std::string& title){ m_title = title;};
		void setInetMode(void){m_inetmode = true; m_Path = CONFIGDIR "/";};
};

#endif
