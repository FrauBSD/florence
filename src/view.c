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

#include "system.h"
#include "view.h"
#include "status.h"
#include "trace.h"
#include "settings.h"
#include "keyboard.h"
#include "tools.h"
#include "fsm.h"
#include "key.h"
#include "florence.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <math.h>
#ifdef ENABLE_XKB
#include <X11/XKBlib.h>
#endif

#ifndef FLORENCE_GREETER
/*
 * Session: center on built-in panel, above FVWM taskbar; portrait fit like
 * greeter. Live RandR geometry (never stale FLORENCE_PANEL_* from login).
 */
#include <X11/extensions/Xrandr.h>

#define SESSION_GLYPH_H 44
#define SESSION_GLYPH_MARGIN 20
#define SESSION_TASKBAR_DEFAULT 40

static guint session_monitors_debounce_id;
static guint session_monitors_settle_id;
static gdouble session_saved_landscape_scale;
static int session_in_portrait_fit;

void view_create_window_mask(struct view *view);
void view_live_scale(struct view *view, gdouble scale, gint pin_x, gint pin_y);

static int
session_name_is_internal(const char *name)
{

	if (name == NULL)
		return (0);
	if (strncmp(name, "eDP", 3) == 0)
		return (1);
	if (strncmp(name, "LVDS", 4) == 0)
		return (1);
	if (strncmp(name, "DSI", 3) == 0)
		return (1);
	return (0);
}

static int
session_panel_xrandr(gint *px, gint *py, gint *pw, gint *ph)
{
	Display *xdpy;
	Window root;
	XRRScreenResources *res;
	XRROutputInfo *oi;
	XRRCrtcInfo *ci;
	int i;
	int fx = 0, fy = 0, fw = 0, fh = 0;
	int pox = 0, poy = 0, pow = 0, poh = 0;
	int have_first = 0, have_pri = 0;
	RROutput primary;

	xdpy = gdk_x11_get_default_xdisplay();
	if (!xdpy)
		return (0);
	root = DefaultRootWindow(xdpy);
	res = XRRGetScreenResourcesCurrent(xdpy, root);
	if (!res)
		return (0);
	primary = XRRGetOutputPrimary(xdpy, root);

	for (i = 0; i < res->noutput; i++) {
		oi = XRRGetOutputInfo(xdpy, res, res->outputs[i]);
		if (!oi)
			continue;
		if (oi->connection != RR_Connected || oi->crtc == None) {
			XRRFreeOutputInfo(oi);
			continue;
		}
		ci = XRRGetCrtcInfo(xdpy, res, oi->crtc);
		if (!ci || ci->mode == None) {
			if (ci)
				XRRFreeCrtcInfo(ci);
			XRRFreeOutputInfo(oi);
			continue;
		}
		if (session_name_is_internal(oi->name)) {
			*px = ci->x;
			*py = ci->y;
			*pw = (gint)ci->width;
			*ph = (gint)ci->height;
			XRRFreeCrtcInfo(ci);
			XRRFreeOutputInfo(oi);
			XRRFreeScreenResources(res);
			return (*pw > 0 && *ph > 0);
		}
		if (!have_first) {
			fx = ci->x;
			fy = ci->y;
			fw = (int)ci->width;
			fh = (int)ci->height;
			have_first = 1;
		}
		if (!have_pri && primary == res->outputs[i]) {
			pox = ci->x;
			poy = ci->y;
			pow = (int)ci->width;
			poh = (int)ci->height;
			have_pri = 1;
		}
		XRRFreeCrtcInfo(ci);
		XRRFreeOutputInfo(oi);
	}
	XRRFreeScreenResources(res);
	if (have_pri && pow > 0 && poh > 0) {
		*px = pox;
		*py = poy;
		*pw = pow;
		*ph = poh;
		return (1);
	}
	if (have_first && fw > 0 && fh > 0) {
		*px = fx;
		*py = fy;
		*pw = fw;
		*ph = fh;
		return (1);
	}
	return (0);
}

static int
session_builtin_panel(gint *px, gint *py, gint *pw, gint *ph)
{
	const char *ex, *ey, *ew, *eh;
	GdkDisplay *dpy;
	GdkRectangle geo;
	GdkMonitor *m;

	/* Live CRTC first - login FLORENCE_PANEL_* goes stale after rotate. */
	if (session_panel_xrandr(px, py, pw, ph))
		return (1);

	ex = getenv("FLORENCE_PANEL_X");
	ey = getenv("FLORENCE_PANEL_Y");
	ew = getenv("FLORENCE_PANEL_W");
	eh = getenv("FLORENCE_PANEL_H");
	if (!(ex && ey && ew && eh)) {
		/* florence-greeter-start exports these. */
		ex = getenv("FLORENCE_GREETER_PANEL_X");
		ey = getenv("FLORENCE_GREETER_PANEL_Y");
		ew = getenv("FLORENCE_GREETER_PANEL_W");
		eh = getenv("FLORENCE_GREETER_PANEL_H");
	}
	if (ex && ey && ew && eh) {
		*px = atoi(ex);
		*py = atoi(ey);
		*pw = atoi(ew);
		*ph = atoi(eh);
		if (*pw > 0 && *ph > 0)
			return (1);
	}

	dpy = gdk_display_get_default();
	if (!dpy)
		return (0);
	m = gdk_display_get_primary_monitor(dpy);
	if (!m && gdk_display_get_n_monitors(dpy) > 0)
		m = gdk_display_get_monitor(dpy, 0);
	if (!m)
		return (0);
	gdk_monitor_get_geometry(m, &geo);
	*px = geo.x;
	*py = geo.y;
	*pw = geo.width;
	*ph = geo.height;
	return (1);
}

static gdouble
session_preferred_landscape_scale(struct view *view, gdouble cur)
{
	gdouble want;

	want = session_saved_landscape_scale;
	if (want < 10.0 && view->status &&
	    view->status->resize_scale_launch >= 10.0)
		want = view->status->resize_scale_launch;
	if (want < 10.0)
		want = settings_get_double(SETTINGS_SCALEX);
	if (want < 10.0)
		want = cur;
	if (want < 10.0)
		want = 10.0;
	if (want > 72.0)
		want = 72.0;
	return (want);
}

static void
session_fit_scale_for_panel(struct view *view, gint pw, gint ph, gint margin,
    gint taskbar)
{
	gdouble fit_w, fit_h, fit, cur, want;
	gint max_w, max_h, pin_x, pin_y, bottom_clear;
	int portrait;

	if (!view || view->vwidth < 1.0 || view->vheight < 1.0)
		return;
	if (margin < 8)
		margin = 8;
	bottom_clear = margin + SESSION_GLYPH_H + SESSION_GLYPH_MARGIN + taskbar;
	portrait = (pw < ph);
	cur = view->scalex;
	if (cur < 1.0)
		cur = settings_get_double(SETTINGS_SCALEX);

	pin_x = settings_get_int(SETTINGS_XPOS);
	pin_y = settings_get_int(SETTINGS_YPOS);
	if (view->window)
		gtk_window_get_position(view->window, &pin_x, &pin_y);

	if (portrait) {
		if (!session_in_portrait_fit) {
			session_saved_landscape_scale =
			    session_preferred_landscape_scale(view, cur);
			session_in_portrait_fit = 1;
		}
		max_w = pw - 2 * margin;
		max_h = ph - margin - bottom_clear;
		if (max_w < 32)
			max_w = pw > 32 ? pw - margin : pw;
		if (max_h < 32)
			max_h = ph > 32 ? ph / 2 : ph;
		fit_w = (gdouble)max_w / view->vwidth;
		fit_h = (gdouble)max_h / view->vheight;
		fit = fit_w < fit_h ? fit_w : fit_h;
		if (fit < 10.0)
			fit = 10.0;
		if (fit > 72.0)
			fit = 72.0;
		if (session_saved_landscape_scale > 0.0 &&
		    fit > session_saved_landscape_scale)
			fit = session_saved_landscape_scale;
		if (fabs(fit - cur) < 0.05 &&
		    (gint)view->width <= max_w + 2 &&
		    (gint)view->height <= max_h + 2)
			return;
		view_live_scale(view, fit, pin_x, pin_y);
	} else {
		/*
		 * Landscape: always restore preferred scale when we were in
		 * portrait fit or the shell is still portrait-shrunk. Do not
		 * clear the saved scale - a mid-rotate spurious landscape
		 * event must not forget it for the real settle.
		 */
		want = session_preferred_landscape_scale(view, cur);
		if (session_in_portrait_fit || cur + 0.05 < want) {
			if (fabs(want - cur) >= 0.05)
				view_live_scale(view, want, pin_x, pin_y);
		}
		session_in_portrait_fit = 0;
		if (want >= 10.0)
			session_saved_landscape_scale = want;
	}
}

