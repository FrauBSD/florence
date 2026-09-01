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

/* Note : this is both the viewer and the controller */

#include "system.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <glib/gprintf.h>
#ifdef ENABLE_HELP
	#include <gdk/gdkkeysyms.h>
#endif
#include <gdk/gdkx.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include "settings.h"
#include "trace.h"
#include "layoutreader.h"
#include "key.h"
#include "style.h"
#include "tools.h"
#include "settings-window.h"

#ifdef ENABLE_AT_SPI2
#define AT_SPI
#endif
#ifdef ENABLE_AT_SPI
#define AT_SPI
#endif

static struct settings_window *settings_window=NULL;
/* TRUE while syncing widgets from settings - ignore widget->settings signals. */
static gboolean settings_window_updating=FALSE;
void settings_window_extension(GtkToggleButton *button, gchar *name);
static gboolean settings_window_on_delete(GtkWidget *window, GdkEvent *event,
	gpointer data);

/*********************/
/* private functions */
/*********************/

/* Populate layout combobox with available layouts */
void settings_window_layouts_populate()
{
	START_FUNC
	GtkTreeIter iter;
	GtkCellRenderer *cell;
	DIR *dp=opendir(DATADIR "/layouts");
	struct dirent *ep;
	gchar *name;
	struct layout *layout;
	struct layout_infos *infos;

	if (dp!=NULL) {
		if (settings_window->layout_list) {
			gtk_list_store_clear(settings_window->layout_list);
			g_object_unref(G_OBJECT(settings_window->layout_list)); 
		}
		settings_window->layout_list=gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
		cell=gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_start(
			GTK_CELL_LAYOUT(gtk_builder_get_object(settings_window->gtkbuilder, "flo_layouts")),
			cell, FALSE);
		gtk_cell_layout_set_attributes(
			GTK_CELL_LAYOUT(gtk_builder_get_object(settings_window->gtkbuilder, "flo_layouts")),
			cell, "text", 0, NULL);
		while ((ep=readdir(dp))) {
			if (ep->d_name[0]!='.') {
				name=g_strdup_printf(DATADIR "/layouts/%s", ep->d_name);
				layout=layoutreader_new(name, NULL, DATADIR "/relaxng/florence.rng");
				layoutreader_element_open(layout, "layout");
				infos=layoutreader_infos_new(layout);
				gtk_list_store_append(settings_window->layout_list, &iter);
				gtk_list_store_set(settings_window->layout_list, &iter, 0, infos->name, 1, name, -1);
				layoutreader_infos_free(infos);
				layoutreader_free(layout);
				g_free(name);
			}
		}
		closedir(dp);
		gtk_combo_box_set_model(GTK_COMBO_BOX(gtk_builder_get_object(
				settings_window->gtkbuilder, "flo_layouts")),
			GTK_TREE_MODEL(settings_window->layout_list));
	} else flo_error(_("Couldn't open directory %s"), DATADIR "/layouts");
	END_FUNC
}

/* Populate input method combobox with available methods */
void settings_window_input_method_populate()
{
	START_FUNC
	GtkTreeIter iter;
	GtkCellRenderer *cell;

	if (settings_window->input_method_list) {
		gtk_list_store_clear(settings_window->input_method_list);
		g_object_unref(G_OBJECT(settings_window->input_method_list)); 
	}
	settings_window->input_method_list=gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	cell=gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(
		GTK_CELL_LAYOUT(gtk_builder_get_object(settings_window->gtkbuilder, "input_method_combo")),
		cell, FALSE);
	gtk_cell_layout_set_attributes(
		GTK_CELL_LAYOUT(gtk_builder_get_object(settings_window->gtkbuilder, "input_method_combo")),
		cell, "text", 0, NULL);
	gtk_list_store_append(settings_window->input_method_list, &iter);
	gtk_list_store_set(settings_window->input_method_list, &iter, 0, _("Mouse"), 1, "button", -1);
	gtk_list_store_append(settings_window->input_method_list, &iter);
	gtk_list_store_set(settings_window->input_method_list, &iter, 0, _("Touch screen"), 1, "touch", -1);
	gtk_list_store_append(settings_window->input_method_list, &iter);
	gtk_list_store_set(settings_window->input_method_list, &iter, 0, _("Timer"), 1, "timer", -1);
#ifdef ENABLE_RAMBLE
	gtk_list_store_append(settings_window->input_method_list, &iter);
	gtk_list_store_set(settings_window->input_method_list, &iter, 0, _("Ramble"), 1, "ramble", -1);
#endif
	gtk_combo_box_set_model(GTK_COMBO_BOX(gtk_builder_get_object(
			settings_window->gtkbuilder, "input_method_combo")),
		GTK_TREE_MODEL(settings_window->input_method_list));
	END_FUNC
}

