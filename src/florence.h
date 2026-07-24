/* 
   Florence - Florence is a simple virtual keyboard for Gnome.

   Copyright (C) 2008, 2009, 2010 François Agrech

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  

*/

#ifndef FLORENCE
#define FLORENCE

#include "system.h"
#include "config.h"
#ifdef ENABLE_XKB
	#include <X11/XKBlib.h>
#endif
#include <gtk/gtk.h>
#include "status.h"
#include "trayicon.h"
#ifdef ENABLE_RAMBLE
	#include "ramble.h"
#endif
#include "service.h"
#include "controller.h"

/* XDM greeter mode: same binary, safer UI (no prefs/menu). */
void florence_set_greeter_mode(gboolean on);
gboolean florence_in_greeter(void);

/* There is one florence structure which contains all global data in florence.c */
struct florence {
	struct style *style; /* the style of florence */
	struct view *view; /* the graphical representation of florence on screen */
	GSList *keyboards; /* the keyboard extensions (including main) of florence */
	struct status *status; /* the status of florence */	
	struct trayicon *trayicon; /* tray icon object */
	gint xpos, ypos; /* remember pointer position */
#ifdef ENABLE_RAMBLE
	struct ramble *ramble; /* track the path of the mouse. */
#endif
	struct service *service; /* dbus service object */
	guint to_top_id; /* keep-on-top poll source (0 if none) */
};

/* create a new instance of florence. */
struct florence *flo_new(const gchar *focus_back, void (*ready)(void *));
/* liberate all the memory used by florence */
void flo_free(struct florence *florence);
/* stop florence. */
void flo_terminate(struct florence *florence);

#endif