/*
 * True if keyboard center sits on a non-eDP connected output (user parked it
 * on an external). Off-desk / below-panel gaps are NOT "external" - those
 * must be re-homed (POLA on show after rotate).
 */
static int
session_keyboard_on_external(struct view *view)
{
	Display *xdpy;
	Window root;
	XRRScreenResources *res;
	XRROutputInfo *oi;
	XRRCrtcInfo *ci;
	gint wx, wy, ww, wh, cx, cy;
	int i, on_ext = 0;

	if (!view || !view->window)
		return (0);
	gtk_window_get_position(view->window, &wx, &wy);
	gtk_window_get_size(view->window, &ww, &wh);
	cx = wx + ww / 2;
	cy = wy + wh / 2;

	xdpy = gdk_x11_get_default_xdisplay();
	if (!xdpy)
		return (0);
	root = DefaultRootWindow(xdpy);
	res = XRRGetScreenResourcesCurrent(xdpy, root);
	if (!res)
		return (0);

	for (i = 0; i < res->noutput; i++) {
		oi = XRRGetOutputInfo(xdpy, res, res->outputs[i]);
		if (!oi)
			continue;
		if (oi->connection != RR_Connected || oi->crtc == None) {
			XRRFreeOutputInfo(oi);
			continue;
		}
		ci = XRRGetCrtcInfo(xdpy, res, oi->crtc);
		if (!ci || ci->mode == None) {
			if (ci)
				XRRFreeCrtcInfo(ci);
			XRRFreeOutputInfo(oi);
			continue;
		}
		if (cx >= (gint)ci->x && cy >= (gint)ci->y &&
		    cx < (gint)(ci->x + ci->width) &&
		    cy < (gint)(ci->y + ci->height)) {
			if (!session_name_is_internal(oi->name))
				on_ext = 1;
			XRRFreeCrtcInfo(ci);
			XRRFreeOutputInfo(oi);
			break;
		}
		XRRFreeCrtcInfo(ci);
		XRRFreeOutputInfo(oi);
	}
	XRRFreeScreenResources(res);
	return (on_ext);
}

static void
session_place_keyboard(struct view *view)
{
	GdkDisplay *dpy;
	const char *em, *et;
	gint px, py, pw, ph, ww, wh, margin, taskbar, bottom_clear, x, y;

	if (!view || !view->window)
		return;
	dpy = gtk_widget_get_display(GTK_WIDGET(view->window));
	if (!session_builtin_panel(&px, &py, &pw, &ph)) {
		GdkMonitor *m;
		GdkRectangle geo;

		px = 0;
		py = 0;
		pw = 0;
		ph = 0;
		m = gdk_display_get_primary_monitor(dpy);
		if (!m && gdk_display_get_n_monitors(dpy) > 0)
			m = gdk_display_get_monitor(dpy, 0);
		if (m) {
			gdk_monitor_get_geometry(m, &geo);
			px = geo.x;
			py = geo.y;
			pw = geo.width;
			ph = geo.height;
		}
	}

	em = getenv("FLORENCE_PLACE_MARGIN");
	if (em && atoi(em) > 0)
		margin = atoi(em);
	else {
		margin = (gint)gdk_display_get_default_cursor_size(dpy);
		if (margin < 16)
			margin = 24;
	}
	et = getenv("FLORENCE_TASKBAR_H");
	if (florence_in_greeter())
		taskbar = 0; /* XDM greeter has no Start bar. */
	else
		taskbar = (et && atoi(et) > 0) ? atoi(et) : SESSION_TASKBAR_DEFAULT;
	/*
	 * Landscape: sit just above the taskbar (glyph is corner-only).
	 * Portrait: also clear the float glyph so the OSK does not cover it.
	 * Greeter: always clear the glyph (no taskbar).
	 */
	if (florence_in_greeter() || pw < ph)
		bottom_clear = margin + SESSION_GLYPH_H + SESSION_GLYPH_MARGIN +
		    taskbar;
	else
		bottom_clear = margin + taskbar;

	session_fit_scale_for_panel(view, pw, ph, margin, taskbar);

	ww = (gint)view->width;
	wh = (gint)view->height;
	if (ww < 1 || wh < 1)
		gtk_window_get_size(view->window, &ww, &wh);

	x = px + (pw - ww) / 2;
	y = py + ph - wh - bottom_clear;
	if (x < px)
		x = px;
	if (y < py)
		y = py;
	if (x + ww > px + pw)
		x = px + pw - ww;
	if (y + wh > py + ph)
		y = py + ph - wh;
	if (x < px)
		x = px;
	if (y < py)
		y = py;

	{
		GdkWindow *gdkw;

		gtk_widget_set_size_request(GTK_WIDGET(view->window), ww, wh);
		gdkw = gtk_widget_get_window(GTK_WIDGET(view->window));
		if (gdkw) {
			gtk_window_resize(view->window, ww, wh);
			gdk_window_move_resize(gdkw, x, y, ww, wh);
			/*
			 * Match greeter: refresh XShape after place. Skip
			 * settings_set_int here - keyfile rewrite mid-RandR
			 * races FVWM and can leave the mapped shell stuck.
			 */
			if (settings_get_bool(SETTINGS_TRANSPARENT) &&
			    !view->composite)
				view_create_window_mask(view);
			else
				gtk_widget_queue_draw(GTK_WIDGET(view->window));
		} else {
			gtk_window_resize(view->window, ww, wh);
			gtk_window_move(view->window, x, y);
		}
	}
	/* In-memory pins only (greeter policy). */
	if (view->status) {
		view->status->move_launch_x = x;
		view->status->move_launch_y = y;
		view->status->move_launch_valid = TRUE;
		view->status->resize_shell_w = (guint)ww;
		view->status->resize_shell_h = (guint)wh;
	}
}

static gboolean
session_monitors_settle_idle(gpointer data)
{
	struct view *view = (struct view *)data;

	session_monitors_settle_id = 0;
	if (!view || !view->window)
		return FALSE;
	if (!gtk_widget_get_visible(GTK_WIDGET(view->window)))
		return FALSE;
	/* Second pass after FVWM/RandR finish rearranging the desk. */
	session_place_keyboard(view);
	return FALSE;
}

static gboolean
session_monitors_reposition_idle(gpointer data)
{
	struct view *view = (struct view *)data;

	session_monitors_debounce_id = 0;
	if (!view || !view->window)
		return FALSE;
	if (!gtk_widget_get_visible(GTK_WIDGET(view->window)))
		return FALSE;
	/*
	 * Always re-home on RandR settle. Pre-rotate absolute coords can land
	 * on an external CRTC after P->L; skipping those left the OSK stuck
	 * until hide/show. (view_show still honors on-external parking.)
	 */
	session_place_keyboard(view);
	if (session_monitors_settle_id)
		g_source_remove(session_monitors_settle_id);
	session_monitors_settle_id = g_timeout_add(500,
	    session_monitors_settle_idle, view);
	return FALSE;
}