/* fills the preview icon view with icons representing the themes */
void settings_window_preview_build()
{
	START_FUNC
	GtkTreeIter iter;
	GdkPixbuf *pixbuf;
	struct style *style;
	DIR *dp=opendir(DATADIR "/styles");
	struct dirent *ep;
	gchar *name;

	if (dp!=NULL) {
		if (settings_window->style_list) {
			gtk_list_store_clear(settings_window->style_list);
			g_object_unref(G_OBJECT(settings_window->style_list)); 
		}
		settings_window->style_list=gtk_list_store_new(2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
		gtk_icon_view_set_model(GTK_ICON_VIEW(gtk_builder_get_object(
				settings_window->gtkbuilder, "flo_preview")),
			GTK_TREE_MODEL(settings_window->style_list));
		while ((ep=readdir(dp))) {
			if (ep->d_name[0]!='.') {
				name=g_strdup_printf(DATADIR "/styles/%s", ep->d_name);
				style=style_new(name);
				pixbuf=style_pixbuf_draw(style);
				if (!pixbuf) flo_error(_("Unable to create preview for style %s"), name);
				else {
					gtk_list_store_append(settings_window->style_list, &iter);
					gtk_list_store_set(settings_window->style_list, &iter, 0,
						pixbuf, 1, ep->d_name, -1);
				}
				if (style) style_free(style);
				g_free(name);
				g_object_unref(G_OBJECT(pixbuf)); 
			}
		}
		closedir(dp);
	} else flo_error(_("Couldn't open directory %s"), DATADIR "/styles");
	END_FUNC
}

/* converts a color from string to gdk */
GdkRGBA *settings_window_convert_color(gchar *strcolor)
{
	START_FUNC
	static GdkRGBA ret;
	unsigned int r, g, b;
	sscanf(strcolor, "#%02x%02x%02x", &r, &g, &b);
	ret.red=((gdouble)r)/256.; ret.green=((gdouble)g)/256.; ret.blue=((gdouble)b)/256.;
	ret.alpha=1.0;
	END_FUNC
	return &ret;
}

/* update the extension check box list according to the layout and GSettings */
void settings_window_extensions_update(gchar *layoutname)
{
	START_FUNC
	GList *extensions;
	GtkWidget *extension;
	struct layout *layout;
	struct layout_extension *ext=NULL;
	GtkWidget *new;
	gchar **extstrs, **extstr;
	gchar *id;
	gchar *temp;

	GSList *list=settings_window->extensions;
	while (list) {
		g_free(list->data);
		list=list->next;
	}
	if (settings_window->extensions) g_slist_free(settings_window->extensions);
	settings_window->extensions=NULL;

	extensions=gtk_container_get_children(
		GTK_CONTAINER(gtk_builder_get_object(settings_window->gtkbuilder, "flo_extensions")));
	while (extensions) {
		extension=(GTK_WIDGET(extensions->data));
		extensions=extensions->next;
		/* TODO: g_free(id) */
		gtk_widget_destroy(extension);
	}
	layout=layoutreader_new(layoutname, NULL, DATADIR "/relaxng/florence.rng");
	layoutreader_element_open(layout, "layout");
	while ((ext=layoutreader_extension_new(layout))) {
		new=gtk_check_button_new_with_label(ext->name);
		id=g_strdup(ext->identifiant);
		settings_window->extensions=g_slist_append(settings_window->extensions, id);
		temp=settings_get_string(SETTINGS_EXTENSIONS);
		extstrs=extstr=g_strsplit(temp, ":", -1);
		while (extstr && *extstr &&
		    !settings_extension_id_equal(*extstr, id)) {
			extstr++;
		}
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(new), extstr && *extstr);
		g_strfreev(extstrs);
		if (temp) g_free(temp);
		g_signal_connect(G_OBJECT(new), "toggled",
			G_CALLBACK(settings_window_extension), id);
		gtk_box_pack_start(
			GTK_BOX(gtk_builder_get_object(settings_window->gtkbuilder, "flo_extensions")),
			new, FALSE, FALSE, 0);
		gtk_widget_show(new);
		layoutreader_extension_free(layout, ext);
	}
	layoutreader_free(layout);
	END_FUNC
}

