/* 
 * florence - Florence is a simple virtual keyboard for Gnome.

 * Copyright (C) 2012 François Agrech

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  

*/

#include "florence.h"
#include "trace.h"
#include "settings.h"
#include "xkeyboard.h"
#include "keyboard.h"
#include "tools.h"
#include "layoutreader.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

/* terminate the program */
void flo_terminate(struct florence *florence)
{
	START_FUNC
	/* release all keys */
	if (!florence->status) {
		flo_warn(_("NULL status detected."));
		return;
	}
	status_pressed_set(florence->status, NULL);

	gtk_main_quit();
	END_FUNC
}

/* Called on destroy event (systray quit or close window) */
void flo_destroy (GtkWidget *widget, gpointer user_data)
{
	START_FUNC
	struct florence *florence=(struct florence *)user_data;
	flo_terminate(florence);
	END_FUNC
}

/* load the keyboards from the layout file into the keyboards member of florence */
GSList *flo_keyboards_load(struct florence *florence, struct layout *layout)
{
	START_FUNC
	GSList *keyboards=NULL;;
	struct keyboard *keyboard=NULL;
	struct keyboard_globaldata global;

	global.status=florence->status;
	florence->status->xkeyboard=xkeyboard_new();
	global.style=florence->style;

	/* read the layout file and create the extensions */
	keyboards=g_slist_append(keyboards, keyboard_new(layout, &global));
#ifdef ENABLE_XRECORD
	status_keys_add(florence->status, ((struct keyboard *)keyboards->data)->keys);
#endif
	while ((keyboard=keyboard_extension_new(layout, &global))) {
		keyboards=g_slist_append(keyboards, keyboard);
#ifdef ENABLE_XRECORD
		status_keys_add(florence->status, keyboard->keys);
#endif
	}

	xkeyboard_client_map_free(florence->status->xkeyboard);
	END_FUNC
	return keyboards;
}

/* handles mouse leave events */
gboolean flo_mouse_leave_event (GtkWidget *window, GdkEvent *event, gpointer user_data)
{
	START_FUNC
	gint x, y;
	struct florence *florence=(struct florence *)user_data;

	status_timer_stop(florence->status);
	if (status_get_moving(florence->status)) {
		/* Keep dragging; seat grab delivers motion outside the window. */
		if (event && event->type == GDK_LEAVE_NOTIFY) {
			x = (gint)((GdkEventCrossing *)event)->x_root;
			y = (gint)((GdkEventCrossing *)event)->y_root;
		} else {
			gdk_device_get_position(gdk_device_manager_get_client_pointer(
				gdk_display_get_device_manager(gdk_display_get_default())),
				NULL, &x, &y);
		}
		gtk_window_move(GTK_WINDOW(window), x-florence->xpos, y-florence->ypos);
		END_FUNC
		return FALSE;
	}
	if (status_get_resizing(florence->status)) {
		/*
		 * Growing/shrinking moves the window edge under a stationary
		 * pointer and synthesizes LeaveNotify. Do not re-apply scale
		 * here — that double-fired with motion and jittered position.
		 * Seat grab still delivers MotionNotify for tracking.
		 */
		END_FUNC
		return FALSE;
	}

	/* Ignore inferior/virtual leaves (crossing child widgets). */
	if (event && event->type == GDK_LEAVE_NOTIFY &&
	    ((GdkEventCrossing *)event)->mode != GDK_CROSSING_NORMAL) {
		END_FUNC
		return FALSE;
	}

	/*
	 * Compositor/WM restacks synthesize LeaveNotify while the pointer is
	 * still over us — clearing hover then made the highlight flicker every
	 * few seconds. Only clear when the pointer is truly outside.
	 */
	if (event && event->type == GDK_LEAVE_NOTIFY) {
		GdkWindow *gw = gtk_widget_get_window(window);
		gint px, py, ox, oy;
		gint ww, wh;
		if (gw) {
			gdk_device_get_position(gdk_device_manager_get_client_pointer(
				gdk_display_get_device_manager(gdk_display_get_default())),
				NULL, &px, &py);
			gdk_window_get_origin(gw, &ox, &oy);
			ww = gdk_window_get_width(gw);
			wh = gdk_window_get_height(gw);
			if (px >= ox && py >= oy &&
			    px < ox + ww && py < oy + wh) {
				END_FUNC
				return FALSE;
			}
		}
	}

	/*
	 * Finger/mouse still down: keep the key held so X auto-repeat continues.
	 * Touch jitter often synthesizes Leave/Motion/"drag"; releasing here
	 * was why hold-to-repeat worked with the mouse but not the finger.
	 * ButtonRelease still arrives via the implicit grab.
	 */
	if (status_pressed_get(florence->status)) {
		END_FUNC
		return FALSE;
	}

	status_focus_set(florence->status, NULL);
	status_pressed_set(florence->status, NULL);
#ifdef ENABLE_RAMBLE
	if (florence->ramble) {
		ramble_reset(florence->ramble, gtk_widget_get_window(GTK_WIDGET(florence->view->window)), NULL);
	}
#endif
	END_FUNC
	return FALSE;
}

