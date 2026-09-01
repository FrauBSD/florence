/* 
   Florence - Florence is a simple virtual keyboard for Gnome.

   Copyright (C) 2012 François Agrech

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

#include "trace.h"
#include "status.h"
#include "settings.h"
#include <math.h>
#include <X11/Xproto.h>

/* check for record events every 1/10th of a second */
#define STATUS_EVENTCHECK_INTERVAL 100
/* animate keyboard every 1/50th of a second */
#define STATUS_ANIMATION_INTERVAL 20
/* show visual effect for touched keys for 200ms */
#define STATUS_TOUCH_TIMEOUT 200

/* terminate the application */
void status_terminate(struct status *status)
{
	START_FUNC
	view_destroy(status->view);
	END_FUNC
}

/* handle X11 errors */
int status_error_handler(Display *my_dpy, XErrorEvent *event)
{
	START_FUNC
	flo_warn(_("Unable to focus window."));
	END_FUNC
	return 0;
}

/* switch focus to focus window */
void status_focus_window(struct status *status)
{
	START_FUNC
	int (*old_handler)(Display *, XErrorEvent *);
	if (status->w_focus) {
		old_handler=XSetErrorHandler(status_error_handler);
		XSetInputFocus(gdk_x11_get_default_xdisplay(), status->w_focus->w,
				status->w_focus->revert_to, CurrentTime);
		XSync(gdk_x11_get_default_xdisplay(), FALSE);
		XSetErrorHandler(old_handler);
	}
	END_FUNC
}

/* update the global modifier mask */
void status_globalmod_set(struct status *status, GdkModifierType mod)
{
	START_FUNC
	status->globalmod|=mod;
	END_FUNC
}

#ifdef ENABLE_XRECORD
/* Called when a record event is received from XRecord */
void status_record_event (XPointer priv, XRecordInterceptData *hook)
{
	START_FUNC
	xEvent *event;
	struct status *status=(struct status *)priv;
	struct key *key;
	if (hook->category==XRecordFromServer) {
		event=(xEvent *)hook->data;
		if ((key=status->keys[event->u.u.detail])) {
			/*
			 * Ignore one KeyPress echo of Florence's own XTest.
			 * Always run KeyRelease so highlights clear for both
			 * OSK and hardware typing.
			 */
			if (event->u.u.type==KeyPress) {
				if (status->xtest_echo_code &&
				    status->xtest_echo_code ==
				    (unsigned int)event->u.u.detail) {
					status->xtest_echo_code = 0;
				} else
					fsm_process(status, key, FSM_PRESSED);
			} else if (event->u.u.type==KeyRelease) {
				if (status->xtest_echo_code ==
				    (unsigned int)event->u.u.detail)
					status->xtest_echo_code = 0;
				fsm_process(status, key, FSM_RELEASED);
			}
		}
	}
	if (hook) XRecordFreeData(hook);
	END_FUNC
}

/* Process record events (every 1/10th of a second) */
gboolean status_record_process (gpointer data)
{
	START_FUNC
	struct status *status=(struct status *)data;
	XRecordProcessReplies(status->data_disp);
	END_FUNC
	return TRUE;
}

/* Record keyboard events */
gpointer status_record_start (gpointer data)
{
	START_FUNC
	struct status *status=(struct status *)data;
	int major, minor;
	XRecordRange *range;
	XRecordClientSpec client;
	Display *ctrl_disp=(Display *)gdk_x11_get_default_xdisplay();

	status->data_disp=XOpenDisplay(NULL);
	if (XRecordQueryVersion(ctrl_disp, &major, &minor)) {
		flo_info(_("XRecord extension found version=%d.%d"), major, minor);
		if (!(range=XRecordAllocRange())) flo_fatal(_("Unable to allocate memory for record range"));
		memset(range, 0, sizeof(XRecordRange));
		range->device_events.first=KeyPress;
		range->device_events.last=KeyRelease;
		client=XRecordAllClients;
		if ((status->RecordContext=XRecordCreateContext(ctrl_disp, 0, &client, 1, &range, 1))) {
			XSync(ctrl_disp, TRUE);
			if (!XRecordEnableContextAsync(status->data_disp, status->RecordContext, status_record_event,
				(XPointer)status))
				flo_error(_("Unable to record events"));
		} else flo_warn(_("Unable to create xrecord context"));
		XFree(range);
	}
	else flo_warn(_("No XRecord extension found"));
	if (!status->RecordContext) flo_warn(_("Keyboard synchronization is disabled."));
	END_FUNC
	return data;
}