/* update the layout settings according to GSettings */
gchar *settings_window_combo_update(gchar *item)
{
	START_FUNC
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *data=NULL;
	gchar *ret=NULL;
	GtkComboBox *combo=GTK_COMBO_BOX(gtk_builder_get_object(settings_window->gtkbuilder, item));
	gchar *val;
	const gchar *base_val, *base_row;
	gboolean matched=FALSE;

	/* update the layout combo box */
	model=gtk_combo_box_get_model(combo);
	if (gtk_tree_model_get_iter_first(model, &iter)) {
		val=settings_get_string(settings_get_settings_name(GTK_WIDGET(combo)));
		do {
			gtk_tree_model_get(model, &iter, 1, &data, -1);
			if (val && data && !strcmp(data, val)) {
				gtk_combo_box_set_active_iter(combo, &iter);
				ret=data;
				data=NULL;
				matched=TRUE;
				break;
			}
			g_free(data);
			data=NULL;
		} while (gtk_tree_model_iter_next(model, &iter));
		/*
		 * Theme path (FVWM --use-config) is not under DATADIR. Match by
		 * basename for the combo display, but return the real path so
		 * extensions_update still reads Navigation keys from that file.
		 * A failed search must NOT return the last readdir row (that
		 * was compact-alt here - no Navigation keys).
		 */
		if (!matched && val && val[0]) {
			base_val=strrchr(val, '/');
			base_val=base_val ? base_val+1 : val;
			if (gtk_tree_model_get_iter_first(model, &iter)) {
				do {
					gtk_tree_model_get(model, &iter, 1, &data, -1);
					base_row=data ? strrchr(data, '/') : NULL;
					base_row=base_row ? base_row+1 : data;
					if (base_row && !strcmp(base_row, base_val)) {
						gtk_combo_box_set_active_iter(combo, &iter);
						g_free(data);
						data=NULL;
						break;
					}
					g_free(data);
					data=NULL;
				} while (gtk_tree_model_iter_next(model, &iter));
			}
			ret=g_strdup(val);
		}
		if (val) g_free(val);
	}

	END_FUNC
	return ret;
}

/* update the input method options */
void settings_window_input_method_update(gchar *method)
{
	START_FUNC
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_timer")));
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"ramble_threshold1")));
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"ramble_threshold2")));
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"ramble_button")));
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"ramble_distance")));
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"ramble_timer")));
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"ramble_time")));
	if (!strcmp(method, "timer"))
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"flo_timer")));
#ifdef ENABLE_RAMBLE
	else if (!strcmp(method, "ramble")) {
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"ramble_threshold1")));
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"ramble_threshold2")));
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"ramble_button")));
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"ramble_distance")));
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"ramble_time")));
		if (gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(gtk_builder_get_object(settings_window->gtkbuilder,
			"ramble_distance")))) {
			gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_threshold1")));
			gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_threshold2")));
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_timer")));
		} else {
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_threshold1")));
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_threshold2")));
			gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_timer")));
		}
	}
#endif
	END_FUNC
}

/* update the window according to GSettings */
void settings_window_update()
{
	START_FUNC
	GObject *object;
	gchar *color;
	guint searchidx=0;
	const struct settings_param *params=settings_defaults_get();
#ifdef ENABLE_RAMBLE
	gchar *val;
#endif

	settings_window_updating=TRUE;
	while (params[searchidx].builder_name) {
		if (strcmp(params[searchidx].builder_name, SETTINGS_NONE))
			switch (params[searchidx].type) {
				case SETTINGS_BOOL:
					gtk_toggle_button_set_active(
						GTK_TOGGLE_BUTTON(gtk_builder_get_object(settings_window->gtkbuilder,
							params[searchidx].builder_name)),
						settings_get_bool(searchidx));
					break;
				case SETTINGS_COLOR:
					color=settings_get_string(searchidx);
					gtk_color_chooser_set_rgba(
						GTK_COLOR_CHOOSER(gtk_builder_get_object(settings_window->gtkbuilder,
							params[searchidx].builder_name)),
						settings_window_convert_color(color));
					if (color) g_free(color);
					break;
				case SETTINGS_STRING:
					object=gtk_builder_get_object(settings_window->gtkbuilder,
						params[searchidx].builder_name);
					if (GTK_IS_FONT_BUTTON(object)) {
						gchar *font=settings_get_string(searchidx);
						gtk_font_chooser_set_font(
							GTK_FONT_CHOOSER(object), font);
						if (font) g_free(font);
					}
					break;
				case SETTINGS_DOUBLE:
					gtk_range_set_value(
						GTK_RANGE(gtk_builder_get_object(settings_window->gtkbuilder,
							params[searchidx].builder_name)),
						settings_get_double(searchidx));
					break;
				default:flo_error(_("unknown setting type: %d"),
						params[searchidx].type);
					break;
			}
		searchidx++;
	}

#ifdef ENABLE_RAMBLE
	val=settings_get_string(SETTINGS_RAMBLE_ALGO);
	gtk_toggle_button_set_active(
		GTK_TOGGLE_BUTTON(gtk_builder_get_object(settings_window->gtkbuilder,
			"ramble_distance")),
		!strcmp("distance", val));
	gtk_toggle_button_set_active(
		GTK_TOGGLE_BUTTON(gtk_builder_get_object(settings_window->gtkbuilder,
			"ramble_time")),
		!strcmp("time", val));
	if (val) g_free(val);
#endif

	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_move_to_widget")), settings_get_bool(SETTINGS_AUTO_HIDE));
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_intermediate_icon")), settings_get_bool(SETTINGS_AUTO_HIDE));
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_font")), !settings_get_bool(SETTINGS_SYSTEM_FONT));
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_font_label")), !settings_get_bool(SETTINGS_SYSTEM_FONT));

	color=settings_window_combo_update("flo_layouts");
	if (color) {
		settings_window_extensions_update(color);
		g_free(color);
	}
	color=settings_window_combo_update("input_method_combo");
	if (color) {
		settings_window_input_method_update(color);
	       	g_free(color);
	}

	settings_window_updating=FALSE;
	END_FUNC
}

