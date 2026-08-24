/* 
 * florence - Florence is a simple virtual keyboard for Gnome.

 * Copyright (C) 2014 François Agrech

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

#include "controller.h"
#include "trace.h"
#include "settings.h"
#include "tools.h"
#include "lib/florence.h"
#include "florence.h"
#include <cairo-xlib.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <gdk/gdkx.h>

#define MOVING_THRESHOLD 15

/* Match fvwm-florence glyph well (taskbar + margin). */
#define SESSION_ICON_W 64
#define SESSION_ICON_H 44
#define SESSION_ICON_MARGIN 20
#define SESSION_TASKBAR_H 40

RsvgHandle *handle;

/*
 * gtk_widget_destroy on the float icon from our own teardown must not exit
 * Florence. A WM Close/Destroy on the glyph (FVWM WindowOps) should.
 */
static gboolean controller_icon_destroy_internal = FALSE;

static guint session_icon_debounce_id;

static int
session_ctl_name_is_internal(const char *name)
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
session_ctl_panel_xrandr(gint *px, gint *py, gint *pw, gint *ph)
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
		if (session_ctl_name_is_internal(oi->name)) {
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

static void
session_ctl_icon_xy(gint *ix, gint *iy)
{
	const char *et, *em;
	gint px, py, pw, ph, taskbar, margin;

	if (!session_ctl_panel_xrandr(&px, &py, &pw, &ph)) {
		*ix = settings_get_int(SETTINGS_CONTROLLER_ICON_XPOS);
		*iy = settings_get_int(SETTINGS_CONTROLLER_ICON_YPOS);
		return;
	}
	em = getenv("FLORENCE_ICON_MARGIN");
	margin = (em && atoi(em) > 0) ? atoi(em) : SESSION_ICON_MARGIN;
	if (florence_in_greeter()) {
		/* No Start bar on the XDM greeter. */
		taskbar = 0;
	} else {
		et = getenv("FLORENCE_TASKBAR_H");
		taskbar = (et && atoi(et) > 0) ? atoi(et) : SESSION_TASKBAR_H;
	}
	*ix = px + pw - SESSION_ICON_W - margin;
	*iy = py + ph - taskbar - SESSION_ICON_H - margin;
	if (*ix < px)
		*ix = px;
	if (*iy < py)
		*iy = py;
}

static void
session_ctl_place_icon(struct controller *controller)
{
	gint ix, iy;

	if (!controller || !controller->controller_icon)
		return;
	session_ctl_icon_xy(&ix, &iy);
	/* gtk move only — do not settings_set (keyfile rewrite). */
	gtk_window_move(GTK_WINDOW(controller->controller_icon), ix, iy);
}

static gboolean
session_ctl_monitors_idle(gpointer data)
{
	struct controller *controller = (struct controller *)data;

	session_icon_debounce_id = 0;
	session_ctl_place_icon(controller);
	return FALSE;
}

static void
session_ctl_on_monitors_changed(GdkScreen *screen, gpointer data)
{
	struct controller *controller = (struct controller *)data;

	(void)screen;
	if (session_icon_debounce_id)
		g_source_remove(session_icon_debounce_id);
	session_icon_debounce_id = g_timeout_add(300,
	    session_ctl_monitors_idle, controller);
}

static void
session_ctl_watch_monitors(struct controller *controller)
{
	GdkScreen *screen;

	if (!controller)
		return;
	screen = gdk_screen_get_default();
	if (!screen)
		return;
	g_signal_handlers_disconnect_by_func(screen,
	    G_CALLBACK(session_ctl_on_monitors_changed), controller);
	g_signal_connect(screen, "monitors-changed",
	    G_CALLBACK(session_ctl_on_monitors_changed), controller);
	g_signal_connect(screen, "size-changed",
	    G_CALLBACK(session_ctl_on_monitors_changed), controller);
}

/* on expose event: display florence icon */
void controller_icon_expose (GtkWidget *window, cairo_t* context, void *userdata)
{
	START_FUNC
	gdouble w, h;
	w=gtk_widget_get_allocated_width(window);
	h=gtk_widget_get_allocated_height(window);

	cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 0.0);
	cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
	cairo_paint(context);
	cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
	style_render_svg(context, handle, w, h, FALSE, NULL);
	END_FUNC
}

