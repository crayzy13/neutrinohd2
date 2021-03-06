/*
  $Id: mediaplayer.cpp 2014/01/25 mohousch Exp $

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

#include <plugin.h>


extern "C" void plugin_exec(void);
extern "C" void plugin_init(void);
extern "C" void plugin_del(void);

void plugin_init(void)
{
}

void plugin_del(void)
{
}

void plugin_exec(void)
{
	CMoviePlayerGui tmpMoviePlayerGui;
					
	CFileBrowser * fileBrowser;
	
	fileBrowser = new CFileBrowser();
	
	CFileFilter fileFilter;
	
	fileFilter.addFilter("ts");
	fileFilter.addFilter("mpg");
	fileFilter.addFilter("mpeg");
	fileFilter.addFilter("divx");
	fileFilter.addFilter("avi");
	fileFilter.addFilter("mkv");
	fileFilter.addFilter("asf");
	fileFilter.addFilter("aiff");
	fileFilter.addFilter("m2p");
	fileFilter.addFilter("mpv");
	fileFilter.addFilter("m2ts");
	fileFilter.addFilter("vob");
	fileFilter.addFilter("mp4");
	fileFilter.addFilter("mov");	
	fileFilter.addFilter("flv");	
	fileFilter.addFilter("dat");
	fileFilter.addFilter("trp");
	fileFilter.addFilter("vdr");
	fileFilter.addFilter("mts");
	fileFilter.addFilter("wmv");
	fileFilter.addFilter("wav");
	fileFilter.addFilter("flac");
	fileFilter.addFilter("mp3");
	fileFilter.addFilter("wma");
	fileFilter.addFilter("ogg");

	fileBrowser->Multi_Select = true;
	fileBrowser->Filter = &fileFilter;
	
	std::string Path_local = g_settings.network_nfs_moviedir;

BROWSER:
	if (fileBrowser->exec(Path_local.c_str()))
	{
		Path_local = fileBrowser->getCurrentDir();
		MI_MOVIE_INFO mfile;
		CMovieInfo cMovieInfo;

		CFileList::const_iterator files = fileBrowser->getSelectedFiles().begin();
		for(; files != fileBrowser->getSelectedFiles().end(); files++)
		{
			cMovieInfo.clearMovieInfo(&mfile);

			mfile.file.Name = files->Name;

			// info if there is xml file|skip audio files
			if(mfile.file.getType() != CFile::FILE_AUDIO)
				cMovieInfo.loadMovieInfo(&mfile, &mfile.file);

			// epgTitle
			if(mfile.epgTitle.empty())
			{
				std::string Title = files->getFileName();
				removeExtension(Title);
				mfile.epgTitle = Title;
			}

			// tfile
			std::string fname = files->Name;
			changeFileNameExt(fname,".jpg");	
			if(!access(fname.c_str(), F_OK) )
				mfile.tfile= fname.c_str();

			// epgInfo2
			if(mfile.file.getType() != CFile::FILE_AUDIO)
			{
	 			if(g_settings.prefer_tmdb_info)
				{
					cTmdb * tmdb = new cTmdb(mfile.epgTitle);
	
					// epgInfo2
					if(mfile.epgInfo2.empty() && !tmdb->getDescription().empty())
					{
						mfile.epgInfo2 = tmdb->getDescription();
					}
					delete tmdb;
					tmdb = NULL;
				}
			}
					
			tmpMoviePlayerGui.addToPlaylist(mfile);
		}
		
		tmpMoviePlayerGui.exec(NULL, "urlplayback");
		
		neutrino_msg_t msg;
		neutrino_msg_data_t data;

		g_RCInput->getMsg_ms(&msg, &data, 10);
		
		if (msg != CRCInput::RC_home) 
		{
			goto BROWSER;
		}
	}
	
	delete fileBrowser;
}