/*************/
/* callbacks */
/*************/

/* opens yelp when F1 is pressed  */
void settings_window_help(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	START_FUNC
#ifdef ENABLE_HELP
	GError *error=NULL;
	if (event->keyval==GDK_KEY_F1) {
		gtk_show_uri(NULL, "ghelp:florence?config", gtk_get_current_event_time(), &error);
		if (error) flo_error(_("Unable to open %s: %s"), "ghelp:florence?config",
			error->message);
	}
#endif
	END_FUNC
}

/* Called when a new style is selected */
void settings_window_style_change (GtkIconView *iconview, gpointer user_data) 
{
	START_FUNC
	gchar *path;
	gchar *name;
	GtkTreeIter iter;
	GList *list;
	if (settings_window_updating) { END_FUNC; return; }
	list=gtk_icon_view_get_selected_items(iconview);
	if (list) {
		gtk_tree_model_get_iter(gtk_icon_view_get_model(iconview), &iter, (GtkTreePath *)list->data);
		gtk_tree_model_get(gtk_icon_view_get_model(iconview), &iter, 1, &name, -1);
		path=g_strdup_printf(DATADIR "/styles/%s", name);
		settings_set_string(SETTINGS_STYLE_ITEM, path);
		g_list_foreach(list, (GFunc)(gtk_tree_path_free), NULL);
		g_list_free(list);
		g_free(path);
	}
	END_FUNC
}

/* on color change */
void settings_window_change_color(GtkColorChooser *button)
{
	START_FUNC
	GdkRGBA color;
	gchar strcolor[8];
	enum settings_item item;
	if (settings_window_updating) { END_FUNC; return; }
	item=settings_get_settings_name(GTK_WIDGET(button));
	gtk_color_chooser_get_rgba(button, &color);
	g_sprintf(strcolor, "#%02X%02X%02X", (int)((color.red)*256.),
		(int)((color.green)*256.), (int)((color.blue)*256.));
	settings_set_string(item, strcolor);
	/* update style preview */
	if (item==SETTINGS_KEY) settings_window_preview_build();
	END_FUNC
}

/* called on combo change: set gconf entry. */
void settings_window_combo(GtkComboBox *combo)
{
	START_FUNC
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *data=NULL;

	if (settings_window_updating) { END_FUNC; return; }
	gtk_combo_box_get_active_iter(combo, &iter);
	model=gtk_combo_box_get_model(combo);
	gtk_tree_model_get(model, &iter, 1, &data, -1);
	settings_set_string(settings_get_settings_name(GTK_WIDGET(combo)), data);
	if (!strcmp(gtk_buildable_get_name(GTK_BUILDABLE(combo)), "flo_layouts")) {
		settings_window_extensions_update(data);
	} else if (!strcmp(gtk_buildable_get_name(GTK_BUILDABLE(combo)), "input_method_combo")) {
		settings_window_input_method_update(data);
	}
	g_free(data);
	END_FUNC
}