/* handles button press events */
gboolean flo_button_press_event (GtkWidget *window, GdkEventButton *event, gpointer user_data)
{
	START_FUNC
	struct florence *florence=(struct florence *)user_data;
	struct key *key=NULL;
	
	if (event) {
		key=status_hit_get(florence->status, (gint)((GdkEventButton*)event)->x,
#ifdef ENABLE_RAMBLE
			(gint)((GdkEventButton*)event)->y, NULL);
#else
			(gint)((GdkEventButton*)event)->y);
#endif
		/* we don't want double and triple click events */
		if ((event->type==GDK_2BUTTON_PRESS) || (event->type==GDK_3BUTTON_PRESS)) {
			END_FUNC
			return FALSE;
		}
		/*
		 * Move handle: always Florence live-move + seat grab.
		 *
		 * Touchscreen gestures feed XTest Button1, which GDK reports as
		 * a mouse — begin_move_drag then leaves Button1 latched after
		 * finger-up and breaks all subsequent 1fg pointer motion.
		 * Physical mouse also uses this path (seat grab keeps motion).
		 *
		 * Resize handle: same seat-grab live-scale drag.
		 */
		if (key && key_get_action(key, florence->status) == KEY_MOVE) {
			GdkWindow *gdkw;
			GdkSeat *seat;

			florence->xpos = (gint)event->x;
			florence->ypos = (gint)event->y;
			status_set_moving(florence->status, TRUE);
			gdkw = gtk_widget_get_window(window);
			seat = gdk_display_get_default_seat(gdk_display_get_default());
			if (gdkw && seat &&
			    gdk_seat_grab(seat, gdkw, GDK_SEAT_CAPABILITY_ALL_POINTING,
				FALSE, NULL, (GdkEvent *)event, NULL, NULL) ==
				GDK_GRAB_SUCCESS)
				florence->status->move_grabbed = TRUE;
			/* Fall through so press state tracks the key. */
		}
		if (key && key_get_action(key, florence->status) == KEY_RESIZE) {
			GdkWindow *gdkw;
			GdkSeat *seat;

			status_set_resizing(florence->status, TRUE);
			florence->status->resize_root_x = (gint)event->x_root;
			florence->status->resize_root_y = (gint)event->y_root;
			/* Seat grab so motion survives keep-on-top + size changes. */
			gdkw = gtk_widget_get_window(window);
			seat = gdk_display_get_default_seat(gdk_display_get_default());
			if (gdkw && seat &&
			    gdk_seat_grab(seat, gdkw, GDK_SEAT_CAPABILITY_ALL_POINTING,
				FALSE, NULL, (GdkEvent *)event, NULL, NULL) ==
				GDK_GRAB_SUCCESS)
				florence->status->resize_grabbed = TRUE;
			/* Fall through so press state tracks the key. */
		}
	} else {
		key=status_focus_get(florence->status);
	}

#ifdef ENABLE_RAMBLE
	if (status_im_get(florence->status)==STATUS_IM_RAMBLE) {
		if (key_get_action(key, florence->status)==KEY_MOVE) {
			status_pressed_set(florence->status, key);
		} else if (ramble_start(florence->ramble,
			gtk_widget_get_window(GTK_WIDGET(florence->view->window)),
			(gint)((GdkEventButton*)event)->x,
			(gint)((GdkEventButton*)event)->y, key)) {
			status_pressed_set(florence->status, key);
			status_pressed_set(florence->status, NULL);
			status_focus_set(florence->status, key);
		}
	} else {
#endif
	status_pressed_set(florence->status, key);
	status_timer_stop(florence->status);
#ifdef ENABLE_RAMBLE
	}
#endif
	END_FUNC
	return FALSE;
}