/* Stop recording keyboard events */
void status_record_stop (struct status *status)
{
	START_FUNC
	if (status->RecordContext) {
		XRecordDisableContext(status->data_disp, status->RecordContext);
		XRecordFreeContext(status->data_disp, status->RecordContext);
		status->RecordContext=0;
	}
	if (status->data_disp) {
		/* TODO: investigate why this is blocking */
		/* XCloseDisplay(status->data_disp); */
		status->data_disp=NULL;
	}
	END_FUNC
}

/* Add keys to the keycode indexed keys */
void status_keys_add(struct status *status, GSList *keys)
{
	START_FUNC
	GSList *list=keys;
	struct key *key;
	struct key_mod *mod;
	struct key_code *code;
	while (list) {
		key=(struct key *)(list->data);
		mod=(struct key_mod *)(key->mods->data);
		if (mod->type==KEY_CODE) {
			code=(struct key_code *)(mod->data);
			status->keys[code->code]=key;
		}
		list=list->next;
	}
	END_FUNC
}
#endif

/* update the focus key */
void status_focus_set(struct status *status, struct key *focus)
{
	START_FUNC
	struct key *old = status->focus;

	/*
	 * While a key is held (mouse button or finger down), do not change
	 * focus in a way that releases it. Touch jitter / tiny drags were
	 * cancelling auto-repeat on arrows and other hold keys.
	 */
	if (status->pressed && !status->moving && !status->resizing) {
		if (status->focus != status->pressed) {
			status->focus = status->pressed;
			view_update(status->view, old, FALSE);
			view_update(status->view, status->focus, FALSE);
		}
		END_FUNC
		return;
	}

	/* Same key: skip invalidate + pressed_set(NULL) (hover flicker). */
	if (old == focus) {
		END_FUNC
		return;
	}

	status->focus=focus;
	view_update(status->view, old, FALSE);
	view_update(status->view, status->focus, FALSE);
	if(!status->moving && !status->resizing) status_pressed_set(status, NULL);
	END_FUNC
}

/* return the focus key */
struct key *status_focus_get(struct status *status)
{
	START_FUNC
	END_FUNC
	return status->focus;
}

/* return the currently pressed key */
struct key *status_pressed_get(struct status *status) { return status->pressed; }

/* update the pressed key: send the press event and update the view 
 * if pressed is NULL, then release the last pressed key.
 * WARNING: not multi-touch safe! */
void status_pressed_set(struct status *status, struct key *pressed)
{
	START_FUNC
	enum fsm_event event;
	gboolean touch=(status_im_get(status)==STATUS_IM_TOUCH);

	/* find actions in fsm table */
	if (pressed) {
		event=FSM_PRESS;
		if ((!touch) || (key_get_action(pressed, status)==KEY_MOVE) ||
		    (key_get_action(pressed, status)==KEY_RESIZE))
			status->pressed=pressed;
	} else {
		event=FSM_RELEASE;
		if (touch && !((status->pressed)&&
		    ((key_get_action(status->pressed, status)==KEY_MOVE) ||
		     (key_get_action(status->pressed, status)==KEY_RESIZE)))) {
			/* Do not force-clear Caps/Num lock visuals on release. */
			if (status->pressed && (!key_get_modifier(status->pressed)) &&
			    !key_is_locker(status->pressed)) {
				key_state_set(status->pressed, KEY_RELEASED);
				view_update(status->view, status->pressed, FALSE);
			}
			status->pressed=status->focus;
		}
	}

	fsm_process(status, status->pressed, event);
	if (touch) {
		if (status->pressed &&
		    (key_get_modifier(status->pressed) || key_is_locker(status->pressed)))
			status->pressed=NULL;
	} else status->pressed=pressed;
	END_FUNC
}