/* called when an extension is activated/deactivated. */
void settings_window_extension(GtkToggleButton *button, gchar *name)
{
	START_FUNC
        gchar *allextstr=NULL;
        gchar **extstrs=NULL;
        gchar **extstr=NULL;
        gchar **newextstrs=NULL;
        gchar **newextstr=NULL;
	gchar *new_value=NULL;
	const gchar *alias;

	if (settings_window_updating) { END_FUNC; return; }
	allextstr=settings_get_string(SETTINGS_EXTENSIONS);
	if (allextstr) {
                extstrs=g_strsplit(allextstr, ":", -1);
                extstr=extstrs;
		newextstrs=g_malloc(sizeof(gchar *)*(2+g_strv_length(extstrs)));
               	newextstr=newextstrs;
		alias=settings_extension_id_alias(name);
		/* Drop this id and its layout alias so we never keep both. */
		while (extstr && *extstr) {
			if (!settings_extension_id_equal(*extstr, name) &&
			    !(alias && !strcmp(*extstr, alias)))
				*(newextstr++)=*extstr;
			extstr++;
		}
		if (gtk_toggle_button_get_active(button)) {
			*(newextstr++)=name;
		}
		*newextstr=NULL;
		new_value=g_strjoinv(":", newextstrs);
		settings_set_string(SETTINGS_EXTENSIONS, new_value);
                g_strfreev(extstrs);
                g_free(newextstrs);
		g_free(allextstr);
		g_free(new_value);
        }
	END_FUNC
}

/* Set a GSettings double according to the state of the scale bar.
 * Look for the gconf parameter name in the parameters table */
void settings_window_set_double(GtkHScale *scale)
{
	START_FUNC
	if (settings_window_updating) { END_FUNC; return; }
	settings_set_double(settings_get_settings_name(GTK_WIDGET(scale)),
		gtk_range_get_value(GTK_RANGE(scale)), TRUE);
	END_FUNC
}

/* on font change: apply immediately */
void settings_window_font(GtkFontButton *font)
{
	START_FUNC
	gchar *name;
	if (settings_window_updating) { END_FUNC; return; }
	name=gtk_font_chooser_get_font(GTK_FONT_CHOOSER(font));
	settings_set_string(SETTINGS_FONT, name);
	if (name) g_free(name);
	END_FUNC
}

/* set a gconf boolean according to the state of the toggle button.
 * Look for the GSettings parameter name in the parameters table */
void settings_window_set_bool (GtkToggleButton *button)
{
	START_FUNC
	if (settings_window_updating) { END_FUNC; return; }
#ifdef ENABLE_RAMBLE
	if ((!strcmp(gtk_buildable_get_name(GTK_BUILDABLE(button)), "ramble_distance")) ||
		(!strcmp(gtk_buildable_get_name(GTK_BUILDABLE(button)), "ramble_time"))) {
		if (!strcmp(gtk_buildable_get_name(GTK_BUILDABLE(button)), "ramble_distance") &&
			gtk_toggle_button_get_active(button)) {
			settings_set_string(SETTINGS_RAMBLE_ALGO, "distance");
			gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_threshold1")));
			gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_threshold2")));
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_timer")));
		} else {
			settings_set_string(SETTINGS_RAMBLE_ALGO, "time");
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_threshold1")));
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_threshold2")));
			gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
				"ramble_timer")));
		}
	} else {
#endif
	settings_set_bool(settings_get_settings_name(GTK_WIDGET(button)),
		gtk_toggle_button_get_active(button));
	if (!strcmp(gtk_buildable_get_name(GTK_BUILDABLE(button)), "flo_auto_hide")) {
		gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"flo_move_to_widget")), gtk_toggle_button_get_active(button));
		gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"flo_intermediate_icon")), gtk_toggle_button_get_active(button));
	} else if (!strcmp(gtk_buildable_get_name(GTK_BUILDABLE(button)), "flo_system_font")) {
		gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"flo_font_label")), !gtk_toggle_button_get_active(button));
		gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"flo_font")), !gtk_toggle_button_get_active(button));
	}
#ifdef ENABLE_RAMBLE
	}
#endif
	END_FUNC
}

/* apply changes. */
void settings_window_commit(GtkWidget *window, GtkWidget *button)
{
	START_FUNC
	settings_commit();
	END_FUNC
}

/* Revert unapplied changes (does not close the dialog). */
void settings_window_rollback(GtkWidget *window, GtkWidget *button)
{
	START_FUNC
	(void)button;
	settings_rollback();
	if (window) settings_window_preview_build();
	settings_window_update();
	END_FUNC
}