/* create icon */
void controller_icon_create (struct controller *controller, GtkWindow **icon, gdouble scale)
{
	START_FUNC
	RsvgDimensionData dim;
	if (!*icon) {
		*icon=GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
		gtk_window_set_keep_above(*icon, TRUE);
		gtk_window_set_skip_taskbar_hint(*icon, TRUE);
		rsvg_handle_get_dimensions(handle, &dim);
		gtk_widget_set_size_request(GTK_WIDGET(*icon), dim.width, dim.height);
		gtk_container_set_border_width(GTK_CONTAINER(*icon), 0);
		gtk_window_set_decorated(*icon, FALSE);
		gtk_window_set_position(*icon, GTK_WIN_POS_MOUSE);
		gtk_window_set_accept_focus(*icon, FALSE);
		gtk_widget_set_events(GTK_WIDGET(*icon),
			GDK_EXPOSURE_MASK|GDK_POINTER_MOTION_HINT_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|
			GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK|GDK_STRUCTURE_MASK|GDK_POINTER_MOTION_MASK);
		g_signal_connect(G_OBJECT(*icon), "draw", G_CALLBACK(controller_icon_expose), NULL);
		g_signal_connect(G_OBJECT(*icon), "screen-changed",
			G_CALLBACK(view_screen_changed), NULL);
		view_screen_changed(GTK_WIDGET(*icon), NULL, NULL);
	}
	END_FUNC
}

#ifdef ENABLE_AT_SPI2

void controller_icon_hide (gpointer user_data);
void controller_icon_show (gpointer user_data);
void controller_set_mode (struct controller *controller);

/* Move the window to near the accessible onject. */
void controller_move_to(struct controller *controller)
{
	AtspiRect *rect;
	AtspiComponent *component=NULL;
	guint x, y;

	if (settings_get_bool(SETTINGS_MOVE_TO_WIDGET) && controller->obj) {
		component=atspi_accessible_get_component(controller->obj);
		if (component) {
			rect=atspi_component_get_extents(component, ATSPI_COORD_TYPE_SCREEN, NULL);
			if (rect->x<0) x=0; else x=(guint)rect->x;
			if (rect->y<0) y=0; else y=(guint)rect->y;
			florence_move_to(x, y, (unsigned int)rect->width, (unsigned int)rect->height);
			g_free(rect);
		}
	}
}

/* on button-press events: destroy the icon and show the actual keyboard */
void controller_autohide_icon_press (GtkWidget *window, GdkEventButton *event, gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	controller_icon_hide((gpointer)controller);
	controller_move_to(controller);
	florence_show();
	END_FUNC
}

/* Show an intermediate icon before showing the keyboard (if intermediate_icon is activated) 
 * otherwise, directly show the keyboard */
void controller_show (struct controller *controller)
{
	START_FUNC
	GtkWindow *icon=controller->autohide_icon;

	if (settings_get_bool(SETTINGS_INTERMEDIATE_ICON)) {
		florence_hide();
		controller_icon_create(controller, &(controller->autohide_icon), 2.0);
		if (!icon) {
			g_signal_connect(G_OBJECT(controller->autohide_icon), "button-press-event",
				G_CALLBACK(controller_autohide_icon_press), controller);
			florence_register(FLORENCE_SHOW, controller_icon_hide, controller);
			florence_register(FLORENCE_HIDE, controller_icon_show, controller);
		}
		tools_window_move(controller->autohide_icon, controller->obj);
		gtk_widget_show(GTK_WIDGET(controller->autohide_icon));
	} else {
		controller_move_to(controller);
		florence_show();
	}
	END_FUNC
}

/* Called to hide the icon */
void controller_icon_hide (gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	if (controller->autohide_icon) {
		gtk_widget_hide(GTK_WIDGET(controller->autohide_icon));
	}
	END_FUNC
}

/* Called to show the icon */
void controller_icon_show (gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	if (controller->autohide_icon && controller->obj) {
		controller_show(controller);
	}
	END_FUNC
}

/* debounce focus event timeout function */
static gboolean debounced_focus_event (gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;

	if (controller->next_obj) {
		if (controller->obj) g_object_unref(controller->obj);
		controller->obj = controller->next_obj;
		g_object_ref(controller->obj);
		controller_show(controller);
	} else {
		florence_hide();
		controller_icon_hide((gpointer)controller);
		if (controller->obj) g_object_unref(controller->obj);
		controller->obj=NULL;
	}
	END_FUNC
	return FALSE;
}

