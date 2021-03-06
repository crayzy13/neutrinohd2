/*
  Neutrino-GUI  -   DBoxII-Project
  
  $Id: movieplayer.cpp 2013/10/12 mohousch Exp $

  Movieplayer (c) 2003, 2004 by gagga
  Based on code by Dirch, obi and the Metzler Bros. Thanks.

  $Id: movieplayer.cpp,v 1.97 2004/07/18 00:54:52 thegoodguy Exp $

  Homepage: http://www.giggo.de/dbox2/movieplayer.html

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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/mount.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <poll.h>
#include <sys/timeb.h>

#include <gui/movieplayer.h>

#include <global.h>
#include <neutrino.h>

#include <driver/fontrenderer.h>
#include <driver/rcinput.h>
#include <driver/vcrcontrol.h>

#include <daemonc/remotecontrol.h>

#include <system/settings.h>
#include <system/helpers.h>

#include <gui/eventlist.h>
#include <gui/color.h>
#include <gui/infoviewer.h>
#include <gui/nfs.h>
#include <gui/audio_video_select.h>

#include <gui/widget/buttons.h>
#include <gui/widget/icons.h>
#include <gui/widget/messagebox.h>
#include <gui/widget/hintbox.h>
#include <gui/widget/stringinput.h>
#include <gui/widget/stringinput_ext.h>
#include <gui/widget/helpbox.h>
#include <gui/widget/infobox.h>

#include <system/debug.h>

#include <libxmltree/xmlinterface.h>

//
#include <playback_cs.h>
#include <video_cs.h>

//
//#include <channel.h>


// scripts
#define MOVIEPLAYER_START_SCRIPT 	CONFIGDIR "/movieplayer.start" 
#define MOVIEPLAYER_END_SCRIPT 		CONFIGDIR "/movieplayer.end"

extern cPlayback *playback;

// movieplayer
CMoviePlayerGui::CMoviePlayerGui()
{
	stopped = false;

	frameBuffer = CFrameBuffer::getInstance();

	// local path
	if (strlen(g_settings.network_nfs_moviedir) != 0)
		Path_local = g_settings.network_nfs_moviedir;
	else
		Path_local = "/";

	selected = 0;
}

CMoviePlayerGui::~CMoviePlayerGui()
{
	filelist.clear();
}

void CMoviePlayerGui::cutNeutrino()
{
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::cutNeutrino\n");
	
	if (stopped)
		return;
	
	// tell neutrino we are in ts mode
	CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::CHANGEMODE, NeutrinoMessages::mode_ts);
	
	// save (remeber) last mode
	m_LastMode = (CNeutrinoApp::getInstance()->getLastMode() | NeutrinoMessages::norezap);
	
	//
	CNeutrinoApp::getInstance()->lockPlayBack();
	
	// start mp start-script
	puts("[movieplayer.cpp] executing " MOVIEPLAYER_START_SCRIPT ".");
	if (system(MOVIEPLAYER_START_SCRIPT) != 0)
		perror("Datei " MOVIEPLAYER_START_SCRIPT " fehlt. Bitte erstellen, wenn gebraucht.\nFile " MOVIEPLAYER_START_SCRIPT " not found. Please create if needed.\n");

	stopped = true;
}

void CMoviePlayerGui::restoreNeutrino()
{
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::restoreNeutrino\n");
	
	if (!stopped)
		return;
	
	//	
	CNeutrinoApp::getInstance()->unlockPlayBack();

	// tell neutrino that we are in the last mode
	CNeutrinoApp::getInstance()->handleMsg(NeutrinoMessages::CHANGEMODE, m_LastMode);
	
	//show infobar
	g_RCInput->postMsg( NeutrinoMessages::SHOW_INFOBAR, 0 );
	
	// start end script
	puts("[movieplayer.cpp] executing " MOVIEPLAYER_END_SCRIPT ".");
	if (system(MOVIEPLAYER_END_SCRIPT) != 0)
		perror("Datei " MOVIEPLAYER_END_SCRIPT " fehlt. Bitte erstellen, wenn gebraucht.\nFile " MOVIEPLAYER_END_SCRIPT " not found. Please create if needed.\n");

	stopped = false;
}

void CMoviePlayerGui::updateLcd(const std::string & lcd_filename)
{
	char tmp[20];
	std::string lcd;

	switch (playstate) 
	{
		case CMoviePlayerGui::PAUSE:
			//lcd = "|| ";
			lcd = lcd_filename;
			break;
			
		case CMoviePlayerGui::REW:
			sprintf(tmp, "%dx<< ", speed);
			lcd = tmp;
			lcd += lcd_filename;
			break;
			
		case CMoviePlayerGui::FF:
			sprintf(tmp, "%dx>> ", speed);
			lcd = tmp;
			lcd += lcd_filename;
			break;

		case CMoviePlayerGui::SLOW:
			sprintf(tmp, "%ds>> ", slow);
			lcd = tmp;
			lcd += lcd_filename;
			break;

		default:
			//lcd = "> ";
			lcd = lcd_filename;
			break;
	}
	
	CVFD::getInstance()->showMenuText(0, lcd.c_str(), -1, true);	
}

void CMoviePlayerGui::addToPlaylist(MI_MOVIE_INFO& mfile)
{	
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::addToPlaylist: %s\n", mfile.file.Name.c_str());
	
	filelist.push_back(mfile);
}

void CMoviePlayerGui::clearPlaylist(void)
{
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::clearPlaylist:\n");

	if (!filelist.empty())
	{
		filelist.clear();
		selected = 0;
	}
}

void CMoviePlayerGui::removeFromPlaylist(long pos)
{
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::removeFromPlayList:\n");

	filelist.erase(filelist.begin() + pos); 
}

void CMoviePlayerGui::hide()
{
	frameBuffer->ClearFrameBuffer();
	frameBuffer->blit();
}

int CMoviePlayerGui::exec(CMenuTarget * parent, const std::string & actionKey)
{
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::exec: actionKey = %s\n", actionKey.c_str());

	if (parent) 
		parent->hide();

	bool usedBackground = frameBuffer->getuseBackground();

	if (usedBackground) 
	{
		frameBuffer->saveBackgroundImage();
		frameBuffer->ClearFrameBuffer();
		frameBuffer->blit();
	}
	
	//
	position = 0;
	file_prozent = 0;
	duration = 0;
	startposition = 0;
	
	//
	speed = 1;
	slow = 0;
	
	// global flags
	update_lcd = false;
	open_filebrowser = false;
	start_play = false;
	exit = false;
	was_file = false;
	m_loop = false;
	m_multiselect = false;
	isMovieBrowser = false;
	isURL = false;
	
	// for playing
	playstate = CMoviePlayerGui::STOPPED;
	is_file_player = false;
	
	//
	time_forced = false;
	
	selected = 0;
	
	// 
	g_numpida = 0;
	g_vpid = 0;
	g_vtype = 0;
	g_currentapid = 0;
	g_currentac3 = 0;
	
	// cutneutrino
	cutNeutrino();

	if (actionKey == "tsmoviebrowser") 
	{
		open_filebrowser = true;
		isMovieBrowser = true;
		isURL = false;
		
		// moviebrowser
		moviebrowser = new CMovieBrowser();
		moviebrowser->setMode(MB_SHOW_RECORDS);
	}
	else if (actionKey == "moviebrowser") 
	{
		open_filebrowser = true;
		isMovieBrowser = true;
		isURL = false;
		
		// moviebrowser
		moviebrowser = new CMovieBrowser();
		moviebrowser->setMode(MB_SHOW_FILES);
	}
	else if(actionKey == "urlplayback")
	{
		open_filebrowser = false;
		isMovieBrowser = false;
		isURL = true;
	}
	
	//
	PlayFile();
	
	// quit
	// Restore previous background
	if (usedBackground) 
	{
		frameBuffer->restoreBackgroundImage();
		frameBuffer->useBackground(true);
		frameBuffer->paintBackground();
		frameBuffer->blit();
	}
	
	// clear filelist
	if(!filelist.empty())
		filelist.clear();
	
	//
	if (actionKey == "tsmoviebrowser" || actionKey == "moviebrowser") 
	{
		if (moviebrowser != NULL)
		{
			delete moviebrowser;
			moviebrowser = NULL;
		}
	}

	// restore neutrino
	restoreNeutrino();
	
	//
	CVFD::getInstance()->setMode(CVFD::MODE_TVRADIO);

	return menu_return::RETURN_REPAINT;
}

//
void CMoviePlayerGui::PlayFile(void)
{
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile\n");

	neutrino_msg_t msg;
	neutrino_msg_data_t data;
	
	// url
	if(isURL)
	{
		//
		if(!filelist.empty())
		{
			//
			if(filelist[0].ytid.empty())
			{
				if(!filelist[0].audioPids.empty()) 
				{
					g_currentapid = filelist[0].audioPids[0].epgAudioPid;
					g_currentac3 = filelist[0].audioPids[0].atype;

					//
					currentapid = g_currentapid;
				}

				for (int i = 0; i < (int)filelist[0].audioPids.size(); i++) 
				{
					if (filelist[0].audioPids[i].selected) 
					{
#if defined (PLATFORM_COOLSTREAM)
						g_currentapid = filelist[selected].audioPids[i].epgAudioPid;
#else
						g_currentapid = i;	//FIXME
#endif						
						g_currentac3 = filelist[0].audioPids[i].atype;
						//

#if defined (PLATFORM_COOLSTREAM)
						currentapid = g_currentapid;
						currentac3 = g_currentac3;
#else
						currentapid = 0;
#endif
					}
				}

				//
				g_vpid = filelist[0].epgVideoPid;
				g_vtype = filelist[0].VideoType;

				// startposition			
				startposition = 1000 * showStartPosSelectionMenu();
			}

			//
			update_lcd = true;
			start_play = true;
			was_file = true;

			//
			if(filelist.size() > 1)
				m_multiselect = true;
		}
						
		CVFD::getInstance()->setMode(CVFD::MODE_MENU_UTF8);	
	}

	// bookmarks menu
	timeb current_time;

	int width = 280;
	int height = 65;
        int x = frameBuffer->getScreenX() + (frameBuffer->getScreenWidth() - width) / 2;
        int y = frameBuffer->getScreenY() + frameBuffer->getScreenHeight() - height - 20;

	CBox boxposition(x, y, width, height);	// window position for the hint boxes

	// backword hintbox
	CTextBox newBackwordHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_NEWBOOK_BACKWARD), NULL, CTextBox::CENTER , &boxposition);

	// forward hintbox
	CTextBox newForwardHintBox(g_Locale->getText(LOCALE_MOVIEBROWSER_HINT_NEWBOOK_FORWARD), NULL, CTextBox::CENTER, &boxposition);

	//
	int jump_not_until = 0;		// any jump shall be avoided until this time (in seconds from moviestart)
	MI_BOOKMARK new_bookmark;	// used for new movie info bookmarks created from the movieplayer
	new_bookmark.pos = 0;		// clear , since this is used as flag for bookmark activity
	new_bookmark.length = 0;

	//
#define BOOKMARK_START_MENU_MAX_ITEMS 5
	CSelectedMenu cSelectedMenuBookStart[BOOKMARK_START_MENU_MAX_ITEMS];

	CMenuWidget bookStartMenu(LOCALE_MOVIEBROWSER_BOOK_NEW, NEUTRINO_ICON_STREAMING);
	bookStartMenu.disableMenuPosition();

	bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_NEW, true, NULL, &cSelectedMenuBookStart[0]));
	bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_TYPE_FORWARD, true, NULL, &cSelectedMenuBookStart[1]));
	bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_TYPE_BACKWARD, true, NULL, &cSelectedMenuBookStart[2]));
	bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_MOVIESTART, true, NULL, &cSelectedMenuBookStart[3]));
	bookStartMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_MOVIEEND, true, NULL, &cSelectedMenuBookStart[4]));

	// play loop
 go_repeat:
	do {
		// multi select|loop
		if (playstate == CMoviePlayerGui::STOPPED && was_file && selected > 0) 
		{
			if(selected + 1 < filelist.size()) 
			{
				selected++;

				//
				if(filelist[selected].ytid.empty())
				{ 
					if(!filelist[selected].audioPids.empty()) 
					{
						g_currentapid = filelist[selected].audioPids[0].epgAudioPid;
						g_currentac3 = filelist[selected].audioPids[0].atype;

						//
						currentapid = g_currentapid;
					}

					for (int i = 0; i < (int)filelist[selected].audioPids.size(); i++) 
					{
						if (filelist[selected].audioPids[i].selected) 
						{
#if defined (PLATFORM_COOLSTREAM)
							g_currentapid = filelist[selected].audioPids[i].epgAudioPid;
#else
							g_currentapid = i;	//FIXME
#endif							
							g_currentac3 = filelist[selected].audioPids[i].atype;

							//
#if defined (PLATFORM_COOLSTREAM)
							currentapid = g_currentapid;
							currentac3 = g_currentac3;
#else
							currentapid = 0;
#endif
						}
					}

					//
					g_vpid = filelist[selected].epgVideoPid;
					g_vtype = filelist[selected].VideoType;

					// startposition			
					startposition = 1000 * showStartPosSelectionMenu();
				}
				
				//
				update_lcd = true;
				start_play = true;
			} 
			else if(m_loop) // loop
			{
				update_lcd = true;
				start_play = true;
			}
			else 
			{
				open_filebrowser = true;
			}
		}
		
		// exit
		if (exit) 
		{	  
			exit = false;
			
			dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile: stop (3)\n");
			playstate = CMoviePlayerGui::STOPPED;
			break;
		}
		
		// do bookmarks
		bool doBookmark = true;
		if (doBookmark)
		{	  
			if (playstate == CMoviePlayerGui::PLAY) 
			{				
#if defined (PLATFORM_COOLSTREAM)
				playback->GetPosition(position, duration);
#else
 				playback->GetPosition((int64_t &)position, (int64_t &)duration);
#endif				
				// check if !jump is stale (e.g. if user jumped forward or backward)
				int play_sec = position / 1000;	// get current seconds from moviestart

				if (play_sec + 10 < jump_not_until || play_sec > jump_not_until + 10)
					jump_not_until = 0;

				// do bookmark activities only, if there is no new bookmark started
				if (new_bookmark.pos == 0)
				{
					// process bookmarks if we have any movie info
					if (&filelist[selected] != NULL)
					{
						// movieend bookmark
						if (filelist[selected].bookmarks.end != 0) 
						{
							// stop playing
							// we ARE close behind the stop position, stop playing 
							if (play_sec >= filelist[selected].bookmarks.end && play_sec <= filelist[selected].bookmarks.end + 2 && play_sec > jump_not_until)
							{
								dprintf(DEBUG_INFO, "CMoviePlayerGui::PlayFile: bookmark stop\r\n");
								playstate = CMoviePlayerGui::STOPPED;
							}
						}
						
						// jump bookmark
						int loop = true;

						for (int book_nr = 0; book_nr < MI_MOVIE_BOOK_USER_MAX && loop == true; book_nr++) 
						{
							if (filelist[selected].bookmarks.user[book_nr].pos != 0 && filelist[selected].bookmarks.user[book_nr].length != 0) 
							{
								// do jump
								if (play_sec >= filelist[selected].bookmarks.user[book_nr].pos && play_sec <= filelist[selected].bookmarks.user[book_nr].pos + 2 && play_sec > jump_not_until)	//
								{
									g_jumpseconds = filelist[selected].bookmarks.user[book_nr].length;

									// jump back
									if (filelist[selected].bookmarks.user[book_nr].length < 0) 
									{
										if (g_jumpseconds > -15)
											g_jumpseconds = -15;

										g_jumpseconds = g_jumpseconds + filelist[selected].bookmarks.user[book_nr].pos;

										//playstate = CMoviePlayerGui::JPOS;	// bookmark  is of type loop, jump backward
										playback->SetPosition((int64_t)g_jumpseconds * 1000);
									} 
									// jump forward
									else if (filelist[selected].bookmarks.user[book_nr].length > 0) 
									{
										// jump at least 15 seconds
										if (g_jumpseconds < 15)
											g_jumpseconds = 15;
										g_jumpseconds = g_jumpseconds + filelist[selected].bookmarks.user[book_nr].pos;

										//
										playback->SetPosition((int64_t)g_jumpseconds * 1000);
									}
									
									dprintf(DEBUG_INFO, "CMoviePlayerGui::PlayFile: do jump %d sec\r\n", g_jumpseconds);
									update_lcd = true;
									loop = false;	// do no further bookmark checks
								}
							}
						}
					}
				}
			}
		}	

		// moviebrowser/filebrowser
		if (open_filebrowser) 
		{
			open_filebrowser = false;
			
			FileTime.hide();

			// moviebrowser
			if (isMovieBrowser == true) //moviebrowser
			{	
				if (moviebrowser->exec(Path_local.c_str())) 
				{
					// get the current path and file name
					Path_local = moviebrowser->getCurrentDir();

					if (moviebrowser->getSelectedFile() != NULL) 
					{
						// clear playlist
						clearPlaylist();

						// add to playlist
						addToPlaylist(*moviebrowser->getCurrentMovieInfo());

						//
						if(!filelist[0].audioPids.empty()) 
						{
							g_currentapid = filelist[0].audioPids[0].epgAudioPid;	//FIXME
							g_currentac3 = filelist[0].audioPids[0].atype;

							//
							currentapid = g_currentapid;
						}

						for (int i = 0; i < (int)filelist[0].audioPids.size(); i++) 
						{
							if (filelist[0].audioPids[i].selected) 
							{
#if defined (PLATFORM_COOLSTREAM)
								g_currentapid = filelist[selected].audioPids[i].epgAudioPid;
#else
								g_currentapid = i;	//FIXME
#endif								
								g_currentac3 = filelist[0].audioPids[i].atype;
								//
#if defined (PLATFORM_COOLSTREAM)
								currentapid = g_currentapid;
								currentac3 = g_currentac3;
#else
								currentapid = 0;
#endif
							}
						}

						// startposition					
						startposition = 1000 * showStartPosSelectionMenu();						

						//
						g_vpid = filelist[0].epgVideoPid;
						g_vtype = filelist[0].VideoType;
						
						dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile: file:%s apid:0x%X atype:%d vpid:0x%X vtype:%d startposition:%d\n", filelist[selected].file.Name.c_str(), g_currentapid, g_currentac3, g_vpid, g_vtype, startposition/1000);
						
						update_lcd = true;
						start_play = true;
						was_file = true;
					}
				} 
				else if (playstate == CMoviePlayerGui::STOPPED) 
				{
					was_file = false;
					break;
				}
				
				CVFD::getInstance()->setMode(CVFD::MODE_MENU_UTF8);	
			}
		}

		// LCD 
		if (update_lcd) 
		{
			update_lcd = false;

			updateLcd(filelist[selected].epgTitle.empty()? filelist[selected].file.getFileName() : filelist[selected].epgTitle);
		}

		// timeosd
		if (FileTime.IsVisible()) 
		{
			if (FileTime.GetMode() == CMovieInfoViewer::MODE_ASC) 
			{
				FileTime.update(position / 1000);
			} 
			else 
			{
				FileTime.update((duration - position) / 1000);
			}

			FileTime.show(filelist[selected].epgTitle, (filelist[selected].epgInfo1.empty())? filelist[selected].epgInfo2 : filelist[selected].epgInfo1, (duration != 0)? (position / (duration / 100)) : 0, ac3state, speed, playstate, (filelist[selected].ytid.empty())? true : false);

			time_t jetzt = time(NULL);

			if((jetzt - timeStartShowingInfo) >= 60) // 60 sec
				FileTime.hide();
		}

		// start playing
		if (start_play) 
		{
			dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile: Startplay at %d seconds\n", startposition/1000);

			start_play = false;
			
			//
			if(playback->playing)
				playback->Close();

			// init player
#if defined (PLATFORM_COOLSTREAM)
			if(filelist[selected].file.getExtension() != CFile::EXTENSION_TS)
				is_file_player = true;

			playback->Open(is_file_player ? PLAYMODE_FILE : PLAYMODE_TS);
#else			
			playback->Open();
#endif			
			
			duration = 0;
			if(filelist[selected].length != 0)
				duration = filelist[selected].length * 60 * 1000;
			  
			// PlayBack Start
#if defined (PLATFORM_COOLSTREAM)			  
			if(!playback->Start((char *)filelist[selected].file.Name.c_str(), g_vpid, g_vtype, g_currentapid, g_currentac3, duration))
#else
			if(!playback->Start((char *)filelist[selected].file.Name.c_str()))
#endif
			{
				dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile: Starting Playback failed!\n");
				playback->Close();
				restoreNeutrino();
			} 
			else 
			{
				// set PlayState
				playstate = CMoviePlayerGui::PLAY;

				CVFD::getInstance()->ShowIcon(VFD_ICON_PLAY, true);
				
				// set position 
				playback->SetPosition((int64_t)startposition);
				
				//
#if defined (PLATFORM_COOLSTREAM)
				playback->GetPosition(position, duration);
#else
				playback->GetPosition((int64_t &)position, (int64_t &)duration);
#endif

				// movieInfoviewer
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);

				timeStartShowingInfo = time(NULL);	
			}
		}

		//get position/duration/speed during playing
		if ( playstate >= CMoviePlayerGui::PLAY )
		{
#if defined (PLATFORM_COOLSTREAM)
			if( playback->GetPosition(position, duration) )
#else
			if( playback->GetPosition((int64_t &)position, (int64_t &)duration) )
#endif			
			{					
				if(duration > 100)
					file_prozent = (unsigned char) (position / (duration / 100));

				playback->GetSpeed(speed);
								
				dprintf(DEBUG_DEBUG, "CMoviePlayerGui::PlayFile: speed %d position %d duration %d percent(%d%%)\n", speed, position, duration, file_prozent);					
			}
			else
			{
				if(filelist.size() > 1 && selected + 1 < filelist.size())
					g_RCInput->postMsg(CRCInput::RC_next, 0);
				else
					g_RCInput->postMsg((neutrino_msg_t) g_settings.mpkey_stop, 0);
			}
		}
		
		// loop msg
		g_RCInput->getMsg(&msg, &data, 10);	// 1 secs
		
		if (msg == (neutrino_msg_t) g_settings.mpkey_stop) 
		{
			dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile: stop (1)\n");

			//exit play
			playstate = CMoviePlayerGui::STOPPED;
			
			if(filelist[selected].ytid == "timeshift")
			{
				// stop record if recording
				if( CNeutrinoApp::getInstance()->recordingstatus) 
				{
					if(MessageBox(LOCALE_MESSAGEBOX_INFO, LOCALE_SHUTDOWN_RECODING_QUERY, CMessageBox::mbrYes, CMessageBox::mbYes | CMessageBox::mbNo, NULL, 450, 30, true) == CMessageBox::mbrYes)
					{
						CVCRControl::getInstance()->Stop();
						g_Timerd->stopTimerEvent(CNeutrinoApp::getInstance()->recording_id);
						CVFD::getInstance()->ShowIcon(VFD_ICON_TIMESHIFT, false );

						CNeutrinoApp::getInstance()->recording_id = 0;
						CNeutrinoApp::getInstance()->recordingstatus = 0;
						CNeutrinoApp::getInstance()->timeshiftstatus = 0;
					}
				} 
				//
			}
			
			if(isURL)
				was_file = false;
			
			if(m_loop)
				m_loop = false;
			
			if(m_multiselect)
				m_multiselect = false;
			
			exit = true;
		} 
		else if (msg == (neutrino_msg_t) g_settings.mpkey_play) 
		{
			if (playstate >= CMoviePlayerGui::PLAY) 
			{
				playstate = CMoviePlayerGui::PLAY;
				update_lcd = true;
				CVFD::getInstance()->ShowIcon(VFD_ICON_PLAY, true);
				CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, false);
				
				speed = 1;
				playback->SetSpeed(speed);
			} 
			else if(filelist[selected].ytid != "timeshift")
			{
				open_filebrowser = true;
			}

			if (time_forced) 
			{
				time_forced = false;
				
				FileTime.hide();
			}
			
			if (FileTime.IsVisible()) 
				FileTime.hide();

			// movie title
			if(filelist[selected].ytid != "timeshift")
			{
				if (FileTime.IsVisible()) 
				{
					if (FileTime.GetMode() == CMovieInfoViewer::MODE_ASC) 
					{
						FileTime.SetMode(CMovieInfoViewer::MODE_DESC);
						FileTime.update((duration - position) / 1000);
					} 
					else 
					{
						FileTime.hide();
					}
				}
				else 
				{
					FileTime.SetMode(CMovieInfoViewer::MODE_ASC);

					timeStartShowingInfo = time(NULL);
				}
			}
		} 
		else if ( msg == (neutrino_msg_t) g_settings.mpkey_pause) 
		{
			update_lcd = true;
			
			if (playstate == CMoviePlayerGui::PAUSE) 
			{
				playstate = CMoviePlayerGui::PLAY;
				CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, false);
				// show play icon
				CVFD::getInstance()->ShowIcon(VFD_ICON_PLAY, true);
				speed = 1;
				playback->SetSpeed(speed);
			} 
			else 
			{
				playstate = CMoviePlayerGui::PAUSE;
				CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, true);
				CVFD::getInstance()->ShowIcon(VFD_ICON_PLAY, false);
				speed = 0;
				playback->SetSpeed(speed);
			}

			//show MovieInfoBar
			if(filelist[selected].ytid != "timeshift")
			{
				if (FileTime.IsVisible()) 
				{
					if (FileTime.GetMode() == CMovieInfoViewer::MODE_ASC) 
					{
						FileTime.SetMode(CMovieInfoViewer::MODE_DESC);
						FileTime.update((duration - position) / 1000);
					} 
					else 
					{
						FileTime.hide();
					}
				}
				else 
				{
					FileTime.SetMode(CMovieInfoViewer::MODE_ASC);

					//timeStartShowingInfo = time(NULL); 
				}
			}
		} 
		else if (msg == (neutrino_msg_t) g_settings.mpkey_bookmark) 
		{
			if (FileTime.IsVisible()) 
				FileTime.hide();
			
			//			
			if(filelist[selected].ytid.empty())
			{
				int pos_sec = position / 1000;

				if (newForwardHintBox.isPainted() == true) 
				{
					// yes, let's get the end pos of the jump forward
					new_bookmark.length = pos_sec - new_bookmark.pos;
					dprintf(DEBUG_DEBUG, "CMoviePlayerGui::PlayFile: commercial length: %d\r\n", new_bookmark.length);
					if (cMovieInfo.addNewBookmark(&filelist[selected], new_bookmark) == true) 
					{
						cMovieInfo.saveMovieInfo(filelist[selected]);	// save immediately in xml file
					}
					new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
					newForwardHintBox.hide();
				} 
				else if (newBackwordHintBox.isPainted() == true) 
				{
					// yes, let's get the end pos of the jump backward
					new_bookmark.length = new_bookmark.pos - pos_sec;
					new_bookmark.pos = pos_sec;
					dprintf(DEBUG_DEBUG, "CMoviePlayerGui::PlayFile: loop length: %d\r\n", new_bookmark.length);
					if (cMovieInfo.addNewBookmark(&filelist[selected], new_bookmark) == true) 
					{
						cMovieInfo.saveMovieInfo(filelist[selected]);	// save immediately in xml file
						jump_not_until = pos_sec + 5;	// avoid jumping for this time
					}
					new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
					newBackwordHintBox.hide();
				} 
				else 
				{
					// no, nothing else to do, we open a new bookmark menu
					new_bookmark.name = "";	// use default name
					new_bookmark.pos = 0;
					new_bookmark.length = 0;

					//
					bookStartMenu.exec(NULL, "none");
					
					//
					if (cSelectedMenuBookStart[0].selected == true) 
					{
						// new bookmark
						new_bookmark.pos = pos_sec;
						new_bookmark.length = 0;

						if (cMovieInfo.addNewBookmark(&filelist[selected], new_bookmark) == true)
							cMovieInfo.saveMovieInfo(filelist[selected]);	// save immediately in xml file
						new_bookmark.pos = 0;	// clear again, since this is used as flag for bookmark activity
						cSelectedMenuBookStart[0].selected = false;	// clear for next bookmark menu
					} 
					else if (cSelectedMenuBookStart[1].selected == true)
					{
						// jump forward bookmark
						new_bookmark.pos = pos_sec;
						dprintf(DEBUG_DEBUG, "CMoviePlayerGui::PlayFile: new bookmark 1. pos: %d\r\n", new_bookmark.pos);
						newForwardHintBox.paint();

						cSelectedMenuBookStart[1].selected = false;	// clear for next bookmark menu
					} 
					else if (cSelectedMenuBookStart[2].selected == true) 
					{
						// jump backward bookmark
						new_bookmark.pos = pos_sec;
						dprintf(DEBUG_DEBUG, "CMoviePlayerGui::PlayFile: new bookmark 1. pos: %d\r\n", new_bookmark.pos);
						newBackwordHintBox.paint();
						cSelectedMenuBookStart[2].selected = false;	// clear for next bookmark menu
					} 
					else if (cSelectedMenuBookStart[3].selected == true) 
					{
						// movie start bookmark
						filelist[selected].bookmarks.start = pos_sec;

						dprintf(DEBUG_DEBUG, "CMoviePlayerGui::PlayFile: New movie start pos: %d\r\n", filelist[selected].bookmarks.start);

						cMovieInfo.saveMovieInfo(filelist[selected]);	// save immediately in xml file
						cSelectedMenuBookStart[3].selected = false;	// clear for next bookmark menu
					} 
					else if (cSelectedMenuBookStart[4].selected == true) 
					{
						// Moviebrowser movie end bookmark
						filelist[selected].bookmarks.end = pos_sec;

						dprintf(DEBUG_DEBUG, "CMoviePlayerGui::PlayFile: New movie end pos: %d\r\n", filelist[selected].bookmarks.start);

						cMovieInfo.saveMovieInfo(filelist[selected]);	//save immediately in xml file
						cSelectedMenuBookStart[4].selected = false;	// clear for next bookmark menu
					}
				}
			}		
		} 
		else if ( (msg == (neutrino_msg_t) g_settings.mpkey_audio) || ( msg == CRCInput::RC_audio) ) 
		{
			if (FileTime.IsVisible()) 
				FileTime.hide();
			
			CAVPIDSelectWidget * AVSelectHandler = new CAVPIDSelectWidget();
			AVSelectHandler->exec(NULL, "");
							
			delete AVSelectHandler;
			AVSelectHandler = NULL;
		} 
		else if(msg == CRCInput::RC_yellow)
		{
			if (FileTime.IsVisible()) 
				FileTime.hide();
			
			//show help
			showHelpTS();
		}
		else if ( msg == (neutrino_msg_t) g_settings.mpkey_time || msg == CRCInput::RC_info)
		{
			//if(!timeshift)
			if(filelist[selected].ytid != "timeshift")
			{
				if (FileTime.IsVisible()) 
				{
					if (FileTime.GetMode() == CMovieInfoViewer::MODE_ASC) 
					{
						FileTime.SetMode(CMovieInfoViewer::MODE_DESC);
						FileTime.update((duration - position) / 1000);
					} 
					else 
					{
						FileTime.hide();
					}
				}
				else 
				{
					FileTime.SetMode(CMovieInfoViewer::MODE_ASC);

					timeStartShowingInfo = time(NULL);
				}

			}
			else
			{
				if (FileTime.IsVisible()) 
					FileTime.hide();
				else
				{
					FileTime.SetMode(CMovieInfoViewer::MODE_ASC);

					timeStartShowingInfo = time(NULL);
				}
			}
		} 
		else if (msg == (neutrino_msg_t) g_settings.mpkey_rewind) 
		{
			// backward
			speed = (speed >= 0) ? -1 : speed - 1;
						
			if(speed < -15)
				speed = -15;			
			
			// hide icons
			CVFD::getInstance()->ShowIcon(VFD_ICON_PLAY, false);
			CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, false);

			playback->SetSpeed(speed);
			playstate = CMoviePlayerGui::REW;
			update_lcd = true;

			if (FileTime.IsVisible()) 
				FileTime.hide();
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		}
		else if (msg == (neutrino_msg_t) g_settings.mpkey_forward) 
		{	// fast-forward
			speed = (speed <= 0) ? 2 : speed + 1;
						
			if(speed > 15)
				speed = 15;			
			
			// icons
			CVFD::getInstance()->ShowIcon(VFD_ICON_PLAY, false);
			CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, false);

			playback->SetSpeed(speed);

			update_lcd = true;
			playstate = CMoviePlayerGui::FF;

			if (FileTime.IsVisible()) 
				FileTime.hide();

			// movie info viewer
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_1) 
		{	// Jump Backwards 1 minute
			//update_lcd = true;
			playback->SetPosition(-60 * 1000);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_3) 
		{	// Jump Forward 1 minute
			//update_lcd = true;
			playback->SetPosition(60 * 1000);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_4) 
		{	// Jump Backwards 5 minutes
			playback->SetPosition(-5 * 60 * 1000);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_6) 
		{	// Jump Forward 5 minutes
			playback->SetPosition(5 * 60 * 1000);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_7) 
		{	// Jump Backwards 10 minutes
			playback->SetPosition(-10 * 60 * 1000);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_9) 
		{	// Jump Forward 10 minutes
			playback->SetPosition(10 * 60 * 1000);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if ( msg == CRCInput::RC_2 )
		{	// goto start
			playback->SetPosition((int64_t)startposition);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if ( msg == CRCInput::RC_repeat )
		{
			if(m_loop)
				m_loop = false;
			else
				m_loop = true;
			
			dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile: Repeat Modus: [%s]\n", m_loop? "ON" : "OFF");
		} 
		else if (msg == CRCInput::RC_5) 
		{	
			// goto middle
			playback->SetPosition((int64_t)duration/2);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_8) 
		{	
			// goto end
			playback->SetPosition((int64_t)duration - 60 * 1000);
			
			//time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_page_up) 
		{
			playback->SetPosition(10 * 1000);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}

		} 
		else if (msg == CRCInput::RC_page_down) 
		{
			playback->SetPosition(-10 * 1000);
			
			// time
			if (!FileTime.IsVisible()) 
			{
				FileTime.SetMode(CMovieInfoViewer::MODE_ASC);
					
				time_forced = true;

				timeStartShowingInfo = time(NULL);
			}
		} 
		else if (msg == CRCInput::RC_0) 
		{
			// cancel bookmark
			if (new_bookmark.pos != 0) 
			{
				new_bookmark.pos = 0;	// stop current bookmark activity, TODO:  might bemoved to another key
				if(newBackwordHintBox.isPainted())
					newBackwordHintBox.hide();

				if(newForwardHintBox.isPainted())
					newForwardHintBox.hide();
			}
			jump_not_until = (position / 1000) + 10;
		} 
#if !defined (PLATFORM_COOLSTREAM)		
		else if (msg == CRCInput::RC_slow) 
		{
			if (slow > 0)
				slow = 0;
			
			slow += 2;
		
			// set slow
			playback->SetSlow(slow);
			//update_lcd = true;
			playstate = CMoviePlayerGui::SLOW;
			update_lcd = true;
		}
#endif		
		else if(msg == CRCInput::RC_red)
		{
			if (FileTime.IsVisible()) 
				FileTime.hide();
			
				showFileInfo();
		}
		else if(msg == CRCInput::RC_home)
		{
			if (FileTime.IsVisible()) 
				FileTime.hide();
		}
		else if(msg == CRCInput::RC_left || msg == CRCInput::RC_prev)
		{
			//FIXME:
			if(!filelist.empty() && selected > 0 && playstate == CMoviePlayerGui::PLAY) 
			{
				selected--;

				//
				if(filelist[selected].ytid.empty())
				{
					if(!filelist[selected].audioPids.empty()) 
					{
						g_currentapid = filelist[selected].audioPids[0].epgAudioPid;
						g_currentac3 = filelist[selected].audioPids[0].atype;

							//
							currentapid = g_currentapid;
					}

					for (int i = 0; i < (int)filelist[selected].audioPids.size(); i++) 
					{
						if (filelist[selected].audioPids[i].selected) 
						{
#if defined (PLATFORM_COOLSTREAM)
							g_currentapid = filelist[selected].audioPids[i].epgAudioPid;
#else
							g_currentapid = i;
#endif
							g_currentac3 = filelist[selected].audioPids[i].atype;

							//
#if defined (PLATFORM_COOLSTREAM)
							currentapid = g_currentapid;
							currentac3 = g_currentac3;
#else
							currentapid = 0;
#endif
						}
					}

					//
					g_vpid = filelist[selected].epgVideoPid;
					g_vtype = filelist[selected].VideoType;

					// startposition			
					startposition = 1000 * showStartPosSelectionMenu();
				}

				//
				update_lcd = true;
				start_play = true;
			}
		}
		else if(msg == CRCInput::RC_right || msg == CRCInput::RC_next)
		{
			//FIXME:
			if(!filelist.empty() && selected + 1 < filelist.size() && playstate == CMoviePlayerGui::PLAY) 
			{
				selected++;

				//
				if(filelist[selected].ytid.empty())
				{
					if(!filelist[selected].audioPids.empty()) 
					{
						g_currentapid = filelist[selected].audioPids[0].epgAudioPid;
						g_currentac3 = filelist[selected].audioPids[0].atype;

							//
							currentapid = g_currentapid;
					}

					for (int i = 0; i < (int)filelist[selected].audioPids.size(); i++) 
					{
						if (filelist[selected].audioPids[i].selected) 
						{
#if defined (PLATFORM_COOLSTREAM)
							g_currentapid = filelist[selected].audioPids[i].epgAudioPid;
#else
							g_currentapid = i;
#endif							
							g_currentac3 = filelist[selected].audioPids[i].atype;

							//
#if defined (PLATFORM_COOLSTREAM)
							currentapid = g_currentapid;
							currentac3 = g_currentac3;
#else
							currentapid = 0;
#endif
						}
					}

					//
					g_vpid = filelist[selected].epgVideoPid;
					g_vtype = filelist[selected].VideoType;

					// startposition			
					startposition = 1000 * showStartPosSelectionMenu();
				}
				
				//
				update_lcd = true;
				start_play = true;
			}
		}
		else if (msg == (neutrino_msg_t)g_settings.key_screenshot && isMovieBrowser == true )
		{
         		if(MessageBox(LOCALE_MESSAGEBOX_INFO, g_Locale->getText(LOCALE_SCREENSHOT_ANNOUNCE), CMessageBox::mbrNo, CMessageBox:: mbYes | CMessageBox::mbNo) == CMessageBox::mbrYes) 
			{
				CVCRControl::getInstance()->Screenshot(0, (char *)filelist[selected].file.Name.c_str());
			}
		}
		else if ((msg == NeutrinoMessages::ANNOUNCE_RECORD) || msg == NeutrinoMessages::RECORD_START || msg == NeutrinoMessages::ZAPTO || msg == NeutrinoMessages::STANDBY_ON || msg == NeutrinoMessages::SHUTDOWN || msg == NeutrinoMessages::SLEEPTIMER) 
		{	
			// Exit for Record/Zapto Timers
			exit = true;
			g_RCInput->postMsg(msg, data);
		}
		else 
		{
			if (CNeutrinoApp::getInstance()->handleMsg(msg, data) & messages_return::cancel_all)
				exit = true;
			else if ( msg <= CRCInput::RC_MaxRC ) 
			{
				CVFD::getInstance()->setMode(CVFD::MODE_MENU_UTF8);
	
				update_lcd = true;
			}
		}

		if (exit) 
		{
			dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile: stop (2)\n");	

			if(filelist[selected].ytid.empty() && filelist[selected].file.getType() != CFile::FILE_AUDIO)
			{
				// if we have a movie information, try to save the stop position
				ftime(&current_time);
				filelist[selected].dateOfLastPlay = current_time.time;
				current_time.time = time(NULL);
				filelist[selected].bookmarks.lastPlayStop = position / 1000;
				
				cMovieInfo.saveMovieInfo(filelist[selected]);
			}
		}
	} while (playstate >= CMoviePlayerGui::PLAY);
	
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::PlayFile: stop (4)\n");	

	if(FileTime.IsVisible())
		FileTime.hide();
	
	playback->Close();

	CVFD::getInstance()->ShowIcon(VFD_ICON_PLAY, false);
	CVFD::getInstance()->ShowIcon(VFD_ICON_PAUSE, false);

	if (was_file) 
	{
		usleep(3000);
		
		if(!isURL)
			open_filebrowser = true;
		
		if(m_multiselect || m_loop)
			start_play = true;
		
		goto go_repeat;
	}
}

void CMoviePlayerGui::showHelpTS()
{
	CHelpBox helpbox;

	helpbox.addLine(NEUTRINO_ICON_BUTTON_RED, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP1));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_GREEN, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP2));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_YELLOW, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP3));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_BLUE, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP4));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_SETUP, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP5));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_HELP, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP15));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_1, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP6));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_2, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP12) );
	helpbox.addLine(NEUTRINO_ICON_BUTTON_3, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP7));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_4, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP8));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_5, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP13));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_6, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP9));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_7, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP10));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_8, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP14));
	helpbox.addLine(NEUTRINO_ICON_BUTTON_9, g_Locale->getText(LOCALE_MOVIEPLAYER_TSHELP11));
	helpbox.addLine("Version: $Revision: 1.97 $");
	helpbox.addLine("Movieplayer (c) 2003, 2004 by gagga");

	hide();

	helpbox.show(LOCALE_MESSAGEBOX_INFO);
}

void CMoviePlayerGui::showFileInfo()
{
	dprintf(DEBUG_INFO, "CMoviePlayerGui::showFileInfo:%s %s %s\n", filelist[selected].epgTitle.c_str(), filelist[selected].epgInfo1.c_str(), filelist[selected].epgInfo2.c_str());
	
	std::string buffer;
	
	// prepare print buffer  
	buffer = filelist[selected].epgInfo1;
	buffer += "\n";
	buffer += filelist[selected].epgInfo2;
	buffer += "\n";

	// tfile
	int picw = 320;
	int pich = 256;

	int p_w = 0;
	int p_h = 0;
	int nbpp = 0;	

	if(!access(filelist[selected].tfile.c_str(), F_OK))
	{
		CFrameBuffer::getInstance()->getSize(filelist[selected].tfile, &p_w, &p_h, &nbpp);

		if(p_w <= picw && p_h <= pich)
		{
			picw = p_w;
			pich = p_h;
		}
		else
		{
			float aspect = (float)(p_w) / (float)(p_h);
					
			if (((float)(p_w) / (float)picw) > ((float)(p_h) / (float)pich)) 
			{
				p_w = picw;
				p_h = (int)(picw / aspect);
			}
			else
			{
				p_h = pich;
				p_w = (int)(pich * aspect);
			}
		}
	}
	
	CBox position(g_settings.screen_StartX + 50, g_settings.screen_StartY + 50, g_settings.screen_EndX - g_settings.screen_StartX - 100, g_settings.screen_EndY - g_settings.screen_StartY - 100); 
	
	CInfoBox * infoBox = new CInfoBox(buffer.c_str(), g_Font[SNeutrinoSettings::FONT_TYPE_EPG_INFO1], CTextBox::SCROLL, &position, filelist[selected].epgTitle.c_str(), g_Font[SNeutrinoSettings::FONT_TYPE_EPG_TITLE], NEUTRINO_ICON_MOVIE);

	infoBox->setText(&buffer, filelist[selected].tfile, p_w, p_h);
	infoBox->exec();
	delete infoBox;
}

int CMoviePlayerGui::showStartPosSelectionMenu(void)
{
	dprintf(DEBUG_INFO, "CMoviePlayerGui::showStartPosSelectionMenu\r\n");
	
	int pos = -1;
	int result = 0;
	int menu_nr = 0;
	int position[MAX_NUMBER_OF_BOOKMARK_ITEMS];
	
	if(filelist[selected].bookmarks.lastPlayStop == 0 /*|| filelist[selected].bookmarks.start == 0*/)
		return(result);

	// reset all start pos
	filelist[selected].bookmarks.start = 0;
	
	char start_pos[13]; 
	snprintf(start_pos, 12,"%3d min", filelist[selected].bookmarks.start/60);
	
	char play_pos[13]; 	
	snprintf(play_pos, 12,"%3d min", filelist[selected].bookmarks.lastPlayStop/60); 
	
	char book[MI_MOVIE_BOOK_USER_MAX][20];

	CMenuWidget startPosSelectionMenu(LOCALE_MOVIEBROWSER_START_HEAD , NEUTRINO_ICON_STREAMING);
	startPosSelectionMenu.enableSaveScreen(true);
	startPosSelectionMenu.disableMenuPosition();
	
	// intros
	//WARNING: dont delete this line , without getselected line return line - 1
	startPosSelectionMenu.addItem(new CMenuSeparator(CMenuSeparator::EMPTY));
	
	// bookmark start
	if(filelist[selected].bookmarks.start != 0)
	{
		startPosSelectionMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_MOVIESTART, true, start_pos));
		position[menu_nr++] = filelist[selected].bookmarks.start;
	}
	
	// bookmark laststop
	if(filelist[selected].bookmarks.lastPlayStop != 0) 
	{
		startPosSelectionMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_BOOK_LASTMOVIESTOP, true, play_pos));
		position[menu_nr++] = filelist[selected].bookmarks.lastPlayStop;
	}
	
	// movie start
	startPosSelectionMenu.addItem(new CMenuForwarder(LOCALE_MOVIEBROWSER_START_RECORD_START, true, NULL));
	position[menu_nr++] = 0;

	int sep_pos = menu_nr;

	for(int i = 0 ; i < MI_MOVIE_BOOK_USER_MAX && menu_nr < MAX_NUMBER_OF_BOOKMARK_ITEMS; i++ )
	{
		if( filelist[selected].bookmarks.user[i].pos != 0 )
		{
			if(filelist[selected].bookmarks.user[i].length >= 0)
				position[menu_nr] = filelist[selected].bookmarks.user[i].pos;
			else
				position[menu_nr] = filelist[selected].bookmarks.user[i].pos + filelist[selected].bookmarks.user[i].length;
				
			snprintf(book[i], 19,"%5d min", position[menu_nr]/60);
			dprintf(DEBUG_NORMAL, "CMoviePlayerGui::showStartPosSelectionMenu adding boomark menu N %d, position %d\n", menu_nr, position[menu_nr]);
			
			startPosSelectionMenu.addItem(new CMenuSeparator(CMenuSeparator::LINE));
			startPosSelectionMenu.addItem(new CMenuForwarder(filelist[selected].bookmarks.user[i].name.c_str(), true, book[i]));
			menu_nr++;
		}
	}

	startPosSelectionMenu.exec(NULL, "12345");
	
	// check what menu item was ok'd  and set the appropriate play offset*/
	result = startPosSelectionMenu.getSelectedLine();
	
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::showStartPosSelectionMenu: result %d\n", result);
	
	if(result < 0)
		return -1;
	
	if(result != 0 && result <= MAX_NUMBER_OF_BOOKMARK_ITEMS)
	{
		result--;
		if(result > sep_pos) 
			result--;
		
		pos = position[result];
	}
	
	dprintf(DEBUG_NORMAL, "CMoviePlayerGui::showStartPosSelectionMenu: selected bookmark %d position %d\n", result, pos);
	
	return(pos) ;
}