/* Apply / Discard / Cancel when closing with unapplied changes. */
static gint
settings_window_confirm_close(GtkWindow *parent)
{
	GtkWidget *dialog;
	gint ret;

	dialog = gtk_message_dialog_new(parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
		_("You have unapplied changes."));
	gtk_window_set_title(GTK_WINDOW(dialog), _("Confirm"));
	gtk_dialog_add_buttons(GTK_DIALOG(dialog),
		_("_Apply"), GTK_RESPONSE_ACCEPT,
		_("_Discard"), GTK_RESPONSE_REJECT,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
	ret = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	return ret;
}

/* Tear down prefs; safe to call from Close, WM delete, or destroy. */
void settings_window_close(GtkWidget *window, GtkWidget *button)
{
	START_FUNC
	static gboolean closed=FALSE;
	GtkWidget *win;
	gboolean do_exit;

	(void)button;
	if (closed || !settings_window) {
		END_FUNC
		return;
	}
	closed=TRUE;

	win = window;
	if ((!win || !GTK_IS_WIDGET(win)) && settings_window->gtkbuilder)
		win = GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
			"flo_config_window"));

	if (settings_dirty() && win && GTK_IS_WINDOW(win)) {
		gint resp = settings_window_confirm_close(GTK_WINDOW(win));
		if (resp == GTK_RESPONSE_CANCEL || resp == GTK_RESPONSE_DELETE_EVENT) {
			closed=FALSE;
			END_FUNC
			return;
		}
		if (resp == GTK_RESPONSE_ACCEPT)
			settings_window_commit(NULL, NULL);
		else
			settings_window_rollback(NULL, NULL);
	}
	if (settings_window->notify_id>0)
		settings_unregister(settings_window->notify_id);
	settings_window->notify_id=0;

	if (settings_window->style_list) {
		g_object_unref(G_OBJECT(settings_window->style_list));
		settings_window->style_list=NULL;
	}

	/* Only config-only mode (florence --settings) should quit the process. */
	do_exit = settings_window->gtk_exit;

	if (win && GTK_IS_WIDGET(win)) {
		/* Prevent glade destroy->close re-entry from double-freeing. */
		g_signal_handlers_disconnect_by_func(win,
			G_CALLBACK(settings_window_close), win);
		g_signal_handlers_disconnect_by_func(win,
			G_CALLBACK(settings_window_on_delete), NULL);
		gtk_widget_hide(win);
		gtk_widget_destroy(win);
	}

	settings_window_free();
	closed=FALSE;
	if (do_exit) exit(0);
	END_FUNC
}

/* WM titlebar close / Alt-F4 */
static gboolean
settings_window_on_delete(GtkWidget *window, GdkEvent *event, gpointer data)
{
	(void)event;
	(void)data;
	settings_window_close(window, NULL);
	return TRUE; /* we destroy in close */
}

/* Locate the visible OSK (not the float glyph, not prefs). */
static gboolean
settings_window_find_osk(gint *ox, gint *oy, gint *ow, gint *oh)
{
	Display *dpy;
	Window root;
	Atom net_list, type;
	int format;
	unsigned long nitems, bytes;
	unsigned char *data = NULL;
	Window *wins;
	unsigned long i;
	gboolean found = FALSE;

	dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
	root = DefaultRootWindow(dpy);
	net_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	if (XGetWindowProperty(dpy, root, net_list, 0, 1024, False, XA_WINDOW,
		&type, &format, &nitems, &bytes, &data) != Success || !data)
		return FALSE;
	wins = (Window *)data;
	for (i = 0; i < nitems; i++) {
		XClassHint hint;
		XWindowAttributes wa;
		Window child;
		int x, y;
		gboolean is_flo, is_prefs;

		memset(&hint, 0, sizeof(hint));
		if (!XGetClassHint(dpy, wins[i], &hint))
			continue;
		is_flo = (hint.res_class &&
			(!g_ascii_strcasecmp(hint.res_class, "Florence") ||
			 !g_ascii_strcasecmp(hint.res_class, "florence"))) ||
			(hint.res_name &&
			 (!g_ascii_strcasecmp(hint.res_name, "florence") ||
			  !g_ascii_strcasecmp(hint.res_name, "florence-sticky")));
		is_prefs = (hint.res_class && strstr(hint.res_class, "refs")) ||
			(hint.res_name && strstr(hint.res_name, "refs"));
		if (hint.res_name) XFree(hint.res_name);
		if (hint.res_class) XFree(hint.res_class);
		if (!is_flo || is_prefs)
			continue;
		if (!XGetWindowAttributes(dpy, wins[i], &wa) ||
		    wa.map_state != IsViewable)
			continue;
		/* Float glyph is ~64x44; keyboard is much larger. */
		if (wa.width < 200 || wa.height < 120)
			continue;
		XTranslateCoordinates(dpy, wins[i], root, 0, 0, &x, &y, &child);
		*ox = x; *oy = y; *ow = wa.width; *oh = wa.height;
		found = TRUE;
		break;
	}
	XFree(data);
	return found;
}

