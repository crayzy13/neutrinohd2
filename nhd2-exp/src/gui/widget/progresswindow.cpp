/*
	Neutrino-GUI  -   DBoxII-Project
	
	$Id: progresswindow.cpp 2013/10/12 mohousch Exp $

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/

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

#include <global.h>
#include <neutrino.h>

#include <driver/fontrenderer.h>
#include <driver/rcinput.h>
#include <driver/screen_max.h>

#include <gui/color.h>

#include <system/debug.h>

#include <gui/widget/progresswindow.h>


CProgressWindow::CProgressWindow()
{
	frameBuffer = CFrameBuffer::getInstance();

	caption = NONEXISTANT_LOCALE;
	captionString = "";
	
	hheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU_TITLE]->getHeight();
	mheight = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getHeight();
	width = w_max (600, 0);
	height = h_max(hheight + 3*mheight, 20);

	global_progress = 101;
	statusText = "";

	x = frameBuffer->getScreenX() + ((frameBuffer->getScreenWidth() - width ) >> 1 );
	y = frameBuffer->getScreenY() + ((frameBuffer->getScreenHeight() - height) >> 1 );
	
	// box
	m_cBoxWindow.setDimension(x, y, width, height);

	// title
	m_cTitleWindow.setDimension(x, y, width, hheight);

	// progressbar
	progressBar = new CProgressBar(width - BORDER_LEFT - BORDER_RIGHT - g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getRenderWidth("100%") - 10, 10);

	progressBar->reset();
}

void CProgressWindow::setTitle(const neutrino_locale_t title)
{
	caption = title;
	captionString = g_Locale->getText(caption);

#ifdef LCD_UPDATE
	CVFD::getInstance()->showProgressBar2(-1, NULL, -1, g_Locale->getText(caption)); // set global text in VFD
#endif // VFD_UPDATE
}

void CProgressWindow::setTitle(const char * const title)
{
	caption = NONEXISTANT_LOCALE;
	captionString = title;

#ifdef LCD_UPDATE
	CVFD::getInstance()->showProgressBar2(-1, NULL, -1, captionString.c_str()); // set global text in VFD
#endif // VFD_UPDATE
}

void CProgressWindow::showGlobalStatus(const unsigned int prog)
{
	if (global_progress == prog)
		return;

	global_progress = prog;

	int pos = x + 10;
	
	char strProg[5] = "100%";
	int w = g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getRenderWidth(strProg);

	sprintf(strProg, "%d%%", global_progress);

	if (global_progress > 100)
		global_progress = 100;

	pos += int( float(width - w - 20)/100.0 * global_progress);
		
	// refresh Box (%)
	frameBuffer->paintBoxRel(x + width - (w + 20), globalstatusY - 5, w + 20, g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->getHeight() + 8, COL_MENUCONTENT_PLUS_0);

	g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->RenderString(x + width - (w + 10), globalstatusY + 18, w, strProg, COL_MENUCONTENT, 0, true); // UTF-8

	// progressBar
	progressBar->paint(x + BORDER_LEFT, globalstatusY, global_progress);
	
	frameBuffer->blit();

#ifdef LCD_UPDATE
	CVFD::getInstance()->showProgressBar2(-1, NULL, global_progress);
#endif // VFD_UPDATE
}

void CProgressWindow::showStatusMessageUTF(const std::string & text)
{
	statusText = text;
	frameBuffer->paintBox(x, statusTextY - mheight, x + width, statusTextY, COL_MENUCONTENT_PLUS_0);
	g_Font[SNeutrinoSettings::FONT_TYPE_MENU]->RenderString(x + 10, statusTextY, width - 20, text, COL_MENUCONTENT, 0, true); // UTF-8
	
	frameBuffer->blit();

#ifdef LCD_UPDATE
	CVFD::getInstance()->showProgressBar2(-1, text.c_str()); // set local text in VFD
#endif // VFD_UPDATE
}

unsigned int CProgressWindow::getGlobalStatus(void)
{
	return global_progress;
}


void CProgressWindow::hide()
{
	m_cBoxWindow.hide();

	delete progressBar;
	progressBar = NULL;	
}

void CProgressWindow::paint()
{
	dprintf(DEBUG_NORMAL, "CProgressWindow::paint\n");

	// title
	int ypos = y;

	// box
	m_cBoxWindow.enableShadow();
	m_cBoxWindow.enableSaveScreen();
	m_cBoxWindow.setColor(COL_MENUCONTENT_PLUS_0);
	m_cBoxWindow.setCorner(RADIUS_MID, CORNER_ALL);
	m_cBoxWindow.paint();
	
	// title
	m_cTitleWindow.setColor(COL_MENUHEAD_PLUS_0);
	m_cTitleWindow.setCorner(RADIUS_MID, CORNER_TOP);
	m_cTitleWindow.setGradient(g_settings.Head_gradient);
	m_cTitleWindow.paint();
	
	// icon
	int icon_w = 0;
	int icon_h = 0;
	
	frameBuffer->getIconSize(NEUTRINO_ICON_INFO, &icon_w, &icon_h);
	frameBuffer->paintIcon(NEUTRINO_ICON_INFO, x + BORDER_LEFT, ypos + (hheight - icon_h)/2);
	
	// caption
	const char * l_caption;
	if (caption != NONEXISTANT_LOCALE)
		l_caption = g_Locale->getText(caption);
	else
		l_caption = captionString.c_str();
	  
	g_Font[SNeutrinoSettings::FONT_TYPE_MENU_TITLE]->RenderString(x + BORDER_LEFT + icon_w + ICON_OFFSET, ypos + hheight, width - BORDER_LEFT - BORDER_RIGHT - ICON_OFFSET, l_caption, COL_MENUHEAD, 0, true); // UTF-8

	// msg status
	ypos += hheight + (mheight>>1);
	statusTextY = ypos + mheight;

	showStatusMessageUTF(statusText);

	// global status
	ypos += mheight;
	globalstatusY = ypos + mheight - 20;
	ypos += mheight >>1;

	showGlobalStatus(global_progress);
}

int CProgressWindow::exec(CMenuTarget* parent, const std::string &/*actionKey*/)
{
	dprintf(DEBUG_NORMAL, "CProgressWindow::exec\n");

	if(parent)
		parent->hide();
	
	paint();
	
	frameBuffer->blit();

	return menu_return::RETURN_REPAINT;
}