/****************************/
/* FSM state change actions */
/****************************/

/* update the view according to the change */
void status_update_view (struct status *status, struct key *key)
{
	START_FUNC
	/*
	 * Stock Florence: view_update(..., key_get_modifier(key)).
	 * Non-zero modifiers (Shift, AltGr, Caps, ...) rebuild the symbols
	 * surface so labels switch to capitals / shifted punctuation.
	 *
	 * A later anti-flicker change forced FALSE always, which left Shift
	 * visually latched but still showing lowercase. Periodic XKB LED
	 * sync no longer full-redraws (view_on_keys_changed), so restoring
	 * the modifier flag is safe.
	 */
	if (status->view)
		view_update(status->view, key,
		    key && key_get_modifier(key) ? TRUE : FALSE);
	END_FUNC
}

/* update the key */
void status_update_key (struct status *status, struct key *key)
{
	START_FUNC
	if (!key_get_modifier(key)) {
		if (key->state==KEY_PRESSED) status->pressed=key;
		else if ((key->state==KEY_RELEASED) && (status->pressed==key)) status->pressed=NULL;
	}
	if (status->view) view_update(status->view, key, FALSE);
	END_FUNC
}

/* triggered after 200ms when a key has been touched. */
gboolean status_touch_timer(gpointer data)
{
	START_FUNC
	struct status *status=(struct status *)data;
	struct key *key=status->touch_key;

	/*
	 * Always clear the key that armed the timer. Using status->pressed alone
	 * left a prior F-key stuck PRESSED after another key was touched, which
	 * made that Fn media key dead for the rest of the session.
	 */
	if (key) {
		key_state_set(key, KEY_RELEASED);
		if (status->view) view_update(status->view, key, FALSE);
		if (status->pressed == key)
			status->pressed=NULL;
		status->touch_key=NULL;
	}
	status->touch_id=0;
	END_FUNC
	return FALSE;
}

/* send the press event */
void status_press (struct status *status, struct key *key)
{
	START_FUNC
	flo_debug(TRACE_DEBUG, _("sending press event"));
	status->pressed = key;
	key_press(key, status);
	/* Remember keycode so XRecord can drop our own KeyPress echo once. */
	if (key->xtest_code)
		status->xtest_echo_code = key->xtest_code;
#ifdef ENABLE_XRECORD
	{
		struct key_mod *m=key->mods ? (struct key_mod *)(key->mods->data) : NULL;
		gboolean synth=(!m || m->type==KEY_ACTION);

		if (!synth && m && m->type==KEY_CODE) {
			unsigned int native=((struct key_code *)m->data)->code;
			if (key->xtest_code && key->xtest_code != native)
				synth=TRUE;
		}
		if (synth)
			fsm_process(status, key, FSM_PRESSED);
	}
#else
	fsm_process(status, key, FSM_PRESSED);
#endif
#ifdef ENABLE_XRECORD
	status_record_process(status);
#endif
	END_FUNC
}

/* send the release event */
void status_release (struct status *status, struct key *key)
{
	START_FUNC
	flo_debug(TRACE_DEBUG, _("sending release event"));
	key_release(key, status);
	/* Always complete Florence FSM release (do not wait on XRecord). */
	fsm_process(status, key, FSM_RELEASED);
#ifdef ENABLE_XRECORD
	status_record_process(status);
#endif
	if (key_is_locker(key) && status->view) {
#ifdef ENABLE_XKB
		XSync(gdk_x11_get_default_xdisplay(), False);
#endif
		view_on_keys_changed(status->view);
	}
	END_FUNC
}