static void
session_on_monitors_changed(GdkScreen *screen, gpointer data)
{
	struct view *view = (struct view *)data;

	(void)screen;
	if (session_monitors_debounce_id)
		g_source_remove(session_monitors_debounce_id);
	/* Wait for xrandr + autorotate (+ FVWM) to settle before place. */
	session_monitors_debounce_id = g_timeout_add(500,
	    session_monitors_reposition_idle, view);
}

static void
session_watch_monitors(struct view *view)
{
	GdkScreen *screen;

	if (!view)
		return;
	screen = gdk_screen_get_default();
	if (!screen)
		return;
	g_signal_handlers_disconnect_by_func(screen,
	    G_CALLBACK(session_on_monitors_changed), view);
	g_signal_connect(screen, "monitors-changed",
	    G_CALLBACK(session_on_monitors_changed), view);
	g_signal_connect(screen, "size-changed",
	    G_CALLBACK(session_on_monitors_changed), view);
}
#endif


/* Show the view next to the accessible object if specified. */
#ifdef AT_SPI
#ifdef ENABLE_AT_SPI2
void view_show (struct view *view, AtspiAccessible *object)
#else
void view_show (struct view *view, Accessible *object)
#endif
#else
void view_show (struct view *view)
#endif
{
	START_FUNC
	gtk_widget_show(GTK_WIDGET(view->window));
	/* Some window managers forget it */
	gtk_window_set_keep_above(view->window, TRUE);
	/* Do not set urgency_hint - WMs pulse/flash it and the hover flickers. */
#ifndef FLORENCE_GREETER
	/*
	 * Re-home for current eDP orientation unless parked on an external.
	 * Covers first-open in portrait (stale landscape XPOS) and hide/show
	 * while stuck below the desk after a rotate.
	 */
	if (!session_keyboard_on_external(view))
		session_place_keyboard(view);
#else
	gtk_window_move(view->window, settings_get_int(SETTINGS_XPOS), settings_get_int(SETTINGS_YPOS));
#endif
#ifdef AT_SPI
	/* positionnement intelligent */
	if (settings_get_bool(SETTINGS_AUTO_HIDE) && 
		settings_get_bool(SETTINGS_MOVE_TO_WIDGET) && object) {
		tools_window_move(view->window, object);
	}
#endif
	END_FUNC
}

/* Hides the view */
void view_hide (struct view *view)
{
	START_FUNC
	/*
	 * Drop hover/press highlight before unmap. Hide often delivers
	 * LeaveNotify with GDK_CROSSING_UNGRAB (ignored by flo_mouse_leave
	 * to avoid compositor flicker), and status_focus_set(NULL) refuses
	 * while pressed is still set mid-release - so the reduce/close key
	 * stayed focused and drew highlighted when the glyph reopened.
	 */
	if (view && view->status) {
		view->status->pressed = NULL;
		view->status->focus = NULL;
	}
	gtk_widget_hide(GTK_WIDGET(view->window));
	END_FUNC
}

void view_on_destroy(gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	view->window=NULL;
	END_FUNC
}

/* destroy the view */
void view_destroy(struct view *view)
{
	START_FUNC
	if (view->window) {
		GtkWidget *window=GTK_WIDGET(view->window);
		view->window=NULL;
		gtk_widget_destroy(window);
	}
	END_FUNC
}

/* resize the window */
void view_resize (struct view *view)
{
	START_FUNC
	GdkRectangle rect;
	GdkGeometry hints;
	hints.win_gravity=GDK_GRAVITY_NORTH_WEST;
	if (settings_get_bool(SETTINGS_RESIZABLE)) {
		gtk_window_set_resizable(view->window, TRUE);
		if (settings_get_bool(SETTINGS_KEEP_RATIO)) {
			hints.min_aspect=view->vwidth/view->vheight;
			hints.max_aspect=view->vwidth/view->vheight;
			gtk_window_set_geometry_hints(view->window, NULL, &hints,
				GDK_HINT_ASPECT|GDK_HINT_WIN_GRAVITY);
		} else {
			gtk_window_set_geometry_hints(view->window, NULL, &hints,
				GDK_HINT_WIN_GRAVITY);
		}
		/* Do not call configure signal handler */
		if (view->configure_handler) g_signal_handler_disconnect(G_OBJECT(view->window), view->configure_handler);
		view->configure_handler=0;
		gtk_window_resize(view->window, view->width, view->height);
	} else {
		gtk_window_set_geometry_hints(view->window, NULL, &hints,
			GDK_HINT_WIN_GRAVITY);
		gtk_window_set_resizable(view->window, FALSE);
		gtk_widget_set_size_request(GTK_WIDGET(view->window),
			view->width, view->height);
	}
	/* refresh the view */
	if (view->window && gtk_widget_get_window(GTK_WIDGET(view->window))) {
		rect.x=0; rect.y=0;
		rect.width=view->width; rect.height=view->height;
		gdk_window_invalidate_rect(gtk_widget_get_window(GTK_WIDGET(view->window)), &rect, TRUE);
	}
	END_FUNC
}

/*
 * Live-move / live-resize: with a compositor, redraw only. XShape suspend /
 * opaque wipe remains only as a fallback when transparent && !composite.
 */
static int view_shape_drag_suspended;
static guint view_shape_drag_resume_id;

static int
view_needs_shape_fallback(struct view *view)
{
	return view && !view->composite &&
	    settings_get_bool(SETTINGS_TRANSPARENT);
}

static void
view_shape_drag_suspend(struct view *view)
{
	Display *disp;
	GdkWindow *gdkw;
	GdkRGBA black = { 0.0, 0.0, 0.0, 1.0 };

	if (view_shape_drag_suspended || !view_needs_shape_fallback(view) ||
	    !view->window)
		return;
	gdkw = gtk_widget_get_window(GTK_WIDGET(view->window));
	if (!gdkw)
		return;
	if (view_shape_drag_resume_id) {
		g_source_remove(view_shape_drag_resume_id);
		view_shape_drag_resume_id = 0;
	}
	disp = (Display *)gdk_x11_get_default_xdisplay();
	XShapeCombineMask(disp, GDK_WINDOW_XID(gdkw), ShapeBounding, 0, 0, 0,
	    ShapeSet);
	/* No non-deprecated GdkWindow background API in GTK3. */
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	gdk_window_set_background_rgba(gdkw, &black);
	G_GNUC_END_IGNORE_DEPRECATIONS
	view_shape_drag_suspended = 1;
}

static void
view_shape_drag_resume(struct view *view)
{
	if (!view_shape_drag_suspended || !view)
		return;
	view_shape_drag_suspended = 0;
	view_create_window_mask(view);
}

static gboolean
view_shape_drag_resume_idle(gpointer data)
{
	struct view *view = (struct view *)data;

	view_shape_drag_resume_id = 0;
	if (!view || !view->window)
		return FALSE;
	if (view->status &&
	    (status_get_resizing(view->status) || status_get_moving(view->status)))
		return FALSE;
	view_shape_drag_resume(view);
	return FALSE;
}

void
view_greeter_live_drag_begin(struct view *view)
{
	view_shape_drag_suspend(view);
	if (view && view->window)
		gtk_widget_queue_draw(GTK_WIDGET(view->window));
}

void
view_greeter_live_drag_end(struct view *view)
{
	if (view_shape_drag_resume_id) {
		g_source_remove(view_shape_drag_resume_id);
		view_shape_drag_resume_id = 0;
	}
	view_shape_drag_resume(view);
}

/*
 * Live-resize: window size always matches content (grow and shrink). Pin is
 * client NW root origin at press. Art is rebuilt each frame - never stretched.
 */