/* handles button release events */
gboolean flo_button_release_event (GtkWidget *window, GdkEvent *event, gpointer user_data)
{
	START_FUNC
	struct florence *florence=(struct florence *)user_data;
#ifdef ENABLE_RAMBLE
	struct key *key;
#endif
	status_pressed_set(florence->status, NULL);
	status_timer_stop(florence->status);
#ifdef ENABLE_RAMBLE
	if (ramble_started(florence->ramble) &&
		status_im_get(florence->status)==STATUS_IM_RAMBLE &&
		settings_get_bool(SETTINGS_RAMBLE_BUTTON)) {
		key=status_hit_get(florence->status,
			(gint)((GdkEventButton*)event)->x,
			(gint)((GdkEventButton*)event)->y, NULL);
		if (ramble_reset(florence->ramble,
				gtk_widget_get_window(GTK_WIDGET(florence->view->window)), key)) {
			status_pressed_set(florence->status, key);
			status_pressed_set(florence->status, NULL);
			status_focus_set(florence->status, key);
		}
	}
#endif
	END_FUNC
	return FALSE;
}

/* update the timer representation: to be called periodically */
gboolean flo_timer_update(gpointer data)
{
	START_FUNC
	struct florence *florence=(struct florence *)data;
	gboolean ret=TRUE;
	if (status_timer_get(florence->status)>0.0 && status_focus_get(florence->status)) {
		if (status_timer_get(florence->status)>=1.0) {
			flo_button_press_event(NULL, NULL, (void *)florence);
			flo_button_release_event(NULL, NULL, (void *)florence);
			status_timer_start(florence->status, flo_timer_update, (gpointer)florence);
		}
		/* view update */
		status_focus_set(florence->status, status_focus_get(florence->status));
	} else ret=FALSE;
	END_FUNC
	return ret;
}

/* Ensure the keyboard stays above; no periodic present/raise.
 * (Stock florence called gtk_window_present() every 1s — that synthesizes
 * leave/enter and flickers hover highlights ~1–2×/sec.) */
void flo_start_keep_on_top(struct florence *florence, gboolean keep_on_top)
{
	START_FUNC
	if (florence->to_top_id) {
		g_source_remove(florence->to_top_id);
		florence->to_top_id=0;
	}
	if (florence->view) {
		gtk_window_set_keep_above(view_window_get(florence->view),
		    keep_on_top || settings_get_bool(SETTINGS_ALWAYS_ON_TOP));
	}
	END_FUNC
}

/* handles mouse motion events 
 * update the keyboard key under the mouse */
gboolean flo_mouse_move_event(GtkWidget *window, GdkEvent *event, gpointer user_data)
{
	START_FUNC
	gint x, y;
#ifdef ENABLE_RAMBLE
	enum key_hit hit;
	gchar *algo;
#endif
	struct florence *florence=(struct florence *)user_data;
	if (status_get_moving(florence->status)) {
		/* Prefer event coords (reliable under seat grab / multi-device). */
		if (event && event->type == GDK_MOTION_NOTIFY) {
			x = (gint)((GdkEventMotion *)event)->x_root;
			y = (gint)((GdkEventMotion *)event)->y_root;
		} else {
			gdk_device_get_position(gdk_device_manager_get_client_pointer(
				gdk_display_get_device_manager(gdk_display_get_default())),
				NULL, &x, &y);
		}
		gtk_window_move(GTK_WINDOW(window), x-florence->xpos, y-florence->ypos);
	} else if (status_get_resizing(florence->status)) {
		gdouble factor, scale;
		gint dx, dy;
		gint slop = 8;

		if (event && event->type == GDK_MOTION_NOTIFY) {
			x = (gint)((GdkEventMotion *)event)->x_root;
			y = (gint)((GdkEventMotion *)event)->y_root;
		} else {
			gdk_device_get_position(gdk_device_manager_get_client_pointer(
				gdk_display_get_device_manager(gdk_display_get_default())),
				NULL, &x, &y);
		}
		dx = x - florence->status->resize_root_x;
		dy = y - florence->status->resize_root_y;
		/*
		 * Click vs drag: ignore motion inside slop so a single click
		 * can restore the cold-start size on release.
		 */
		if (!florence->status->resize_dragged) {
			if (dx > -slop && dx < slop && dy > -slop && dy < slop) {
				END_FUNC
				return FALSE;
			}
			florence->status->resize_dragged = TRUE;
		}
		/* Drag SE grows, NW shrinks; ~400px → ±100% scale. */
		factor = 1.0 + ((gdouble)(dx + dy) / 800.0);
		if (factor < 0.45) factor = 0.45;
		if (factor > 2.5) factor = 2.5;
		scale = florence->status->resize_scale0 * factor;
		view_live_scale(florence->view, scale,
		    florence->status->resize_pin_x,
		    florence->status->resize_pin_y);
		florence->status->resize_last = florence->view->scalex;
	} else {
		/* Remember mouse position for moving */
		florence->xpos=(gint)((GdkEventMotion*)event)->x;
		florence->ypos=(gint)((GdkEventMotion*)event)->y;
		/* Hold-to-repeat: ignore motion while a key is down (touch slop). */
		if (status_pressed_get(florence->status)) {
			END_FUNC
			return FALSE;
		}
#ifdef ENABLE_RAMBLE
		struct key *key=status_hit_get(florence->status, florence->xpos, florence->ypos, &hit);
		if (status_im_get(florence->status)==STATUS_IM_RAMBLE) {
			florence->view->ramble=florence->ramble;
			algo=settings_get_string(SETTINGS_RAMBLE_ALGO);
			if ((hit==KEY_BORDER) &&
				(status_focus_get(florence->status)==key) &&
				(!strcmp("time", algo))) {
				ramble_time_reset(florence->ramble);
				status_focus_set(florence->status, NULL);
			}
			if (algo) g_free(algo);
			if (ramble_started(florence->ramble) &&
				ramble_add(florence->ramble, gtk_widget_get_window(GTK_WIDGET(florence->view->window)),
					florence->xpos, florence->ypos, key)) {
				if (status_focus_get(florence->status)!=key) {
					status_focus_set(florence->status, key);
				}
				status_pressed_set(florence->status, key);
				status_pressed_set(florence->status, NULL);
			}
		} else
#else
		struct key *key=status_hit_get(florence->status, florence->xpos, florence->ypos);
#endif
		if (status_focus_get(florence->status)!=key) {
			if (key && settings_get_double(SETTINGS_TIMER)>0.0 &&
				status_im_get(florence->status)==STATUS_IM_TIMER) {
				status_timer_start(florence->status, flo_timer_update, (gpointer)florence);
			} else status_timer_stop(florence->status);
			status_focus_set(florence->status, key);
		}
	}
	END_FUNC
	return FALSE;
}