// CMovieInfoViewer
#define TIMEOSD_FONT 		SNeutrinoSettings::FONT_TYPE_INFOBAR_CHANNAME
#define TIMEBARH 		38
#define BOXHEIGHT_MOVIEINFO	125
#define BUTTON_BAR_HEIGHT	25
#define TIMESCALE_BAR_HEIGHT	7

CMovieInfoViewer::CMovieInfoViewer()
{
	frameBuffer = CFrameBuffer::getInstance();

	visible = false;
	m_mode = MODE_ASC;
	GetDimensions();
}

CMovieInfoViewer::~CMovieInfoViewer()
{
	hide();
}

void CMovieInfoViewer::show(const std::string _Title, const std::string _Info, short _Percent, const unsigned int _ac3state, const int _speed, const int _playstate, bool _show_bookmark)
{
	// show / update
	GetDimensions();
	
	visible = true;

	//FIXME
#if !defined (PLATFORM_GIGABLUE)
	showMovieInfo(_Title, _Info, _Percent, _ac3state, _speed, _playstate, _show_bookmark);
#endif
}

void CMovieInfoViewer::GetDimensions()
{
	// time
	m_xstart = g_settings.screen_StartX + 10;
	m_xend = g_settings.screen_EndX - 10;
	m_height = g_Font[TIMEOSD_FONT]->getHeight();
	m_y = g_settings.screen_StartY + 10;
	m_width = g_Font[TIMEOSD_FONT]->getRenderWidth("00:00:00");
	twidth = m_xend - m_xstart;
	
	// infobar
	BoxStartX = m_xstart;
	BoxWidth = m_xend - m_xstart;
	BoxHeight = TIMEBARH * 3;
	BoxStartY = g_settings.screen_EndY - BoxHeight - 10;
	BoxEndY = BoxStartY + BoxHeight;
	BoxEndX = m_xend;

	// movieinfo
	cFrameBoxInfo.iHeight = BOXHEIGHT_MOVIEINFO;
	cFrameBoxInfo.iWidth = g_settings.screen_EndX - g_settings.screen_StartX - BORDER_LEFT - BORDER_RIGHT;
	cFrameBoxInfo.iX = g_settings.screen_StartX + 10;
	cFrameBoxInfo.iY = g_settings.screen_EndY - 10 - cFrameBoxInfo.iHeight - SHADOW_OFFSET;

	//movieinfo buttonbar
	cFrameBoxButton.iWidth = cFrameBoxInfo.iWidth;
	cFrameBoxButton.iHeight = BUTTON_BAR_HEIGHT;
	cFrameBoxButton.iX = g_settings.screen_StartX + 10;
	cFrameBoxButton.iY = cFrameBoxInfo.iY + cFrameBoxInfo.iHeight - cFrameBoxButton.iHeight;
}