/* Called when a widget is focused.
 * Check if the widget is editable and show the keyboard or hide if not. */
void controller_focus_event (AtspiEvent *event, void *user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	GError *error=NULL;
	flo_debug(TRACE_DEBUG, _("ATSPI focus event received."));

	AtspiStateSet *state_set=atspi_accessible_get_state_set(event->source);
	AtspiRole role=atspi_accessible_get_role(event->source, &error);
	if (error) flo_error(_("Event error: %s"), error->message);
	flo_debug(TRACE_DEBUG, _("ATSPI focus event received. Object role=%d"), role);
	if (atspi_accessible_get_editable_text(event->source) ||
		((role==ATSPI_ROLE_TERMINAL ||
		(((role==ATSPI_ROLE_TEXT) || (role==ATSPI_ROLE_PASSWORD_TEXT)) &&
		state_set && atspi_state_set_contains(state_set, ATSPI_STATE_EDITABLE))) &&
		event->detail1))
		controller->next_obj=event->source;
	else
		controller->next_obj=NULL;
	
	if (controller->debounce_id)
		g_source_remove (controller->debounce_id);
	controller->debounce_id=g_timeout_add (controller->debounce,
		debounced_focus_event, user_data);
	END_FUNC
}

/* Registered the spi events to monitor focus and show on editable widgets. */
void controller_register_events (struct controller *controller)
{
	START_FUNC
	if (!controller->atspi_enabled) {
		flo_warn(_("SPI is disabled: Unable to switch auto-hide mode on."));
	} else {
		florence_hide();
		if (!atspi_event_listener_register_from_callback(controller_focus_event, (void*)controller, NULL, "object:state-changed:focused", NULL))
			flo_error(_("ATSPI listener register failed"));
		if (!atspi_event_listener_register_from_callback(controller_focus_event, (void*)controller, NULL, "focus:", NULL))
			flo_error(_("ATSPI listener register failed"));
	}
	END_FUNC
}

/* Deregistered the spi events. */
void controller_deregister_events (struct controller *controller)
{
	START_FUNC
	if (!atspi_event_listener_deregister_from_callback(controller_focus_event, (void*)controller, "object:state-changed:focused", NULL)) {
		flo_warn(_("AT SPI: problem deregistering focus listener"));
	}
	if (!atspi_event_listener_deregister_from_callback(controller_focus_event, (void*)controller, "focus:", NULL)) {
		flo_warn(_("AT SPI: problem deregistering window listener"));
	}
	controller_icon_hide((gpointer)controller);
	florence_show();
	controller->obj=NULL;
	END_FUNC
}

/* Triggered by gconf when the "auto_hide" parameter is changed. */
void controller_set_auto_hide(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	controller_set_mode(controller);
	if ((!settings_get_bool(SETTINGS_AUTO_HIDE)) && (controller->autohide_icon)) {
		gtk_widget_destroy(GTK_WIDGET(controller->autohide_icon));
		controller->autohide_icon=NULL;
	}
	END_FUNC
}

#endif

/* on press event: record position and wait for release. */
void controller_icon_on_press (GtkWidget *window, GdkEventButton *event, gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	GdkWindow *gdkw;
	GdkSeat *seat;

	controller->icon_moving=CONTROLLER_PRESSED;
	controller->xpos=(gint)event->x;
	controller->ypos=(gint)event->y;
	if (florence_in_greeter()) {
		/*
		 * Greeter glyph does not drag-move; grab so release-out still
		 * delivers release (touch often clamps widget-local coords).
		 */
		gdkw=gtk_widget_get_window(window);
		seat=gdk_display_get_default_seat(gdk_display_get_default());
		if (gdkw && seat)
			gdk_seat_grab(seat, gdkw, GDK_SEAT_CAPABILITY_ALL_POINTING,
			    FALSE, NULL, (GdkEvent *)event, NULL, NULL);
	}
	END_FUNC
}