/* press all latched keys */
void status_press_latched (struct status *status, struct key *key)
{
	START_FUNC
	struct key *latched;
	GList *list=status->latched_keys;
	while (list) {
		latched=((struct key *)list->data);
		status_press(status, latched);
		list=list->next;
	}
	/* send "locked" modifier keys that are not lockers */
	list=status->locked_keys;
	while (list) {
		latched=((struct key *)list->data);
		if (!key_is_locker(latched)) {
			status_press(status, latched);
		}
		list=list->next;
	}
	END_FUNC
}

/* release all latched keys */
void status_release_latched (struct status *status, struct key *key)
{
	START_FUNC
	struct key *latched;
	GList *list=status->latched_keys;
	while (list) {
		latched=((struct key *)list->data);
		status_release(status, latched);
		list=list->next;
	}
	/* send "locked" modifier keys that are not lockers */
	list=status->locked_keys;
	while (list) {
		latched=((struct key *)list->data);
		if (!key_is_locker(latched)) {
			status_release(status, latched);
		}
		list=list->next;
	}
	END_FUNC
}

/* calculate globalmod according to latched and locked list */
void status_globalmod_calc(struct status *status)
{
	START_FUNC
	GdkModifierType globalmod=0;
	GList *list=status->latched_keys;
	while (list) {
		globalmod|=key_get_modifier((struct key *)list->data);
		list=list->next;
	}
	list=status->locked_keys;
	while (list) {
		globalmod|=key_get_modifier((struct key *)list->data);
		list=list->next;
	}
	status->globalmod=globalmod;
	END_FUNC
}

/* latch or lock a key (depending on state) */
void status_latchorlock (struct status *status, struct key *key, enum key_state state)
{
	START_FUNC
	GList **list=(state==KEY_LATCHED?&(status->latched_keys):&(status->locked_keys));
	*list=g_list_append(*list, key);
	/* update globalmod */
	status_globalmod_set(status, key_get_modifier(key));
	END_FUNC
}

/* unlatch or unlock a key (depending on state) */
void status_unlatchorlock (struct status *status, struct key *key, enum key_state state)
{
	START_FUNC
	GList **list=(state==KEY_LATCHED?&(status->latched_keys):&(status->locked_keys));
	GList *found;
	if ((found=g_list_find(*list, key)))
		*list=g_list_delete_link(*list, found);
	status_globalmod_calc(status);
	END_FUNC
}

/*
 * Touch bounce is typically <80ms between duplicate presses. Intentional
 * taps (latch->lock, lock->off, off->latch) are usually >=100ms apart.
 *
 * IMPORTANT: do not special-case RELEASED->LATCHED. After unlock, a bounce
 * press would immediately re-latch and the cycle became on/lock/on/lock
 * with no way to turn Shift off without typing a character.
 */
#define STATUS_MOD_BOUNCE_US	80000	/* 80 ms */

void
status_mod_state_entered(struct status *status)
{
	if (status)
		status->mod_state_entered_us = g_get_monotonic_time();
}

gboolean
status_mod_bounce_guard(struct status *status, enum key_state from,
	enum key_state to)
{
	gint64 now, dt;

	(void)from;
	(void)to;
	if (!status)
		return FALSE;
	/* First transition ever: no prior state time. */
	if (status->mod_state_entered_us == 0)
		return TRUE;
	now = g_get_monotonic_time();
	dt = now - status->mod_state_entered_us;
	if (dt < STATUS_MOD_BOUNCE_US)
		return FALSE;
	return TRUE;
}

/* latch a key */
void status_latch (struct status *status, struct key *key)
{
	START_FUNC
	status_latchorlock(status, key, KEY_LATCHED);
	END_FUNC
}

/* unlatch a key */
void status_unlatch (struct status *status, struct key *key)
{
	START_FUNC
	status_unlatchorlock(status, key, KEY_LATCHED);
	END_FUNC
}