void view_live_scale (struct view *view, gdouble scale, gint pin_x, gint pin_y)
{
	START_FUNC
	GdkWindow *gdkw;
	GdkGeometry hints;
	guint w, h;

	if (!view || !view->window) {
		END_FUNC
		return;
	}
	if (scale < 10.0) scale = 10.0;
	if (scale > 72.0) scale = 72.0;

	w = (guint)(view->vwidth * scale + 0.5);
	h = (guint)(view->vheight * scale + 0.5);
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	/* Only act when content size/scale changes. */
	if (w == view->width && h == view->height &&
	    fabs(view->scalex - scale) < 0.001) {
		END_FUNC
		return;
	}

	view->scalex = scale;
	view->scaley = scale;
	view->width = w;
	view->height = h;

	if (view->status) {
		view->status->resize_shell_w = w;
		view->status->resize_shell_h = h;
	}

	if (view->configure_handler) {
		g_signal_handler_disconnect(G_OBJECT(view->window),
		    view->configure_handler);
		view->configure_handler = 0;
	}

	hints.win_gravity = GDK_GRAVITY_NORTH_WEST;
	gtk_window_set_geometry_hints(view->window, NULL, &hints,
	    GDK_HINT_WIN_GRAVITY);
	gtk_window_set_gravity(view->window, GDK_GRAVITY_NORTH_WEST);

	gtk_widget_set_size_request(GTK_WIDGET(view->window),
	    (gint)w, (gint)h);

	gdkw = gtk_widget_get_window(GTK_WIDGET(view->window));
	if (gdkw)
		gdk_window_move_resize(gdkw, pin_x, pin_y, (gint)w, (gint)h);

	/* Force fresh SVG/key art at this scale (no stretch blit). */
	if (view->background) {
		cairo_surface_destroy(view->background);
		view->background = NULL;
	}
	if (view->symbols) {
		cairo_surface_destroy(view->symbols);
		view->symbols = NULL;
	}
	/*
	 * Refresh XShape to the new key outlines. create_window_mask also
	 * queue_draws; do not paint an opaque panel under the holes.
	 */
	if (settings_get_bool(SETTINGS_TRANSPARENT) && !view->composite && gdkw)
		view_create_window_mask(view);
	else
		gtk_widget_queue_draw(GTK_WIDGET(view->window));
	END_FUNC
}

void view_live_scale_commit (struct view *view)
{
	START_FUNC
	GdkWindow *gdkw;
	gint pin_x, pin_y;

	if (!view || !view->window) {
		END_FUNC
		return;
	}

	if (view->status) {
		pin_x = view->status->resize_pin_x;
		pin_y = view->status->resize_pin_y;
		/* Allow shell to shrink to content on the next apply. */
		view->status->resize_shell_w = view->width;
		view->status->resize_shell_h = view->height;
	} else {
		pin_x = settings_get_int(SETTINGS_XPOS);
		pin_y = settings_get_int(SETTINGS_YPOS);
	}

	/* Exact content size (may shrink after a grow-only drag). */
	gtk_widget_set_size_request(GTK_WIDGET(view->window),
	    (gint)view->width, (gint)view->height);

	gdkw = gtk_widget_get_window(GTK_WIDGET(view->window));
	if (gdkw)
		gdk_window_move_resize(gdkw, pin_x, pin_y,
		    (gint)view->width, (gint)view->height);

	settings_set_double(SETTINGS_SCALEX, view->scalex, FALSE);
	settings_set_double(SETTINGS_SCALEY, view->scaley, FALSE);
	settings_set_int(SETTINGS_XPOS, pin_x);
	settings_set_int(SETTINGS_YPOS, pin_y);

	if (view->background) {
		cairo_surface_destroy(view->background);
		view->background = NULL;
	}
	if (view->symbols) {
		cairo_surface_destroy(view->symbols);
		view->symbols = NULL;
	}
	view_create_window_mask(view);
	gtk_widget_queue_draw(GTK_WIDGET(view->window));
	END_FUNC
}

/* Click on move (no drag): restore cold-start / default open position. */
void view_restore_open_position (struct view *view)
{
	START_FUNC
#ifndef FLORENCE_GREETER
	if (!view || !view->window)
		return;
	session_place_keyboard(view);
#else
	gint x, y;

	if (!view || !view->window)
		return;
	if (view->status && view->status->move_launch_valid) {
		x = view->status->move_launch_x;
		y = view->status->move_launch_y;
	} else {
		x = settings_get_int(SETTINGS_XPOS);
		y = settings_get_int(SETTINGS_YPOS);
	}
	gtk_window_move(view->window, x, y);
	settings_set_int(SETTINGS_XPOS, x);
	settings_set_int(SETTINGS_YPOS, y);
#endif
	END_FUNC
}

/* draws the background of florence */
void view_draw (struct view *view, cairo_t *cairoctx, cairo_surface_t **surface, enum style_class class)
{
	START_FUNC
	GSList *list=view->keyboards;
	struct keyboard *keyboard;
	cairo_t *offscreen;

	/* create the surface */
	if (!*surface) *surface=cairo_surface_create_similar(cairo_get_target(cairoctx),
		CAIRO_CONTENT_COLOR_ALPHA, view->width, view->height);
	offscreen=cairo_create(*surface);
	cairo_set_source_rgba(offscreen, 0.0, 0.0, 0.0, 0.0);
	cairo_set_operator(offscreen, CAIRO_OPERATOR_SOURCE);
	cairo_paint(offscreen);
	cairo_set_operator(offscreen, CAIRO_OPERATOR_OVER);

	/* browse the keyboards */
	cairo_save(offscreen);
	cairo_scale(offscreen, view->scalex, view->scaley);
	while (list)
	{
		keyboard=(struct keyboard *)list->data;
		if (keyboard_activated(keyboard)) {
			/* actual draw */
			switch(class) {
				case STYLE_SHAPE:
					keyboard_background_draw(keyboard, offscreen, view->style, view->status);
					if (keyboard->under) {
						cairo_set_source_rgba(offscreen, 0.0, 0.0, 0.0, 0.75);
						cairo_set_operator(offscreen, CAIRO_OPERATOR_OVER);
						cairo_rectangle(offscreen, keyboard->xpos, keyboard->ypos,
							keyboard_get_width(keyboard), keyboard_get_height(keyboard));
						cairo_fill(offscreen);
					}
					break;
				case STYLE_SYMBOL:
					keyboard_symbols_draw(keyboard, offscreen, view->style, view->status);
					break;
			}
		}
		list=list->next;
	}
	cairo_destroy(offscreen);
	END_FUNC
}

/* draws the background of florence */
void view_background_draw (struct view *view, cairo_t *cairoctx)
{
	START_FUNC
	view_draw(view, cairoctx, &(view->background), STYLE_SHAPE);
	END_FUNC
}

/* draws the symbols */
void view_symbols_draw (struct view *view, cairo_t *cairoctx)
{
	START_FUNC
	view_draw(view, cairoctx, &(view->symbols), STYLE_SYMBOL);
	END_FUNC
}