void CMovieInfoViewer::update(time_t time_show)
{
	time_t tDisplayTime;
	static time_t oldDisplayTime = 0;
	char cDisplayTime[8 + 1];
	fb_pixel_t color1, color2;
	
	GetDimensions();

	//dprintf(DEBUG_NORMAL, "CMovieInfoViewer::update time %ld\n", time_show);
	
	if(!visible)
		return;

	if(m_mode == MODE_ASC) 
	{
		color1 = COL_MENUCONTENT_PLUS_0;
		color2 = COL_MENUCONTENT;
	} 
	else 
	{
		color1 = COL_MENUCONTENTSELECTED_PLUS_0;
		color2 = COL_MENUCONTENTSELECTED;
		
		if(!time_show) 
			time_show = 1;
	}

	if(time_show) 
	{
		m_time_show = time_show;
		tDisplayTime = m_time_show;
	} 
	else 
	{
		if(m_mode == MODE_ASC) 
		{
			tDisplayTime = m_time_show + (time(NULL) - m_time_dis);
		} 
		else 
		{
			tDisplayTime = m_time_show + (m_time_dis - time(NULL));
		}
	}

	if(tDisplayTime < 0)
		tDisplayTime = 0;

	if(tDisplayTime != oldDisplayTime) 
	{
		oldDisplayTime = tDisplayTime;
		strftime(cDisplayTime, 9, "%T", gmtime(&tDisplayTime));
		
		// time shadow
		frameBuffer->paintBoxRel(m_xend - m_width - 10 + SHADOW_OFFSET, m_y + SHADOW_OFFSET, m_width + 10, m_height, COL_INFOBAR_SHADOW_PLUS_0);

		// time window
		frameBuffer->paintBoxRel(m_xend - m_width - 10, m_y, m_width + 10, m_height, color1 );

		// time
		g_Font[TIMEOSD_FONT]->RenderString(m_xend - m_width - 5, m_y + m_height, m_width + 5, cDisplayTime, color2);
	}
	
	frameBuffer->blit();
}