/*
 * Place prefs in free monitor space so the StaysOnTop keyboard does not
 * cover it. Prefer the band above the OSK (typical bottom dock layout).
 */
static void
settings_window_place_clear_of_keyboard(GtkWindow *prefs)
{
	GdkDisplay *disp;
	GdkMonitor *mon;
	GdkRectangle work;
	gint pw, ph, px, py;
	gint ox, oy, ow, oh;
	gint above, below, left, right;
	const gint gap = 24;

	gtk_window_set_position(prefs, GTK_WIN_POS_NONE);
	gtk_window_get_size(prefs, &pw, &ph);
	if (pw < 1) pw = 350;
	if (ph < 1) ph = 300;
	/* Title/border roughly; keep clear of the OSK either way. */
	ph += 40;

	disp = gtk_widget_get_display(GTK_WIDGET(prefs));
	if (!settings_window_find_osk(&ox, &oy, &ow, &oh)) {
		mon = gdk_display_get_primary_monitor(disp);
		if (!mon) mon = gdk_display_get_monitor(disp, 0);
		if (!mon) return;
		gdk_monitor_get_workarea(mon, &work);
		px = work.x + (work.width - pw) / 2;
		py = work.y + gap;
		gtk_window_move(prefs, px, py);
		gtk_window_present(prefs);
		return;
	}

	mon = gdk_display_get_monitor_at_point(disp, ox + ow / 2, oy + oh / 2);
	if (!mon) mon = gdk_display_get_primary_monitor(disp);
	if (!mon) return;
	gdk_monitor_get_workarea(mon, &work);

	above = oy - work.y;
	below = (work.y + work.height) - (oy + oh);
	left = ox - work.x;
	right = (work.x + work.width) - (ox + ow);

	if (above >= ph + gap) {
		px = work.x + (work.width - pw) / 2;
		py = oy - ph - gap;
		if (py < work.y + gap) py = work.y + gap;
	} else if (below >= ph + gap) {
		px = work.x + (work.width - pw) / 2;
		py = oy + oh + gap;
	} else if (left >= pw + gap) {
		px = ox - pw - gap;
		py = work.y + (work.height - ph) / 2;
	} else if (right >= pw + gap) {
		px = ox + ow + gap;
		py = work.y + (work.height - ph) / 2;
	} else {
		/* No full clear band: pin to top of workarea (least OSK overlap). */
		px = work.x + (work.width - pw) / 2;
		py = work.y + gap;
	}
	if (px < work.x) px = work.x + gap;
	if (py < work.y) py = work.y + gap;
	if (px + pw > work.x + work.width)
		px = work.x + work.width - pw - gap;
	if (py + ph > work.y + work.height)
		py = work.y + work.height - ph - gap;

	gtk_window_move(prefs, px, py);
	gtk_window_set_keep_above(prefs, TRUE);
	gtk_window_present(prefs);
}

/********************/
/* public functions */
/********************/

/* returns true if settings window is open */
gboolean settings_window_open(void)
{
	START_FUNC
	GtkWidget *cfg;

	if (!settings_window || settings_window->notify_id == 0) {
		END_FUNC
		return FALSE;
	}
	cfg = GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_config_window"));
	/* Stale after a WM destroy that bypassed our close cleanup. */
	if (!cfg || !GTK_IS_WINDOW(cfg)) {
		settings_window_free();
		END_FUNC
		return FALSE;
	}
	END_FUNC
	return TRUE;
}

/* presents the settings window to the user */
void settings_window_present(void)
{
	START_FUNC
	GtkWidget *cfg;

	if (!settings_window_open()) {
		END_FUNC
		return;
	}
	cfg = GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_config_window"));
	settings_window_place_clear_of_keyboard(GTK_WINDOW(cfg));
	END_FUNC
}