/* unlatch all latched keys */
void status_unlatch_all (struct status *status, struct key *key)
{
	START_FUNC
	struct key *latched;
	while(status->latched_keys) {
		latched=(struct key *)(g_list_first(status->latched_keys)->data);
		latched->state=KEY_RELEASED;
		status->latched_keys=g_list_delete_link(status->latched_keys,
			g_list_first(status->latched_keys));
		if (status->view) view_update(status->view, latched, TRUE);
	}
	status_globalmod_calc(status);
	END_FUNC
}

/* True for Super/Mod4 (Florence left/right Super keys). */
gboolean
status_key_is_super(struct key *key)
{
	GdkModifierType m;
	struct key_mod *mod;
	guint code;

	if (!key)
		return FALSE;
	m = key_get_modifier(key);
	if (m & (GDK_SUPER_MASK | GDK_META_MASK | GDK_MOD4_MASK))
		return TRUE;
	/* Layout codes: Super_L=133, Super_R=134 (evdev / Xorg). */
	if (key->mods && key->mods->data) {
		mod = (struct key_mod *)key->mods->data;
		if (mod->type == KEY_CODE && mod->data) {
			code = ((struct key_code *)mod->data)->code;
			if (code == 133 || code == 134)
				return TRUE;
		}
	}
	return FALSE;
}

/* Emit a Super press/release (opens FVWM Start via Key Super_L binding). */
void
status_super_tap(struct status *status, struct key *key)
{
	struct key_mod *mod;
	guint code = 133;

	if (key && key->mods && key->mods->data) {
		mod = (struct key_mod *)key->mods->data;
		if (mod->type == KEY_CODE && mod->data)
			code = ((struct key_code *)mod->data)->code;
	}
	status_focus_window(status);
	status->spi = key_event(code, TRUE, status->spi);
	status->spi = key_event(code, FALSE, status->spi);
}

/* lock a key*/
void status_lock (struct status *status, struct key *key)
{
	START_FUNC
	status_latchorlock(status, key, KEY_LOCKED);
	END_FUNC
}

/* unlock a key */
void status_unlock (struct status *status, struct key *key)
{
	START_FUNC
	status_unlatchorlock(status, key, KEY_LOCKED);
	END_FUNC
}


/* returns the key currently focussed */
#ifdef ENABLE_RAMBLE
struct key *status_hit_get(struct status *status, gint x, gint y, enum key_hit *hit)
#else
struct key *status_hit_get(struct status *status, gint x, gint y)
#endif
{
	START_FUNC
	END_FUNC
#ifdef ENABLE_RAMBLE
	return view_hit_get(status->view, x, y, hit);
#else
	return view_hit_get(status->view, x, y);
#endif
}

/* start the timer */
void status_timer_start(struct status *status, GSourceFunc update, gpointer data)
{
	START_FUNC
	if (status->timer) g_timer_start(status->timer);
	else {
		status->timer=g_timer_new();
		g_timeout_add(STATUS_ANIMATION_INTERVAL, update, data);
	}
	END_FUNC
}

/* stop the timer */
void status_timer_stop(struct status *status)
{
	START_FUNC
	if (status->timer) {
		g_timer_destroy(status->timer);
		status->timer=NULL;
	}
	END_FUNC
}

/* get timer value */
gdouble status_timer_get(struct status *status)
{
	START_FUNC
	gdouble ret=0.0;
	if (status->timer)
		ret=g_timer_elapsed(status->timer, NULL)*1000./settings_get_double(SETTINGS_TIMER);
	END_FUNC
	return ret;
}

/* get the list of latched keys */
GList *status_list_latched(struct status *status) { return status->latched_keys; }

/* get the list of locked keys */
GList *status_list_locked(struct status *status) { return status->locked_keys; }

/* get the global modifier mask */
GdkModifierType status_globalmod_get(struct status *status) { return status->globalmod; }