/* handles mouse enter events */
gboolean flo_mouse_enter_event (GtkWidget *window, GdkEvent *event, gpointer user_data)
{
	START_FUNC
	struct florence *florence=(struct florence *)user_data;
	/* Work around gtk bug 556006 */
	GdkEventCrossing *crossing=(GdkEventCrossing *)event;
	GdkEventMotion motion;
	if (status_im_get(florence->status)!=STATUS_IM_RAMBLE) {
		motion.x=crossing->x;
		motion.y=crossing->y;
		motion.x_root=crossing->x_root;
		motion.y_root=crossing->y_root;
		flo_mouse_move_event(window, (GdkEvent *)(&motion), user_data);
	}
	status_release_latched(florence->status, NULL);
	END_FUNC
	return FALSE;
}

/* Triggered by gconf when the "keep_on_top" parameter is changed. */
void flo_set_keep_on_top(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct florence *florence=user_data;
	flo_start_keep_on_top(florence, settings_get_bool(SETTINGS_KEEP_ON_TOP));
	END_FUNC
}

/* liberate memory used by the objects of the layout.
 * Those objects are the style object, the keyboards and the keys */
void flo_layout_unload(struct florence *florence)
{
	START_FUNC
	struct keyboard *keyboard;
	while (florence->keyboards) {
		keyboard=(struct keyboard *)florence->keyboards->data;
		keyboard_free(keyboard);
		florence->keyboards=g_slist_delete_link(florence->keyboards, florence->keyboards);
	}
	if (florence->style) style_free(florence->style);
	END_FUNC
}

/* loads the layout file
 * create the layour objects: the style, the keyboards and the keys */
void flo_layout_load(struct florence *florence)
{
	START_FUNC
	struct layout *layout;
	struct layout_infos *infos;
	gchar *layoutname;

	/* get the informations about the layout */
	layoutname=settings_get_string(SETTINGS_FILE);
	{
		const char *rng=DATADIR "/relaxng/florence.rng";
		const char *home=getenv("HOME");
		gchar *home_rng=NULL;

		if (home) {
			home_rng=g_strdup_printf(
			    "%s/theme/florence/relaxng/florence.rng", home);
			if (g_file_test(home_rng, G_FILE_TEST_EXISTS))
				rng=home_rng;
		}
		layout=layoutreader_new(layoutname,
		    DATADIR "/layouts/florence.xml",
		    (char *)rng);
		if (home_rng) g_free(home_rng);
	}
	layoutreader_element_open(layout, "layout");
	infos=layoutreader_infos_new(layout);
	flo_debug(TRACE_DEBUG, _("Layout name: \"%s\""), infos->name);
	if (!infos->version || strcmp(infos->version, VERSION))
		flo_warn(_("Layout version %s is different from program version %s"),
			infos->version, VERSION);
	layoutreader_infos_free(infos);

	/* create the style object */
	florence->style=style_new(NULL);

	/* create the keyboard objects */
	florence->keyboards=flo_keyboards_load(florence, layout);
	layoutreader_free(layout);
	g_free(layoutname);
	END_FUNC
}