/* update the keyboard positions */
void view_keyboards_set_pos(struct view *view, struct keyboard *over)
{
	START_FUNC
	GSList *list=view->keyboards;
	struct keyboard *keyboard;
	gdouble width=0.0, height=0.0, xoffset=0.0, yoffset=0.0;
	gdouble x=0.0, y=0.0;

	/* browse the keyboards */
	while (list)
	{
		keyboard=(struct keyboard *)list->data;
		if (keyboard_activated(keyboard)) {
			/* get the position to draw the keyboard */
			switch (keyboard_get_placement(keyboard)) {
				case LAYOUT_VOID:
					width=keyboard_get_width(keyboard);
					height=keyboard_get_height(keyboard);
					xoffset=yoffset=0.0;
					x=y=0.0;
					if (over) keyboard_set_under(keyboard); else keyboard_set_over(keyboard);
					break;
				case LAYOUT_TOP:
					yoffset+=keyboard_get_height(keyboard);
					/*
					 * Align with the leftmost chrome (actionkys), not
					 * the main board, so Esc can sit above Close.
					 */
					x=-view->xoffset; y=-yoffset;
					if (over) keyboard_set_under(keyboard); else keyboard_set_over(keyboard);
					break;
				case LAYOUT_BOTTOM:
					x=0.0; y=height;
					height+=keyboard_get_height(keyboard);
					if (over) keyboard_set_under(keyboard); else keyboard_set_over(keyboard);
					break;
				case LAYOUT_LEFT:
					xoffset+=keyboard_get_width(keyboard);
					x=-xoffset; y=0.0;
					if (over) keyboard_set_under(keyboard); else keyboard_set_over(keyboard);
					break;
				case LAYOUT_RIGHT:
					x=width; y=0.0;
					width+=keyboard_get_width(keyboard);
					if (over) keyboard_set_under(keyboard); else keyboard_set_over(keyboard);
					break;
				case LAYOUT_OVER:
					if (keyboard_get_width(keyboard)>width) width=keyboard_get_width(keyboard);
					if (keyboard_get_height(keyboard)>height) height=keyboard_get_height(keyboard);
					x=(width-view->xoffset-keyboard_get_width(keyboard))/2.0;
					y=(height-view->yoffset-keyboard_get_height(keyboard))/2.0;
					if (over==keyboard) keyboard_set_over(keyboard); else keyboard_set_under(keyboard);
					break;
			}
			keyboard_set_pos(keyboard, x+view->xoffset, y+view->yoffset);
		}
		list = list->next;
	}
	END_FUNC
}

/* calculate the dimensions of Florence */
void view_set_dimensions(struct view *view)
{
	START_FUNC
	GSList *list=view->keyboards;
	struct keyboard *keyboard;
	struct keyboard *over=NULL;

	while (list)
	{
		keyboard=(struct keyboard *)list->data;
		if (keyboard_activated(keyboard)) {
			switch (keyboard_get_placement(keyboard)) {
				case LAYOUT_VOID:
					view->vwidth=keyboard_get_width(keyboard);
					view->vheight=keyboard_get_height(keyboard);
					view->xoffset=view->yoffset=0;
					break;
				case LAYOUT_TOP:
					view->vheight+=(view->yoffset+=keyboard_get_height(keyboard));
					break;
				case LAYOUT_BOTTOM:
					view->vheight+=keyboard_get_height(keyboard);
					break;
				case LAYOUT_LEFT:
					view->vwidth+=(view->xoffset+=keyboard_get_width(keyboard));
					break;
				case LAYOUT_RIGHT:
					view->vwidth+=keyboard_get_width(keyboard);
					/* Nav/numpad can be taller than the main board
					 * (e.g. PrtSc row); grow the window so the
					 * bottom arrow row is not clipped. */
					if (view->yoffset + keyboard_get_height(keyboard) >
					    view->vheight)
						view->vheight = view->yoffset +
						    keyboard_get_height(keyboard);
					break;
				case LAYOUT_OVER:
					if (keyboard_get_width(keyboard)>view->vwidth) view->vwidth=keyboard_get_width(keyboard);
					if (keyboard_get_height(keyboard)>view->vheight) view->vheight=keyboard_get_height(keyboard);
					over=keyboard;
					break;
			}
		}
		list = list->next;
	}
	view->width=(guint)(view->vwidth*view->scalex);
	view->height=(guint)(view->vheight*view->scaley);
	view_keyboards_set_pos(view, over);
	END_FUNC
}

/* get the key at position */
#ifdef ENABLE_RAMBLE
struct key *view_hit_get (struct view *view, gint x, gint y, enum key_hit *hit)
#else
struct key *view_hit_get (struct view *view, gint x, gint y)
#endif
{
	START_FUNC
	GSList *list=view->keyboards;
	struct keyboard *keyboard=NULL;
	struct key *key;
	gint kx=0.0, ky=0.0, kw=0.0, kh=0.0;

	/* find the hit keyboard */
	while (list)
	{
		keyboard=(struct keyboard *)list->data;
		/* TODO: record in pixel
		 * and move that to keyboard_test */
		kx=keyboard->xpos*view->scalex;
		ky=keyboard->ypos*view->scaley;
		kw=keyboard->width*view->scalex;
		kh=keyboard->height*view->scaley;
		if (keyboard_activated(keyboard) && (!keyboard->under) && (x>=kx) && (x<=(kx+kw)) && (y>=ky) && y<=(ky+kh)) {
			list=NULL;
		}
		else list = list->next;
	}
#ifdef ENABLE_RAMBLE
	key=keyboard_hit_get(keyboard, x-kx, y-ky, view->scalex, view->scaley, hit);
#else
	key=keyboard_hit_get(keyboard, x-kx, y-ky, view->scalex, view->scaley);
#endif

	END_FUNC
	return key;
}

/* Create a window mask for transparent window for non-composited screen */
/* For composited screen, this function is useless, use alpha channel instead. */
void view_create_window_mask(struct view *view)
{
	START_FUNC
	Pixmap shape;
	cairo_surface_t *mask=NULL;
	cairo_t *cairoctx=NULL;
	Display *disp=(Display *)gdk_x11_get_default_xdisplay();

	if (settings_get_bool(SETTINGS_TRANSPARENT) && (!view->composite)) {
		shape=XCreatePixmap(disp, GDK_WINDOW_XID(gtk_widget_get_window(GTK_WIDGET(view->window))),
			view->width, view->height, 1);
		mask=cairo_xlib_surface_create_for_bitmap(disp, shape,
			DefaultScreenOfDisplay(disp), view->width, view->height);
		cairoctx=cairo_create(mask);
		view_background_draw(view, cairoctx);
		cairo_set_source_rgba(cairoctx, 0.0, 0.0, 0.0, 0.0);
		cairo_set_operator(cairoctx, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cairoctx);
		cairo_set_source_surface(cairoctx, view->background, 0, 0);
		cairo_paint(cairoctx);
		XShapeCombineMask(disp, GDK_WINDOW_XID(gtk_widget_get_window(GTK_WIDGET(view->window))),
			ShapeBounding, 0, 0, cairo_xlib_surface_get_drawable(mask), ShapeSet);
		cairo_destroy(cairoctx);
		cairo_surface_destroy(view->background);
		view->background=NULL;
		cairo_surface_destroy(mask);
		status_focus_zoom_set(view->status, FALSE);
	} else {
		XShapeCombineMask(disp, GDK_WINDOW_XID(gtk_widget_get_window(GTK_WIDGET(view->window))),
			ShapeBounding, 0, 0, 0, ShapeSet);
		status_focus_zoom_set(view->status, TRUE);
	}
	gtk_widget_queue_draw(GTK_WIDGET(view->window));
	END_FUNC
}

/* Triggered by gconf when the "transparent" parameter is changed. Calls view_create_window_mask */
void view_set_transparent(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	gboolean shown=gtk_widget_get_visible(GTK_WIDGET(view->window));
	gtk_widget_show(GTK_WIDGET(view->window));
	view_create_window_mask(view);
	if (!shown) gtk_widget_hide(GTK_WIDGET(view->window));
	END_FUNC
}

/* Triggered by gconf when the "decorated" parameter is changed. Decorates or undecorate the window. */
void view_set_decorated(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	gtk_window_set_decorated(view->window, settings_get_bool(SETTINGS_DECORATED));
	gtk_window_move(view->window, settings_get_int(SETTINGS_XPOS), settings_get_int(SETTINGS_YPOS));
	END_FUNC
}

/* Triggered by gconf when the "always_on_top" parameter is changed. 
   Change the window property to be always on top or not to be. */
void view_set_always_on_top(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	gtk_window_set_keep_above(view->window, settings_get_bool(SETTINGS_ALWAYS_ON_TOP));
	END_FUNC
}

/* Triggered by gconf when the "task_bar" parameter is changed. 
   Change the window hint to appear in the task bar or not. */