/* find a child window of win to focus by its name */
struct status_focus *status_find_subwin(Window parent, const gchar *win)
{
	START_FUNC
	Window root_return, parent_return;
	Window *children;
	guint nchildren, idx;
	gchar *name;
	struct status_focus *focus=NULL;
	XWindowAttributes attrs;
	if (XQueryTree(gdk_x11_get_default_xdisplay(), parent,
		&root_return, &parent_return, &children, &nchildren)) {
		for (idx=0;(idx<nchildren) && (!focus);idx++) {
			XFetchName(gdk_x11_get_default_xdisplay(), children[idx], &name);
			XGetWindowAttributes(gdk_x11_get_default_xdisplay(), children[idx], &attrs);

			if (attrs.map_state==IsViewable && name && (!strcmp(name, win))) {
				focus=g_malloc(sizeof(struct status_focus));
				if (!focus) flo_fatal(_("Unable to allocate memory for status focus"));
				focus->w=children[idx];
				focus->revert_to=RevertToPointerRoot;
				flo_info(_("Found window %s (ID=%ld)"), win, children[idx]);
			} else {
				focus=status_find_subwin(children[idx], win);
			}
			if (name) XFree(name);
		}
	} else {
		flo_warn(_("XQueryTree failed."));
	}
	END_FUNC
	return focus;
}

/* find a window to focus by its name */
/* TODO: wait for window to exist */
struct status_focus *status_find_window(const gchar *win)
{
	START_FUNC
	gchar *name;
	struct status_focus *focus=NULL;
	if (win && win[0]) {
		focus=status_find_subwin(gdk_x11_get_default_root_xwindow(), win);
	}
	if (!focus) {
		focus=g_malloc(sizeof(struct status_focus));
		if (!focus) flo_fatal(_("Unable to allocate memory for status focus"));
		XGetInputFocus(gdk_x11_get_default_xdisplay(), &(focus->w),
			&(focus->revert_to));
		XFetchName(gdk_x11_get_default_xdisplay(), focus->w, &name);
		if (win[0]) {
			flo_warn(_("Window not found: %s, using last focused window: %s (ID=%d)"),
				win, name, focus->w);
		} else {
			flo_info(_("Focussing window %s (ID=%d)"), name, focus->w);
		}
		if (name) XFree(name);
	}
	END_FUNC
	return focus;
}

/* Set input method */
void status_im_set(struct status *status, gchar *val)
{
	START_FUNC
	if (!strcmp(val, "button")) status->input_method=STATUS_IM_BUTTON;
	else if (!strcmp(val, "timer")) status->input_method=STATUS_IM_TIMER;
#ifdef ENABLE_RAMBLE
	else if (!strcmp(val, "ramble")) status->input_method=STATUS_IM_RAMBLE;
#endif
	else if (!strcmp(val, "touch")) status->input_method=STATUS_IM_TOUCH;
	else {
		status->input_method=STATUS_IM_BUTTON;
		flo_warn(_("Unknown input method: %s ; using button input method"), val);
	}
	END_FUNC
}

/* Called on input method change */
void status_input_method(GSettings *settings, gchar *key, gpointer user_data)
{
	START_FUNC
	struct status *status=(struct status *)user_data;
	status_im_set(status, settings_get_string(SETTINGS_INPUT_METHOD));
	END_FUNC
}

/* get selected input method */
enum status_input_method status_im_get(struct status *status)
{
	START_FUNC
	END_FUNC
	return status->input_method;
}

/* allocate memory for status */
struct status *status_new(const gchar *focus_back)
{
	START_FUNC
	gchar *im;
	struct status *status=g_malloc(sizeof(struct status));
	if (!status) flo_fatal(_("Unable to allocate memory for status"));
	memset(status, 0, sizeof(struct status));
#ifdef ENABLE_XRECORD
	status_record_start(status);
	g_timeout_add(STATUS_EVENTCHECK_INTERVAL, status_record_process, (gpointer)status);
#endif
	status->spi=FALSE; /* XTest only - ATSPI+XTest double-delivered keycodes */
	if (focus_back) {
		status->w_focus=status_find_window(focus_back);
	}
	im=settings_get_string(SETTINGS_INPUT_METHOD);
	status_im_set(status, im);
	if (im) g_free(im);
	settings_changecb_register(SETTINGS_INPUT_METHOD, status_input_method, status);
	END_FUNC
	return status;
}