/* on release event: show/hide the keyboard. */
void controller_icon_on_release (GtkWidget *window, GdkEventButton *event, gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	GtkAllocation alloc;
	GdkWindow *gdkw;
	GdkSeat *seat;
	gboolean inside;
	gint ox, oy, lx, ly;

	if (florence_in_greeter()) {
		seat=gdk_display_get_default_seat(gdk_display_get_default());
		if (seat)
			gdk_seat_ungrab(seat);

		if (controller->icon_moving==CONTROLLER_IMMOBILE) {
			END_FUNC
			return;
		}

		gtk_widget_get_allocation(window, &alloc);
		gdkw=gtk_widget_get_window(window);
		if (gdkw) {
			gdk_window_get_origin(gdkw, &ox, &oy);
			lx=(gint)event->x_root - ox;
			ly=(gint)event->y_root - oy;
		} else {
			lx=(gint)event->x;
			ly=(gint)event->y;
		}
		inside = (lx >= 0 && ly >= 0 &&
		    lx < alloc.width && ly < alloc.height);

		/* No right-click menu on the XDM greeter. */
		if (inside && event->button!=3)
			florence_toggle();
		controller->icon_moving=CONTROLLER_IMMOBILE;
		END_FUNC
		return;
	}

	if (controller->icon_moving==CONTROLLER_PRESSED) {
		if (event->button==3) florence_menu(event->time);
		else florence_toggle();
	}
	controller->icon_moving=CONTROLLER_IMMOBILE;
	END_FUNC
}

/*
 * FVWM WindowOps Close/Destroy on the float glyph: tear down the whole
 * session Florence (keyboard + glyph), not just the icon window.
 */
static void
controller_icon_on_destroy(GtkWidget *window, gpointer user_data)
{
	struct controller *controller=(struct controller *)user_data;

	if (controller && controller->controller_icon == GTK_WINDOW(window))
		controller->controller_icon=NULL;
	if (controller_icon_destroy_internal)
		return;
	if (florence_in_greeter())
		return;
	florence_hide();
	florence_terminate();
}

static gboolean
controller_icon_on_delete(GtkWidget *window, GdkEvent *event, gpointer user_data)
{
	(void)window;
	(void)event;
	(void)user_data;
	/* Default destroy path; destroy handler exits Florence. */
	return FALSE;
}

/* on move event: move the icon (session only; greeter glyph is fixed). */
void controller_icon_on_move (GtkWidget *window, GdkEventButton *event, gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	gint x, y, dx, dy;

	if (florence_in_greeter()) {
		(void)window;
		(void)event;
		(void)user_data;
		END_FUNC
		return;
	}

	gdk_device_get_position(gdk_device_manager_get_client_pointer(
		gdk_display_get_device_manager(gdk_display_get_default())), NULL, &x, &y);
	switch(controller->icon_moving) {
		case CONTROLLER_IMMOBILE: break;
		case CONTROLLER_PRESSED:
			dx=(gint)((GdkEventMotion*)event)->x-controller->xpos;
			dy=(gint)((GdkEventMotion*)event)->y-controller->ypos;
			if (dx > MOVING_THRESHOLD || dx < -MOVING_THRESHOLD ||
				dy > MOVING_THRESHOLD || dy < -MOVING_THRESHOLD)
				controller->icon_moving=CONTROLLER_MOVING;
			else break;
			/* no break */
		case CONTROLLER_MOVING:
			dx=x-controller->xpos;
			dy=y-controller->ypos;
			gtk_window_move(GTK_WINDOW(window), dx, dy);
			settings_set_int(SETTINGS_CONTROLLER_ICON_XPOS, dx);
			settings_set_int(SETTINGS_CONTROLLER_ICON_YPOS, dy);
			break;
		default:
			flo_warn(_("Controller: unknown moving state."));
			break;
	}
	END_FUNC
}

/* Set auto hide mode on or off. */
void controller_set_mode (struct controller *controller)
{
	START_FUNC
#ifdef ENABLE_AT_SPI2
	if (settings_get_bool(SETTINGS_AUTO_HIDE)) {
		controller_register_events(controller);
	} else {
		controller_deregister_events(controller);
	}
#endif
	END_FUNC
}