void view_set_task_bar(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	gtk_window_set_skip_taskbar_hint(view->window, !settings_get_bool(SETTINGS_TASK_BAR));
	END_FUNC
}

/* Triggered by gconf when the "resizable" parameter is changed.
   makes the window (not)resizable the window. */
void view_set_resizable(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	if (settings_get_bool(SETTINGS_RESIZABLE)) {
		gtk_widget_set_size_request(GTK_WIDGET(view->window), view->vwidth, view->vheight);
	}
	view_resize(view);
	END_FUNC
}

/* Triggered by gconf when a color parameter is changed. */
void view_redraw(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	style_update_colors(view->style);
	if ((!strcmp(key, "key")) || (!strcmp(key, "outline"))) {
		if (view->background) cairo_surface_destroy(view->background);
		view->background=NULL;
	} else if (!strncmp(key, "label", 5) || (!strcmp(key, "font")) || (!strcmp(key, "system_font"))) {
		if (view->symbols) cairo_surface_destroy(view->symbols);
		view->symbols=NULL;
	}
	gtk_widget_queue_draw(GTK_WIDGET(view->window));
	END_FUNC
}

/* Triggered by gconf when the "resizable" parameter is changed.
   makes the window (not)resizable the window. */
void view_set_keep_ratio(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	if (settings_get_bool(SETTINGS_KEEP_RATIO)) {
		view->scaley=view->scalex;
	}
	view_resize(view);
	view_redraw(settings, key, user_data);
	END_FUNC
}

/* Redraw the key to the window */
void view_update(struct view *view, struct key *key, gboolean statechange)
{
	START_FUNC
	GdkRectangle live_rect;
	GdkCursor *cursor;
	struct keyboard *kbd;
	gdouble x, y, w, h, xmargin, ymargin, zx, zy;
	gboolean focus_zoom;

	if (!view->window) return;
	if (key) {
		if (statechange) {
			if (view->symbols) cairo_surface_destroy(view->symbols);
			view->symbols=NULL;
			gtk_widget_queue_draw(GTK_WIDGET(view->window));
		} else {
			/*
			 * Damage must use the same scale as view_expose
			 * (view->scalex/y). keyboard_key_getrect() still
			 * multiplies by SETTINGS_SCALEX/Y; after RandR
			 * portrait fit, view_live_scale updates only the
			 * live scale, so settings-based damage is SE-shifted
			 * and only ~1/4 of the key highlight redraws.
			 */
			kbd = (struct keyboard *)key_get_keyboard(key);
			zx = view->scalex;
			zy = view->scaley;
			if (zx < 1.0)
				zx = settings_get_double(SETTINGS_SCALEX);
			if (zy < 1.0)
				zy = settings_get_double(SETTINGS_SCALEY);
			x = kbd->xpos + (key->x - (key->w / 2.0));
			y = kbd->ypos + (key->y - (key->h / 2.0));
			w = key->w;
			h = key->h;
			focus_zoom = status_focus_zoom_get(view->status);
			if (focus_zoom) {
				xmargin = (w * zx *
				    (settings_get_double(SETTINGS_FOCUS_ZOOM) -
					1.0)) + 5.0;
				ymargin = (h * zy *
				    (settings_get_double(SETTINGS_FOCUS_ZOOM) -
					1.0)) + 5.0;
			} else {
				xmargin = 5.0;
				ymargin = 5.0;
			}
			live_rect.x = (gint)((x * zx) - xmargin);
			live_rect.y = (gint)((y * zy) - ymargin);
			live_rect.width = (gint)((w * zx) + (xmargin * 2.0));
			live_rect.height = (gint)((h * zy) + (ymargin * 2.0));
			gdk_window_invalidate_rect(
				gtk_widget_get_window(GTK_WIDGET(view->window)),
				&live_rect, TRUE);
		}
	}
	if (status_focus_get(view->status)) {
		if (!view->hand_cursor) {
			cursor=gdk_cursor_new_for_display(
			    gtk_widget_get_display(GTK_WIDGET(view->window)),
			    GDK_HAND2);
			gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(view->window)), cursor);
			view->hand_cursor=TRUE;
		}
	} else if (view->hand_cursor) {
		gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(view->window)), NULL);
		view->hand_cursor=FALSE;
	}
	END_FUNC
}

/* on screen change event: check for composite extension */
void view_screen_changed (GtkWidget *widget, GdkScreen *old_screen, struct view *view)
{
	START_FUNC
	GdkVisual *visual;
	if (gdk_screen_is_composited(gtk_widget_get_screen(widget))) {
		flo_info(_("X11 composite extension detected. Semi-transparency is enabled."));
		if (view) view->composite=TRUE;
		visual=gdk_screen_get_rgba_visual(gdk_screen_get_default());
		if (visual==NULL) visual=gdk_screen_get_system_visual(gdk_screen_get_default());
		gtk_widget_set_visual(widget, visual);
	} else { 
		flo_info(_("Your screen does not support alpha channel. Semi-transparency is disabled"));
		if (view) view->composite=FALSE;
	}
	END_FUNC
}

/* on configure events: record window position */
void view_configure (GtkWidget *window, GdkEventConfigure* pConfig, struct view *view)
{
	START_FUNC
	GdkRectangle rect;
	gint xpos, ypos;
	if ((!view->window)||(!gtk_widget_get_visible(window))) return;

	/*
	 * Live-resize / live-move own geometry. Configure size chatter during
	 * move rebuilds glyphs at jittery scales (wavy labels); ignore it.
	 */
	if (view->status && status_get_resizing(view->status)) {
		END_FUNC
		return;
	}

	/* record window position */
	if (gtk_window_get_decorated(GTK_WINDOW(view->window)))
		gtk_window_get_position(GTK_WINDOW(view->window), &xpos, &ypos);
	else { xpos=pConfig->x; ypos=pConfig->y; }
	if (settings_get_int(SETTINGS_XPOS)!=xpos)
		settings_set_int(SETTINGS_XPOS, xpos);
	if (settings_get_int(SETTINGS_YPOS)!=ypos)
		settings_set_int(SETTINGS_YPOS, ypos);

	if (view->status && status_get_moving(view->status)) {
		END_FUNC
		return;
	}

	/* handle resize events */
	if ((pConfig->width!=view->width) || (pConfig->height!=view->height)) {
		if (settings_get_bool(SETTINGS_KEEP_RATIO)) {
			view->scalex=view->scaley=(gdouble)pConfig->width/view->vwidth;
		} else {
			view->scalex=(gdouble)pConfig->width/view->vwidth;
			view->scaley=(gdouble)pConfig->height/view->vheight;
		}
		if ((view->scalex>200.0)||(view->scaley>200.0))
			flo_warn(_("Window size out of range :%d, %d"), view->scalex, view->scaley);
		else {
			settings_set_double(SETTINGS_SCALEX, view->scalex, FALSE);
			settings_set_double(SETTINGS_SCALEY, view->scaley, FALSE);
		}
		view->width=pConfig->width; view->height=pConfig->height;
		if (view->background) cairo_surface_destroy(view->background);
		view->background=NULL;
		if (view->symbols) cairo_surface_destroy(view->symbols);
		view->symbols=NULL;
		view_create_window_mask(view);
		rect.x=0; rect.y=0;
		rect.width=pConfig->width; rect.height=pConfig->height;
		gtk_widget_size_allocate(GTK_WIDGET(view->window), &rect);
		gdk_window_invalidate_rect(gtk_widget_get_window(GTK_WIDGET(view->window)), &rect, TRUE);
	}

	END_FUNC
}

/* draw the background of the keyboard */
void view_draw_background (struct view *view, cairo_t *context)
{
	START_FUNC
	/* prepare the background */
	if (!view->background) {
		view_background_draw(view, context);
	}

	/* paint the background (never stretch - live-scale rebuilds art) */
	cairo_set_operator(context, CAIRO_OPERATOR_OVER);
	cairo_set_source_surface(context, view->background, 0, 0);
	cairo_paint(context);
	END_FUNC
}