/* opens the settings window */
void settings_window_new(gboolean exit)
{
	START_FUNC
	GError* error = NULL;
	GtkWidget *cfg;
	GdkWindow *gdkw;
	XClassHint ch;

	if (settings_window)
		settings_window_free();
	settings_window=g_malloc(sizeof(struct settings_window));
	memset(settings_window, 0, sizeof(struct settings_window));

	settings_window->gtk_exit=exit;
	settings_window->gtkbuilder=gtk_builder_new();
	if (!gtk_builder_add_from_file(settings_window->gtkbuilder, DATADIR "/florence.glade", &error))
	{
		flo_warn(_("Couldn't load builder file: %s"), error->message);
		g_error_free(error);
	}

	cfg = GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_config_window"));
	/*
	 * Glade marks the window visible; hide immediately and set a distinct
	 * WM_CLASS before remap so FVWM OSK styles (NeverFocus/NoTitle) do not
	 * apply - otherwise prefs cannot be focused, closed, or dismissed.
	 */
	gtk_widget_hide(cfg);
	gtk_window_set_role(GTK_WINDOW(cfg), "preferences");
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	gtk_window_set_wmclass(GTK_WINDOW(cfg), "florence-prefs", "Florence-prefs");
	G_GNUC_END_IGNORE_DEPRECATIONS
	gtk_window_set_accept_focus(GTK_WINDOW(cfg), TRUE);

	/*
	 * Upstream stock button is gtk-cancel but only reverts - it does not
	 * dismiss. Relabel so it matches behaviour (Close dismisses).
	 */
	{
		GtkWidget *revert = GTK_WIDGET(gtk_builder_get_object(
			settings_window->gtkbuilder, "flo_config_rollback"));
		if (revert) {
			G_GNUC_BEGIN_IGNORE_DEPRECATIONS
			gtk_button_set_use_stock(GTK_BUTTON(revert), FALSE);
			G_GNUC_END_IGNORE_DEPRECATIONS
			gtk_button_set_label(GTK_BUTTON(revert), _("Revert"));
			gtk_widget_set_tooltip_text(revert,
				_("Revert unapplied changes"));
		}
	}

	/* populate fields*/
	settings_window_preview_build();
	settings_window_layouts_populate();
	settings_window_input_method_populate();

#ifndef AT_SPI
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_auto_hide")), FALSE);
	gtk_widget_set_tooltip_text(GTK_WIDGET(gtk_builder_get_object(settings_window->gtkbuilder,
		"flo_auto_hide")), _("AT-SPI has been disabled at compile time, this option is not available."));
#endif

	settings_window_update();
	settings_window->notify_id=settings_register_all(
		(settings_callback)settings_window_update);
	settings_transaction();

	/*
	 * gtk_builder_connect_signals needs the executable linked with
	 * -rdynamic / --export-dynamic. Also wire Close/delete ourselves so
	 * dismiss always works even if builder lookup fails.
	 */
	gtk_builder_connect_signals(settings_window->gtkbuilder, NULL);
	{
		GtkWidget *close_btn = GTK_WIDGET(gtk_builder_get_object(
			settings_window->gtkbuilder, "flo_close"));
		g_signal_connect(G_OBJECT(cfg), "delete-event",
			G_CALLBACK(settings_window_on_delete), NULL);
		g_signal_connect_swapped(G_OBJECT(cfg), "destroy",
			G_CALLBACK(settings_window_close), cfg);
		if (close_btn)
			g_signal_connect_swapped(G_OBJECT(close_btn), "clicked",
				G_CALLBACK(settings_window_close), cfg);
	}

	/* set window icon */
	tools_set_icon(GTK_WINDOW(cfg));

	gtk_widget_realize(cfg);
	gdkw = gtk_widget_get_window(cfg);
	if (gdkw && GDK_IS_X11_WINDOW(gdkw)) {
		ch.res_name = (char *)"florence-prefs";
		ch.res_class = (char *)"Florence-prefs";
		XSetClassHint(GDK_WINDOW_XDISPLAY(gdkw), GDK_WINDOW_XID(gdkw), &ch);
	}
	gtk_widget_show_all(cfg);
	gtk_window_resize(GTK_WINDOW(cfg), 350, 300);
	settings_window_place_clear_of_keyboard(GTK_WINDOW(cfg));
	END_FUNC
}

/* liberate memory used by settings window */
void settings_window_free()
{
	START_FUNC
	if (settings_window) {
		GSList *list=settings_window->extensions;
		while (list) {
			g_free(list->data);
			list=list->next;
		}
		if (settings_window->extensions) g_slist_free(settings_window->extensions);
		if (settings_window->gtkbuilder) g_object_unref(G_OBJECT(settings_window->gtkbuilder));
		g_free(settings_window);
		settings_window=NULL;
	}
	END_FUNC
}