void CMovieInfoViewer::hide()
{
	GetDimensions();
	
	//printf("CMovieInfoViewer::hide: x %d y %d xend %d yend %d\n", m_xstart, m_y , m_xend, m_height + 15);

	if(!visible)
		return;

	// hide time
	frameBuffer->paintBackgroundBoxRel(m_xend - m_width - 10, m_y, m_width + 10 + SHADOW_OFFSET, m_height + SHADOW_OFFSET );

	visible = false;

	// hide infoviewer
	frameBuffer->paintBackgroundBoxRel(cFrameBoxInfo.iX, cFrameBoxInfo.iY, cFrameBoxInfo.iWidth + SHADOW_OFFSET, cFrameBoxInfo.iHeight + SHADOW_OFFSET);

	frameBuffer->blit();
}

//showMovieInfo
void CMovieInfoViewer::showMovieInfo(std::string Title, std::string Info, short Percent, const unsigned int ac3state, const int speed, const int playstate, bool show_bookmark)
{
	//dprintf(DEBUG_NORMAL, "CMovieInfoViewer::showMovieInfo:\n");

	int runningPercent = 0;
	
	// icons dimension
	frameBuffer->getIconSize(NEUTRINO_ICON_16_9, &icon_w_aspect, &icon_h_aspect);
	frameBuffer->getIconSize(NEUTRINO_ICON_DD, &icon_w_dd, &icon_h_dd);
	
	// colored user icons
	frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_RED, &icon_red_w, &icon_red_h);
	frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_GREEN, &icon_green_w, &icon_green_h);
	frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_YELLOW, &icon_yellow_w, &icon_yellow_h);
	frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_BLUE, &icon_blue_w, &icon_blue_h);
	
	// init progressbar
	CProgressBar moviescale(cFrameBoxInfo.iWidth - BORDER_LEFT - BORDER_RIGHT, TIMESCALE_BAR_HEIGHT);
	
	moviescale.reset();
	
	// paint shadow
	frameBuffer->paintBoxRel(cFrameBoxInfo.iX + SHADOW_OFFSET, cFrameBoxInfo.iY + SHADOW_OFFSET, cFrameBoxInfo.iWidth, cFrameBoxInfo.iHeight, COL_INFOBAR_SHADOW_PLUS_0, RADIUS_MID, CORNER_BOTH );
		
	// paint info box
	frameBuffer->paintBoxRel(cFrameBoxInfo.iX, cFrameBoxInfo.iY, cFrameBoxInfo.iWidth, cFrameBoxInfo.iHeight, COL_INFOBAR_PLUS_0, RADIUS_MID, CORNER_TOP, g_settings.infobar_gradient); 
		
	// bottum bar
	frameBuffer->paintBoxRel(cFrameBoxButton.iX, cFrameBoxButton.iY, cFrameBoxButton.iWidth, cFrameBoxButton.iHeight, COL_INFOBAR_SHADOW_PLUS_1,  RADIUS_MID, CORNER_BOTTOM); 
	
	// show date/time
	std::string datestr = getNowTimeStr("%d.%m.%Y %H:%M");
			
	int widthtime = g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->getRenderWidth(datestr.c_str(), true); //UTF-8
	int height = g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->getHeight();
			
	g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->RenderString(cFrameBoxInfo.iX + cFrameBoxInfo.iWidth - BORDER_RIGHT - widthtime, cFrameBoxInfo.iY + 5 + height, widthtime, datestr.c_str(), COL_INFOBAR, 0, true); // UTF-8
	
	std::string title = "Movie Player";
	int widthtitle = g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->getRenderWidth(title.c_str(), true); //UTF-8
	g_Font[SNeutrinoSettings::FONT_TYPE_EPG_INFO1]->RenderString(cFrameBoxInfo.iX + BORDER_LEFT, cFrameBoxInfo.iY + 5 + height, widthtitle, (char *)title.c_str(), COL_INFOBAR, 0, true); // UTF-8
	
	// mp icon
	int m_icon_w = 0;
	int m_icon_h = 0;
	
	std::string IconName = DATADIR "/neutrino/icons/" NEUTRINO_ICON_MP ".png";
	
	if(!access(IconName.c_str(), F_OK))
	{
		frameBuffer->getIconSize(NEUTRINO_ICON_MP, &m_icon_w, &m_icon_h);

		int m_icon_x = cFrameBoxInfo.iX + BORDER_LEFT;
		int m_icon_y = cFrameBoxInfo.iY + 30 + TIMESCALE_BAR_HEIGHT + (cFrameBoxInfo.iHeight - 30 - TIMESCALE_BAR_HEIGHT - cFrameBoxButton.iHeight - m_icon_h) / 2;
		
		frameBuffer->paintIcon(NEUTRINO_ICON_MP, m_icon_x, m_icon_y);
	}
	
	// red (movie info)
	int icon_w, icon_h;
	frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_RED, &icon_w, &icon_h);
	frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_RED, cFrameBoxButton.iX + BORDER_LEFT, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);

	g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->RenderString(cFrameBoxButton.iX + BORDER_LEFT + icon_w + ICON_OFFSET, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->getHeight())/2 + g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->getHeight(), cFrameBoxButton.iWidth/5, (char *)"Info", (COL_INFOBAR_SHADOW + 1), 0, true); // UTF-8
		
	// green (AV select)
	frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_GREEN, &icon_w, &icon_h);
	frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_GREEN, cFrameBoxButton.iX + cFrameBoxButton.iWidth/5, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);

	g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->RenderString(cFrameBoxButton.iX + (cFrameBoxButton.iWidth/5) + icon_w + ICON_OFFSET, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->getHeight())/2 + g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->getHeight(), cFrameBoxButton.iWidth/5, g_Locale->getText(LOCALE_INFOVIEWER_LANGUAGES), (COL_INFOBAR_SHADOW + 1), 0, true); // UTF-8
		
	// yellow (help)
	frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_YELLOW, &icon_w, &icon_h);
	frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_YELLOW, cFrameBoxButton.iX + (cFrameBoxButton.iWidth/5)*2, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);

		g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->RenderString(cFrameBoxButton.iX + (cFrameBoxButton.iWidth/5)*2 + icon_w + ICON_OFFSET, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->getHeight())/2 + g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->getHeight(), cFrameBoxButton.iWidth/5, (char *)"help", (COL_INFOBAR_SHADOW * 1), 0, true); // UTF-8
	
	// blue (bookmark)
	if (show_bookmark)
	{
		frameBuffer->getIconSize(NEUTRINO_ICON_BUTTON_BLUE, &icon_w, &icon_h);
		frameBuffer->paintIcon(NEUTRINO_ICON_BUTTON_BLUE, cFrameBoxButton.iX + (cFrameBoxButton.iWidth/5)*3, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);

		g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->RenderString(cFrameBoxButton.iX + (cFrameBoxButton.iWidth/5)*3 + icon_w + ICON_OFFSET, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->getHeight())/2 + g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_SMALL]->getHeight(), cFrameBoxButton.iWidth/5, g_Locale->getText(LOCALE_MOVIEPLAYER_BOOKMARK), (COL_INFOBAR_SHADOW + 1), 0, true); // UTF-8
	}
		
	// ac3
	int icon_w_ac3, icon_h_ac3;
	frameBuffer->getIconSize(NEUTRINO_ICON_DD, &icon_w_ac3, &icon_h_ac3);
	frameBuffer->paintIcon( (ac3state == CInfoViewer::AC3_ACTIVE)?NEUTRINO_ICON_DD : NEUTRINO_ICON_DD_GREY, cFrameBoxButton.iX + cFrameBoxButton.iWidth - BORDER_RIGHT - icon_w_ac3, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h_ac3)/2);
		
	// 4:3/16:9
	const char * aspect_icon = NEUTRINO_ICON_16_9_GREY;
				
	if(g_settings.video_Ratio == ASPECTRATIO_169)
		aspect_icon = NEUTRINO_ICON_16_9;
	
	int icon_w_asp, icon_h_asp;
	frameBuffer->getIconSize(aspect_icon, &icon_w_asp, &icon_h_asp);
	frameBuffer->paintIcon(aspect_icon, cFrameBoxButton.iX + cFrameBoxButton.iWidth - BORDER_RIGHT - icon_w_ac3 - ICON_TO_ICON_OFFSET - icon_w_asp, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h_asp)/2);
	
	// mp keys
	frameBuffer->getIconSize(NEUTRINO_ICON_FF_SMALL, &icon_w, &icon_h);
		
	frameBuffer->paintIcon(NEUTRINO_ICON_REW_SMALL, cFrameBoxButton.iX + cFrameBoxButton.iWidth - BORDER_RIGHT - icon_w_ac3 - ICON_TO_ICON_OFFSET - icon_w_asp - ICON_TO_ICON_OFFSET - 5*icon_w, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);

	frameBuffer->paintIcon(NEUTRINO_ICON_PLAY_SMALL, cFrameBoxButton.iX + cFrameBoxButton.iWidth - BORDER_RIGHT - icon_w_ac3 - ICON_TO_ICON_OFFSET - icon_w_asp - ICON_TO_ICON_OFFSET - 4*icon_w, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);

	frameBuffer->paintIcon(NEUTRINO_ICON_PAUSE_SMALL, cFrameBoxButton.iX + cFrameBoxButton.iWidth - BORDER_RIGHT - icon_w_ac3 - ICON_TO_ICON_OFFSET - icon_w_asp - ICON_TO_ICON_OFFSET - 3*icon_w, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);

	frameBuffer->paintIcon(NEUTRINO_ICON_STOP_SMALL, cFrameBoxButton.iX + cFrameBoxButton.iWidth - BORDER_RIGHT - icon_w_ac3 - ICON_TO_ICON_OFFSET - icon_w_asp - ICON_TO_ICON_OFFSET - 2*icon_w, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);

	frameBuffer->paintIcon(NEUTRINO_ICON_FF_SMALL, cFrameBoxButton.iX + cFrameBoxButton.iWidth - BORDER_RIGHT - icon_w_ac3 - ICON_TO_ICON_OFFSET - icon_w_asp - ICON_TO_ICON_OFFSET - icon_w, cFrameBoxButton.iY + (cFrameBoxButton.iHeight - icon_h)/2);
		
	//playstate
	const char *icon = NEUTRINO_ICON_PLAY;
		
	switch(playstate)
	{
		case CMoviePlayerGui::PAUSE: icon = NEUTRINO_ICON_PAUSE; break;
		case CMoviePlayerGui::PLAY: icon = NEUTRINO_ICON_PLAY; break;
		case CMoviePlayerGui::REW: icon = NEUTRINO_ICON_REW; break;
		case CMoviePlayerGui::FF: icon = NEUTRINO_ICON_FF; break;
		case CMoviePlayerGui::SOFTRESET: break;
		case CMoviePlayerGui::SLOW: break;
		case CMoviePlayerGui::STOPPED: break;
	}
	
	frameBuffer->getIconSize(icon, &icon_w, &icon_h);

	//
	int icon_x = cFrameBoxButton.iX + BORDER_LEFT + m_icon_w + ICON_OFFSET;
	int icon_y = cFrameBoxInfo.iY + 30 + TIMESCALE_BAR_HEIGHT + (cFrameBoxInfo.iHeight - 30 - TIMESCALE_BAR_HEIGHT - cFrameBoxButton.iHeight - icon_h) / 2;

	frameBuffer->paintIcon(icon, icon_x, icon_y);
		
	// paint speed
	char strSpeed[4];
	if( playstate == CMoviePlayerGui::FF || playstate == CMoviePlayerGui::REW )
	{
		sprintf(strSpeed, "%d", speed);
		
		//FIXME:??? position
		g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_NUMBER]->RenderString(icon_x + icon_w + ICON_OFFSET, icon_y + (icon_h - g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_NUMBER]->getHeight())/2 + g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_NUMBER]->getHeight(), BoxWidth/5, strSpeed, COL_INFOBAR ); // UTF-8
	}
		
	int speedWidth = g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->getRenderWidth("-8");
		
	int InfoStartX = cFrameBoxInfo.iX + ICON_OFFSET + m_icon_w + BORDER_LEFT + icon_w + ICON_OFFSET + speedWidth + 2*ICON_OFFSET;
	int InfoWidth = cFrameBoxInfo.iWidth - BORDER_LEFT - BORDER_RIGHT -2*ICON_OFFSET - m_icon_w - icon_w - speedWidth - 2*ICON_OFFSET - 2*BORDER_LEFT;
	
	// title
	int TitleHeight = cFrameBoxInfo.iY + 30 + TIMESCALE_BAR_HEIGHT + (cFrameBoxInfo.iHeight - (30 + TIMESCALE_BAR_HEIGHT + cFrameBoxButton.iHeight) -2*g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->getHeight())/2 + g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->getHeight();	//40???
	
	// Title	
	g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->RenderString(InfoStartX, TitleHeight, InfoWidth, (char *)Title.c_str(), COL_INFOBAR, 0, true);

	// Info
	g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->RenderString(InfoStartX, g_Font[SNeutrinoSettings::FONT_TYPE_INFOBAR_INFO]->getHeight() + TitleHeight, InfoWidth, (char *)Info.c_str(), COL_INFOBAR, 0, true);
	
	// progressbar
	runningPercent = Percent;
	
	if(Percent < 0)
		runningPercent = 0;
	
	if(runningPercent > 100)
		runningPercent = 100;
	
	moviescale.paint(cFrameBoxInfo.iX + BORDER_LEFT, cFrameBoxInfo.iY + 30, runningPercent);
	
	frameBuffer->blit();
}