/* draw a list of keys (latched or locked keys) */
void view_draw_list (struct view *view, cairo_t *context, GList *list)
{
	START_FUNC
	struct keyboard *keyboard;
	struct key *key;
	while (list) {
		key=(struct key *)list->data;
		keyboard=(struct keyboard *)key_get_keyboard(key);
		keyboard_press_draw(keyboard, context, view->style, key, view->status);
		list=list->next;
	}
	END_FUNC
}

/* draw a single key (pressed or focused) */
void view_draw_key (struct view *view, cairo_t *context, struct key *key)
{
	START_FUNC
	struct keyboard *keyboard;
	if (key) {
		keyboard=(struct keyboard *)key_get_keyboard(key);
		keyboard_focus_draw(keyboard, context,
			(gdouble)cairo_xlib_surface_get_width(view->background),
			(gdouble)cairo_xlib_surface_get_height(view->background),
			view->style, key, view->status);
	}
	END_FUNC
}

/* on draw event: draws the keyboards to the window */
void view_expose (GtkWidget *window, cairo_t* context, struct view *view)
{
	START_FUNC
	enum key_state state;

	/*
	 * Do not SOURCE-fill with opaque black during live-resize: with
	 * transparent=false (FVWM session) that paints a solid slab. Idle and
	 * move already leave gaps see-through via unpainted / shaped pixels;
	 * live_scale rebuilds art + mask each frame instead of wiping.
	 */
	if (settings_get_bool(SETTINGS_TRANSPARENT)) {
		cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 0.0);
		cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
		cairo_paint(context);
		cairo_set_operator(context, CAIRO_OPERATOR_OVER);
	}

	view_draw_background(view, context);

	/* draw the symbols */
	if (!view->symbols) {
		view_symbols_draw(view, context);
	}
	cairo_set_source_surface(context, view->symbols, 0, 0);
	cairo_paint(context);

	/* handle composited transparency */
	/* TODO: check for transparency support in WM */
	if (view->composite && settings_get_double(SETTINGS_OPACITY)!=100.0) {
		if (settings_get_double(SETTINGS_OPACITY)>100.0 ||
			settings_get_double(SETTINGS_OPACITY)<1.0) {
			flo_error(_("Window opacity out of range (1.0 to 100.0): %f"),
				settings_get_double(SETTINGS_OPACITY));
		}
		cairo_set_source_rgba(context, 0.0, 0.0, 0.0,
			(100.0-settings_get_double(SETTINGS_OPACITY))/100.0);
		cairo_set_operator(context, CAIRO_OPERATOR_DEST_OUT);
		cairo_paint(context);
		cairo_set_operator(context, CAIRO_OPERATOR_OVER);
	}

	cairo_save(context);
	cairo_scale(context, view->scalex, view->scaley);

	/* draw highlights (pressed keys) */
	view_draw_list(view, context, status_list_latched(view->status));
	view_draw_list(view, context, status_list_locked(view->status));

	/* pressed and focused key */
	view_draw_key(view, context, status_focus_get(view->status));
	if (status_pressed_get(view->status)) {
		state=status_pressed_get(view->status)->state;
		key_state_set(status_pressed_get(view->status), KEY_PRESSED);
		view_draw_key(view, context, status_pressed_get(view->status));
		key_state_set(status_pressed_get(view->status), state);
	}

	cairo_restore(context);

#ifdef ENABLE_RAMBLE
	if (view->ramble) ramble_draw(view->ramble, context);
#endif

	/* restore configure event handler (not during live-resize/move). */
	if (!view->configure_handler &&
	    !(view->status && (status_get_resizing(view->status) ||
	      status_get_moving(view->status))))
		view->configure_handler=g_signal_connect(G_OBJECT(view->window), "configure-event",
			G_CALLBACK(view_configure), view);
	END_FUNC
}

/*
 * Match Caps/Num (and other XKB lockers) to the real lock LEDs so the OSK
 * shows green when active - needed in tablet mode with no physical LED view.
 */
static void
view_sync_lockers(struct view *view)
{
	START_FUNC
#ifdef ENABLE_XKB
	Display *dpy;
	XkbStateRec st;
	GSList *kblist, *keylist;
	struct keyboard *kb;
	struct key *key;
	GdkModifierType mod;
	gboolean want_locked, is_locked;

	if (!view || !view->status || !view->status->xkeyboard) {
		END_FUNC
		return;
	}
	dpy = (Display *)gdk_x11_get_default_xdisplay();
	XkbGetState(dpy, XkbUseCoreKbd, &st);
	view->status->xkeyboard->xkb_state = st;

	for (kblist = view->keyboards; kblist; kblist = kblist->next) {
		kb = (struct keyboard *)kblist->data;
		if (!kb || !keyboard_activated(kb))
			continue;
		for (keylist = kb->keys; keylist; keylist = keylist->next) {
			key = (struct key *)keylist->data;
			if (!key || !key_is_locker(key))
				continue;
			mod = key_get_modifier(key);
			if (!mod)
				continue;
			want_locked = (st.locked_mods & mod) != 0;
			is_locked = (key->state == KEY_LOCKED);
			if (want_locked != is_locked)
				fsm_process(view->status, key, FSM_PRESSED);
		}
	}
#endif
	END_FUNC
}

/* on keys changed events */
void view_on_keys_changed(gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
#ifdef ENABLE_XKB
	XkbStateRec st;
	Display *dpy;
	guint group;
#endif

	/* Caps/Num LED sync only - no full redraw (that flickered hover). */
	view_sync_lockers(view);

#ifdef ENABLE_XKB
	/*
	 * Only wipe symbols when the layout group changes (key labels change).
	 * Lock-bit notifies must not destroy the surface / queue_draw all.
	 */
	dpy = (Display *)gdk_x11_get_default_xdisplay();
	if (XkbGetState(dpy, XkbUseCoreKbd, &st) == Success) {
		group = (guint)st.group;
		if (!view->have_xkb_group || group != view->last_xkb_group) {
			view->have_xkb_group = TRUE;
			view->last_xkb_group = group;
			if (view->symbols) {
				cairo_surface_destroy(view->symbols);
				view->symbols = NULL;
			}
			if (view->window)
				gtk_widget_queue_draw(GTK_WIDGET(view->window));
		}
	}
#endif
	END_FUNC
}

/* track the windows state changes */
void view_window_state (GtkWidget *window, GdkEventWindowState *event, struct view *view)
{
	START_FUNC
	gint is_iconified=gdk_window_get_state(gtk_widget_get_window(window))&GDK_WINDOW_STATE_ICONIFIED;
	if (is_iconified) {
		view_hide(view);
		gtk_window_deiconify(GTK_WINDOW(view->window));
	} 
	END_FUNC
}


/* Triggered by gconf when the "extensions" parameter is changed. */
void view_update_extensions(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	GSList *list=view->keyboards;
	struct keyboard *keyboard;

	/* Do not call configure signal handler */
	if (view->configure_handler) g_signal_handler_disconnect(G_OBJECT(view->window), view->configure_handler);
	view->configure_handler=0;

	while (list)
	{
		keyboard=(struct keyboard *)list->data;
		keyboard_status_update(keyboard, view->status);
		list=list->next;
	}

	view_set_dimensions(view);
	view_resize(view);
	if (view->background) cairo_surface_destroy(view->background);
	view->background=NULL;
	if (view->symbols) cairo_surface_destroy(view->symbols);
	view->symbols=NULL;
	view_create_window_mask(view);
	status_focus_set(view->status, NULL);
	gtk_widget_queue_draw(GTK_WIDGET(view->window));
	END_FUNC
}

