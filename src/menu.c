/* 
   Florence - Florence is a simple virtual keyboard for Gnome.

   Copyright (C) 2014 François Agrech

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

#include "system.h"
#include "trace.h"
#include "settings.h"
#include "florence.h"
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* Display the about dialog window */
void menu_about(void)
{
	START_FUNC
	gchar *authors[] = {
		"François Agrech <f.agrech@gmail.com>",
		"Devin Teske <dteske@FreeBSD.org>",
		"Pietro Pilolli <alpha@paranoici.org>",
       		"Arnaud Andoval <arnaudsandoval@gmail.com>",
		"Stéphane Ancelot <sancelot@free.fr>",
		"Laurent Bessard <laurent.bessard@gmail.com>", NULL};
	gtk_show_about_dialog(NULL, "program-name", _("Florence Virtual Keyboard"),
		"version", VERSION,
		"copyright", _("Copyright (C) 2008-2012 François Agrech\n"
		    "Copyright (C) 2026 Devin Teske"),
		"logo", gdk_pixbuf_new_from_file(ICONDIR "/florence.svg", NULL),
		"website", "https://github.com/FrauBSD/florence",
		"website-label", _("Project home"),
		"authors", authors,
		"license", _("Copyright (C) 2008-2012 François Agrech\n"
		    "Copyright (C) 2026 Devin Teske\n"
		    "\n"
		    "This program is free software; you can redistribute it and/or modify\n"
		    "it under the terms of the GNU General Public License as published by\n"
		    "the Free Software Foundation; either version 2, or (at your option)\n"
		    "any later version.\n"
		    "\n"
		    "This program is distributed in the hope that it will be useful,\n"
		    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		    "GNU General Public License for more details.\n"
		    "\n"
		    "You should have received a copy of the GNU General Public License\n"
		    "along with this program; if not, write to the Free Software Foundation,\n"
		    "Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA."),
		NULL);
	END_FUNC
}

#ifdef ENABLE_HELP
/* Open yelp */
void menu_help(void)
{
	START_FUNC
#if GTK_CHECK_VERSION(2,14,0)
	GError *error=NULL;
	gtk_show_uri(NULL, "ghelp:florence", gtk_get_current_event_time(), &error);
	if (error) flo_error(_("Unable to open %s"), "ghelp:florence");
#else
	if (!gnome_help_display_uri("ghelp:florence", NULL)) {
		flo_error(_("Unable to open %s"), "ghelp:florence");
	}
#endif
	END_FUNC
}
#endif

/* Build a trigger event for menu popup (DBus path has no current GdkEvent). */
static GdkEvent *
menu_trigger_event(guint32 time)
{
	GdkEvent *event;
	GdkDevice *pointer;
	GdkWindow *window;
	gint x, y, rx, ry;

	event = gtk_get_current_event();
	if (event)
		return event;

	pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(
	    gdk_display_get_default()));
	if (!pointer)
		return NULL;
	window = gdk_device_get_window_at_position(pointer, &x, &y);
	if (!window)
		return NULL;

	event = gdk_event_new(GDK_BUTTON_RELEASE);
	event->button.window = g_object_ref(window);
	event->button.send_event = TRUE;
	event->button.time = time ? time : GDK_CURRENT_TIME;
	event->button.button = 3;
	event->button.device = pointer;
	event->button.x = x;
	event->button.y = y;
	gdk_window_get_root_coords(window, x, y, &rx, &ry);
	event->button.x_root = rx;
	event->button.y_root = ry;
	return event;
}

/* Called when the icon is right->clicked
 * Displays the menu. */
void menu_show(GCallback quit_func, gpointer user_data, guint32 time)
{
	START_FUNC
	GtkWidget *menu, *about, *config, *quit;
	GdkEvent *event;
#ifdef ENABLE_HELP
	GtkWidget *help;
#endif

	if (florence_in_greeter()) {
		/* XDM greeter: never Preferences / Quit / About. */
		(void)quit_func;
		(void)user_data;
		(void)time;
		END_FUNC
		return;
	}

	menu=gtk_menu_new();

	quit=gtk_menu_item_new_with_mnemonic(_("_Quit"));
	g_signal_connect_swapped(quit, "activate", quit_func, user_data);

#ifdef ENABLE_HELP
	help=gtk_menu_item_new_with_mnemonic(_("_Help"));
	g_signal_connect(help, "activate", G_CALLBACK(menu_help), NULL);
#endif

	about=gtk_menu_item_new_with_mnemonic(_("_About"));
	g_signal_connect(about, "activate", G_CALLBACK(menu_about), NULL);

	config=gtk_menu_item_new_with_mnemonic(_("_Preferences"));
	g_signal_connect(config, "activate", G_CALLBACK(settings), NULL);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), config);
#ifdef ENABLE_HELP
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), help);
#endif
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), about);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
	gtk_widget_show_all(menu);

	event = menu_trigger_event(time);
	if (event) {
		gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
		gdk_event_free(event);
	} else {
		/* No window under the pointer (rare); keep time-based popup. */
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 3,
		    time ? time : gtk_get_current_event_time());
		G_GNUC_END_IGNORE_DEPRECATIONS
	}
	END_FUNC
}