/* liberate status memory */
void status_free(struct status *status)
{
	START_FUNC
#ifdef ENABLE_XRECORD
	status_record_stop(status);
#endif
	if (status->xkeyboard) xkeyboard_free(status->xkeyboard);
	if (status->timer) g_timer_destroy(status->timer);
	if (status->latched_keys) g_list_free(status->latched_keys);
	if (status->locked_keys) g_list_free(status->locked_keys);
	if (status->w_focus) g_free(status->w_focus);
	if (status) g_free(status);
	END_FUNC
}

/* reset the status to its original state */
void status_reset(struct status *status)
{
	START_FUNC
	status->focus=NULL;
	status->pressed=NULL;
	if (status->timer) g_timer_destroy(status->timer);
	if (status->xkeyboard) xkeyboard_free(status->xkeyboard);
	status->timer=NULL;
	if (status->latched_keys) g_list_free(status->latched_keys);
	if (status->locked_keys) g_list_free(status->locked_keys);
	status->latched_keys=NULL;
	status->locked_keys=NULL;
	status->globalmod=0;
	END_FUNC
}

/* sets the view to update on status change */
void status_view_set(struct status *status, struct view *view)
{
	START_FUNC
	status->view=view;
	view_status_set(view, status);
	END_FUNC
}

/* disable sending of spi events: send xtest events instead */
void status_spi_disable(struct status *status)
{
	START_FUNC
#ifdef ENABLE_XTST
	int event_base, error_base, major, minor;
	if (!XTestQueryExtension(
		(Display *)gdk_x11_get_default_xdisplay(),
		&event_base, &error_base, &major, &minor)) {
		flo_error(_("Neither at-spi nor XTest could be initialized."));
		flo_fatal(_("There is no way we can send keyboard events."));
	} else {
		flo_info(_("At-spi registry daemon is not running. "
			"XTest extension found: version=%d.%d; "
			"It will be used instead of at-spi."), major, minor);
	}
#else
	flo_error(_("Xtest extension not compiled in and at-spi not working"));
	flo_fatal(_("There is no way we can send keyboard events."));
#endif
	status->spi=FALSE;
	END_FUNC
}

/* tell if spi is enabled */
gboolean status_spi_is_enabled(struct status *status) { return status->spi; }

/* set/get moving status (always Florence live-move + seat grab).
 * Click (no drag past slop): restore default open position. */
void status_set_moving(struct status *status, gboolean moving)
{
	START_FUNC
	GtkWindow *win;

	if (moving) {
		if (!status->moving) {
			status->move_dragged=FALSE;
			win=view_window_get(status->view);
			if (win)
				gtk_window_set_gravity(win, GDK_GRAVITY_STATIC);
		}
		status->moving=TRUE;
	} else if (status->moving) {
		status->moving=FALSE;
		if (status->move_grabbed) {
			GdkSeat *seat=gdk_display_get_default_seat(
			    gdk_display_get_default());
			if (seat) gdk_seat_ungrab(seat);
			status->move_grabbed=FALSE;
		}
		if (!status->move_dragged && status->view) {
			view_restore_open_position(status->view);
			/*
			 * Click-restore jumps the window; leave was UNGRAB or
			 * suppressed while moving, so the move key would stay
			 * focused and draw highlighted until the next motion.
			 */
			status->pressed = NULL;
			status->focus = NULL;
		}
		win=view_window_get(status->view);
		if (win)
			gtk_window_set_gravity(win, GDK_GRAVITY_NORTH_WEST);
	}
	END_FUNC
}
gboolean status_get_moving(struct status *status) { return status->moving; }