/* Triggered by gconf when the "zoom" parameter is changed. */
void view_set_scalex(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	/* Do not call configure signal handler */
	if (view->configure_handler) g_signal_handler_disconnect(G_OBJECT(view->window), view->configure_handler);
	view->configure_handler=0;
	view->scalex=settings_get_double(SETTINGS_SCALEX);
	if (settings_get_bool(SETTINGS_KEEP_RATIO)) view->scaley=view->scalex;
	view_update_extensions(settings, key, user_data);
	END_FUNC
}

/* Triggered by gconf when the "zoom" parameter is changed. */
void view_set_scaley(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	/* Do not call configure signal handler */
	if (view->configure_handler) g_signal_handler_disconnect(G_OBJECT(view->window), view->configure_handler);
	view->configure_handler=0;
	view->scaley=settings_get_double(SETTINGS_SCALEY);
	if (settings_get_bool(SETTINGS_KEEP_RATIO)) view->scalex=view->scaley;
	view_update_extensions(settings, key, user_data);
	END_FUNC
}

/* Triggered by gconf when the "opacity" parameter is changed. */
void view_set_opacity(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct view *view=(struct view *)user_data;
	gtk_widget_queue_draw(GTK_WIDGET(view->window));
	END_FUNC
}

/* get gtk window of the view */
GtkWindow *view_window_get (struct view *view)
{
	START_FUNC
	END_FUNC
	return view->window;
}

/* get gtk window of the view */
void view_status_set (struct view *view, struct status *status)
{
	START_FUNC
	view->status=status;
	END_FUNC
}

/* liberate all the memory used by the view */
void view_free(struct view *view)
{
	START_FUNC
	if (view->background) cairo_surface_destroy(view->background);
	if (view->symbols) cairo_surface_destroy(view->symbols);
	g_free(view);
	END_FUNC
}

/* create a view of florence */
struct view *view_new (struct status *status, struct style *style, GSList *keyboards)
{
	START_FUNC
	struct view *view=g_malloc(sizeof(struct view));
	if (!view) flo_fatal(_("Unable to allocate memory for view"));
	memset(view, 0, sizeof(struct view));

	view->status=status;
	view->style=style;
	view->keyboards=keyboards;
	view->scalex=settings_get_double(SETTINGS_SCALEX);
	view->scaley=settings_get_double(SETTINGS_SCALEY);
	view_set_dimensions(view);
	view->window=GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	gtk_window_set_keep_above(view->window, settings_get_bool(SETTINGS_ALWAYS_ON_TOP));
 	gtk_window_set_accept_focus(view->window, FALSE);
	gtk_window_set_skip_taskbar_hint(view->window, !settings_get_bool(SETTINGS_TASK_BAR));
	view_resize(view);
	gtk_container_set_border_width(GTK_CONTAINER(view->window), 0);
	gtk_widget_set_events(GTK_WIDGET(view->window),
		GDK_EXPOSURE_MASK|GDK_POINTER_MOTION_HINT_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|
		GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK|GDK_STRUCTURE_MASK|GDK_POINTER_MOTION_MASK);
	gtk_widget_set_app_paintable(GTK_WIDGET(view->window), TRUE);
	gtk_window_set_decorated(view->window, settings_get_bool(SETTINGS_DECORATED));
#ifndef FLORENCE_GREETER
	/* Greeter-like open pos, raised by the Start/taskbar height. */
	session_place_keyboard(view);
	/* After place/fit - click-restore must match the size the user saw. */
	if (status)
		status->resize_scale_launch = view->scalex;
#else
	gtk_window_move(view->window, settings_get_int(SETTINGS_XPOS), settings_get_int(SETTINGS_YPOS));
	if (status) {
		status->move_launch_x = settings_get_int(SETTINGS_XPOS);
		status->move_launch_y = settings_get_int(SETTINGS_YPOS);
		status->move_launch_valid = TRUE;
		status->resize_scale_launch = view->scalex;
	}
#endif
	/*g_signal_connect(gdk_keymap_get_default(), "keys-changed", G_CALLBACK(view_on_keys_changed), view);*/
	xkeyboard_register_events(status->xkeyboard, view_on_keys_changed, (gpointer)view);
	g_signal_connect(G_OBJECT(view->window), "screen-changed", G_CALLBACK(view_screen_changed), view);
	view->configure_handler=g_signal_connect(G_OBJECT(view->window), "configure-event",
		G_CALLBACK(view_configure), view);
	g_signal_connect(G_OBJECT(view->window), "draw", G_CALLBACK(view_expose), view);
	g_signal_connect(G_OBJECT(view->window), "window-state-event", G_CALLBACK(view_window_state), view);
	view_screen_changed(GTK_WIDGET(view->window), NULL, view);
	g_signal_connect(G_OBJECT(view->window), "destroy", G_CALLBACK(view_on_destroy), view);
	/*
	 * hide-on-start: realize for shape/mask, but do not map - mapping then
	 * hiding flashes the full keyboard for a frame (glyph-only startup).
	 */
	if (settings_get_bool(SETTINGS_HIDE_ON_START)) {
		gtk_widget_realize(GTK_WIDGET(view->window));
#ifndef FLORENCE_GREETER
		/* gtk_window_move before realize is often ignored; re-apply. */
		session_place_keyboard(view);
		if (gtk_widget_get_window(GTK_WIDGET(view->window)) &&
		    view->status && view->status->move_launch_valid)
			gdk_window_move(gtk_widget_get_window(GTK_WIDGET(view->window)),
			    view->status->move_launch_x,
			    view->status->move_launch_y);
#endif
		if (gtk_widget_get_window(GTK_WIDGET(view->window)))
			view_create_window_mask(view);
	} else {
		gtk_widget_show(GTK_WIDGET(view->window));
		view_create_window_mask(view);
	}

	/* register settings callbacks */
	settings_changecb_register(SETTINGS_TRANSPARENT, view_set_transparent, view);
	settings_changecb_register(SETTINGS_DECORATED, view_set_decorated, view);
	settings_changecb_register(SETTINGS_RESIZABLE, view_set_resizable, view);
	settings_changecb_register(SETTINGS_ALWAYS_ON_TOP, view_set_always_on_top, view);
	settings_changecb_register(SETTINGS_TASK_BAR, view_set_task_bar, view);
	settings_changecb_register(SETTINGS_KEEP_RATIO, view_set_keep_ratio, view);
	settings_changecb_register(SETTINGS_SCALEX, view_set_scalex, view);
	settings_changecb_register(SETTINGS_SCALEY, view_set_scaley, view);
	settings_changecb_register(SETTINGS_OPACITY, view_set_opacity, view);
	settings_changecb_register(SETTINGS_EXTENSIONS, view_update_extensions, view);
	settings_changecb_register(SETTINGS_KEY, view_redraw, view);
	settings_changecb_register(SETTINGS_OUTLINE, view_redraw, view);
	settings_changecb_register(SETTINGS_LABEL, view_redraw, view);
	settings_changecb_register(SETTINGS_LABEL_OUTLINE, view_redraw, view);
	settings_changecb_register(SETTINGS_ACTIVATED, view_redraw, view);
	settings_changecb_register(SETTINGS_LATCHED, view_redraw, view);
	settings_changecb_register(SETTINGS_SYSTEM_FONT, view_redraw, view);
	settings_changecb_register(SETTINGS_FONT, view_redraw, view);

	/* set the window icon */
	tools_set_icon(view->window);
#ifndef FLORENCE_GREETER
	session_watch_monitors(view);
#endif
	END_FUNC
	return view;
}

/* Change the layout and style of the view and redraw */
void view_update_layout(struct view *view, struct style *style, GSList *keyboards)
{
	START_FUNC
	view->style=style;
	view->keyboards=keyboards;
	xkeyboard_register_events(view->status->xkeyboard, view_on_keys_changed, (gpointer)view);
	view_update_extensions(NULL, NULL, (gpointer)view);
	END_FUNC
}