/* reloads the layout file */
void flo_layout_reload(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct florence *florence=(struct florence *)user_data;
	status_reset(florence->status);
	flo_layout_unload(florence);
	flo_layout_load(florence);
	view_update_layout(florence->view, florence->style, florence->keyboards);
	END_FUNC
}

/* Activate or deactivate trayicon (depending on settings) */
void flo_trayicon(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct florence *florence=(struct florence *)user_data;
	if (settings_get_bool(SETTINGS_CONTROLLER_TRAYICON)) {
		if (!florence->trayicon)
			florence->trayicon=trayicon_new(florence->view, G_CALLBACK(flo_destroy),
					(gpointer)florence);
	} else {
		if (florence->trayicon) {
			trayicon_free(florence->trayicon);
			florence->trayicon=NULL;
		}
	}
	END_FUNC
}

/* create a new instance of florence. */
struct florence *flo_new(const gchar *focus_back, void (*ready)(void *))
{
	START_FUNC
	struct florence *florence=(struct florence *)g_malloc(sizeof(struct florence));
	if (!florence) flo_fatal(_("Unable to allocate memory for florence"));
	memset(florence, 0, sizeof(struct florence));

#ifdef ENABLE_RAMBLE
	florence->ramble=ramble_new();
#endif

	florence->status=status_new(focus_back);
#ifndef ENABLE_AT_SPI2
	status_spi_disable(florence->status);
#endif

	flo_layout_load(florence);
	florence->view=view_new(florence->status, florence->style, florence->keyboards);
	status_view_set(florence->status, florence->view);
	flo_start_keep_on_top(florence, settings_get_bool(SETTINGS_KEEP_ON_TOP));

	g_signal_connect(G_OBJECT(view_window_get(florence->view)), "destroy",
		G_CALLBACK(flo_destroy), florence);
	g_signal_connect(G_OBJECT(view_window_get(florence->view)), "motion-notify-event",
		G_CALLBACK(flo_mouse_move_event), florence);
	g_signal_connect(G_OBJECT(view_window_get(florence->view)), "leave-notify-event",
		G_CALLBACK(flo_mouse_leave_event), florence);
	g_signal_connect(G_OBJECT(view_window_get(florence->view)), "enter-notify-event",
		G_CALLBACK(flo_mouse_enter_event), florence);
	g_signal_connect(G_OBJECT(view_window_get(florence->view)), "button-press-event",
		G_CALLBACK(flo_button_press_event), florence);
	g_signal_connect(G_OBJECT(view_window_get(florence->view)), "button-release-event",
		G_CALLBACK(flo_button_release_event), florence);
	if (settings_get_bool(SETTINGS_CONTROLLER_TRAYICON)) {
		florence->trayicon=trayicon_new(florence->view, G_CALLBACK(flo_destroy), (gpointer)florence);
	}
	settings_changecb_register(SETTINGS_CONTROLLER_TRAYICON, flo_trayicon, florence);
	settings_changecb_register(SETTINGS_KEEP_ON_TOP, flo_set_keep_on_top, florence);
	/* TODO: just reload the style, no need to reload the whole layout */
	settings_changecb_register(SETTINGS_STYLE_ITEM, flo_layout_reload, florence);
	settings_changecb_register(SETTINGS_FILE, flo_layout_reload, florence);

	florence->service=service_new(florence->view, (service_cb)flo_terminate, (service_cb)ready, (gpointer)florence);
	END_FUNC
	return florence;
}

/* liberate all the memory used by florence */
void flo_free(struct florence *florence)
{
	START_FUNC
	flo_info(_("Terminating."));
	if (florence->trayicon) {
		trayicon_free(florence->trayicon);
		florence->trayicon=NULL;
	}
	flo_layout_unload(florence);
	if (florence->view) view_free(florence->view);
	florence->view=NULL;
	if (florence->status) status_free(florence->status);
	florence->status=NULL;
#ifdef ENABLE_RAMBLE
	if (florence->ramble) ramble_free(florence->ramble);
	florence->ramble=NULL;
#endif
	if (florence->service) service_free(florence->service);
	g_free(florence);
	xmlCleanupParser();
	xmlMemoryDump();
	END_FUNC
}