/* Live-resize: drag adjusts SCALEX/Y; click (no drag) restores launch size. */
void status_set_resizing(struct status *status, gboolean resizing)
{
	START_FUNC
	GtkWindow *win;
	GdkWindow *gdkw;

	if (resizing) {
		if (!status->resizing) {
			/*
			 * Baseline the live view scale, not GSettings. Portrait
			 * fit / session placement can leave view->scalex ahead of
			 * (or behind) the stored SCALEX; starting from settings
			 * then made the first drag frame shrink before growing.
			 */
			if (status->view && status->view->scalex >= 10.0)
				status->resize_scale0=status->view->scalex;
			else
				status->resize_scale0=settings_get_double(SETTINGS_SCALEX);
			status->resize_last=status->resize_scale0;
			status->resize_dragged=FALSE;
			win=view_window_get(status->view);
			if (win) {
				/*
				 * Pin client NW in root coords. gtk_window_get_position
				 * can disagree with the GDK/X window under FVWM frames
				 * and was part of the vacillation.
				 */
				gdkw=gtk_widget_get_window(GTK_WIDGET(win));
				if (gdkw) {
					gdk_window_get_origin(gdkw,
					    &status->resize_pin_x,
					    &status->resize_pin_y);
					status->resize_shell_w =
					    (guint)gdk_window_get_width(gdkw);
					status->resize_shell_h =
					    (guint)gdk_window_get_height(gdkw);
				} else {
					gtk_window_get_position(win,
					    &status->resize_pin_x,
					    &status->resize_pin_y);
					status->resize_shell_w =
					    status->view ? status->view->width : 0;
					status->resize_shell_h =
					    status->view ? status->view->height : 0;
				}
				gtk_window_set_gravity(win,
				    GDK_GRAVITY_NORTH_WEST);
			}
		}
		status->resizing=TRUE;
	} else if (status->resizing) {
		/* Commit while pin/scale are still valid; then clear the flag. */
		if (status->resize_grabbed) {
			GdkSeat *seat=gdk_display_get_default_seat(
			    gdk_display_get_default());
			if (seat) gdk_seat_ungrab(seat);
			status->resize_grabbed=FALSE;
		}
		if (status->view) {
			/*
			 * Click restores cold-start size; drag keeps the live
			 * scale. Keep live size only for an intentional drag:
			 * left the press-slop AND changed scale by >= 5% (min
			 * 1.0). Pointer-at-release checks were too tight under
			 * seat-grab chatter (CLI worse than FVWM): first click
			 * only committed a redraw (flash), second restored.
			 */
			gboolean restore = FALSE;

			if (status->resize_scale_launch > 0.0) {
				gdouble delta, min_drag;

				delta = fabs(status->view->scalex -
				    status->resize_scale0);
				min_drag = status->resize_scale0 * 0.05;
				if (min_drag < 1.0)
					min_drag = 1.0;
				if (!(status->resize_dragged &&
				    delta >= min_drag))
					restore = TRUE;
			}
			if (restore) {
				view_live_scale(status->view,
				    status->resize_scale_launch,
				    status->resize_pin_x,
				    status->resize_pin_y);
				/*
				 * Same stuck-hover case as move click-restore:
				 * leave suppressed during resize + layout jump.
				 */
				status->pressed = NULL;
				status->focus = NULL;
			}
			view_live_scale_commit(status->view);
		}
		status->resizing=FALSE;
		win=view_window_get(status->view);
		if (win)
			gtk_window_set_gravity(win, GDK_GRAVITY_NORTH_WEST);
	}
	END_FUNC
}
gboolean status_get_resizing(struct status *status) { return status->resizing; }

/* zoom the focused key */
void status_focus_zoom_set(struct status *status, gboolean focus_zoom) { status->focus_zoom=focus_zoom; }
gboolean status_focus_zoom_get(struct status *status) { return status->focus_zoom; }