/* Activate or deactivate floating icon (depending on settings) */
void controller_float_icon (struct controller *controller)
{
	if (settings_get_bool(SETTINGS_CONTROLLER_FLOATICON)) {
		GtkWindow *icon=controller->controller_icon;
		controller_icon_create(controller, &(controller->controller_icon), 4.0);
		if (!icon) {
			g_signal_connect(G_OBJECT(controller->controller_icon), "button-press-event",
				G_CALLBACK(controller_icon_on_press), controller);
			g_signal_connect(G_OBJECT(controller->controller_icon), "button-release-event",
				G_CALLBACK(controller_icon_on_release), controller);
			g_signal_connect(G_OBJECT(controller->controller_icon), "motion-notify-event",
				G_CALLBACK(controller_icon_on_move), controller);
			g_signal_connect(G_OBJECT(controller->controller_icon), "leave-notify-event",
				G_CALLBACK(controller_icon_on_move), controller);
			g_signal_connect(G_OBJECT(controller->controller_icon), "delete-event",
				G_CALLBACK(controller_icon_on_delete), controller);
			g_signal_connect(G_OBJECT(controller->controller_icon), "destroy",
				G_CALLBACK(controller_icon_on_destroy), controller);
		}
		gtk_widget_show(GTK_WIDGET(controller->controller_icon));
		gtk_window_move(GTK_WINDOW(controller->controller_icon),
			settings_get_int(SETTINGS_CONTROLLER_ICON_XPOS),
			settings_get_int(SETTINGS_CONTROLLER_ICON_YPOS));
	} else {
		if (controller->controller_icon) {
			controller_icon_destroy_internal=TRUE;
			gtk_widget_destroy(GTK_WIDGET(controller->controller_icon));
			controller_icon_destroy_internal=FALSE;
		}
		controller->controller_icon=NULL;
	}
}

/* Triggered by conf when the "floaticon" parameter is changed. */
void controller_on_float_icon_change(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct controller *controller=(struct controller *)user_data;
	controller_float_icon(controller);
	END_FUNC
}

void controller_terminate (gpointer user_data)
{
	START_FUNC
	gtk_main_quit();
	END_FUNC
}

/* create a new instance of controller. */
struct controller *controller_new(guint debounce)
{
	START_FUNC
	GError *error=NULL;
	struct controller *controller=(struct controller *)g_malloc(sizeof(struct controller));
	if (!controller) flo_fatal(_("Unable to allocate memory for the controller"));
	memset(controller, 0, sizeof(struct controller));
	controller->debounce=debounce;

	handle=rsvg_handle_new_from_file(ICONDIR "/florence.svg", &error);
	if (error) flo_fatal(_("Error loading florence icon: %s"), error->message);

#ifdef ENABLE_AT_SPI2
	controller->atspi_enabled=TRUE;
	if (atspi_init()) {
		controller->atspi_enabled=FALSE;
		flo_warn(_("AT-SPI has been disabled at run time: auto-hide mode is disabled."));
	}
	settings_changecb_register(SETTINGS_AUTO_HIDE, controller_set_auto_hide, controller);

	if (settings_get_bool(SETTINGS_HIDE_ON_START) && (!settings_get_bool(SETTINGS_AUTO_HIDE))) {
		florence_hide();
	} else controller_set_mode(controller);
#else
	flo_warn(_("AT-SPI has been disabled at compile time: auto-hide mode is disabled."));
	if (settings_get_bool(SETTINGS_HIDE_ON_START)) {
		florence_hide();
	} else controller_set_mode(controller);
#endif
	settings_changecb_register(SETTINGS_CONTROLLER_FLOATICON, controller_on_float_icon_change, controller);
	controller_float_icon(controller);
	florence_register(FLORENCE_TERMINATE, controller_terminate, controller);
#ifndef FLORENCE_GREETER
	session_ctl_place_icon(controller);
	session_ctl_watch_monitors(controller);
#endif

	END_FUNC
	return controller;
}

/* liberate all the memory used by the controller */
void controller_free(struct controller *controller)
{
	START_FUNC

#ifdef ENABLE_AT_SPI2
	if (controller->autohide_icon) gtk_widget_destroy(GTK_WIDGET(controller->autohide_icon));
	controller->autohide_icon=NULL;
	atspi_exit();
#endif
	if (controller->controller_icon) {
		controller_icon_destroy_internal=TRUE;
		gtk_widget_destroy(GTK_WIDGET(controller->controller_icon));
		controller_icon_destroy_internal=FALSE;
	}
	controller->controller_icon=NULL;

	g_free(controller);
	END_FUNC
}

