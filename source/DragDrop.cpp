/*
 * Copyright (c) 2009-2016, Albertas Vyšniauskas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *     * Neither the name of the software author nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "DragDrop.h"
#include "ColorObject.h"
#include "DynvHelpers.h"
#include "GlobalState.h"
#include "gtk/ColorWidget.h"
#include "Converter.h"
#include "dynv/DynvXml.h"
#include <string.h>
#include <iostream>
#include <sstream>

using namespace std;

enum {
	TARGET_STRING = 1,
	TARGET_ROOTWIN,
	TARGET_COLOR,
	TARGET_COLOR_OBJECT_LIST,
	TARGET_COLOR_OBJECT_LIST_SERIALIZED,
};

static GtkTargetEntry targets[] = {
	{ (char*)"color_object-list", GTK_TARGET_SAME_APP, TARGET_COLOR_OBJECT_LIST },
	{ (char*)"application/x-color_object-list", GTK_TARGET_OTHER_APP, TARGET_COLOR_OBJECT_LIST_SERIALIZED },
	{ (char*)"application/x-color", 0, TARGET_COLOR },
	{ (char*)"text/plain", 0, TARGET_STRING },
	{ (char*)"STRING", 0, TARGET_STRING },
	{ (char*)"application/x-rootwin-drop", 0, TARGET_ROOTWIN }
};

static guint n_targets = G_N_ELEMENTS (targets);

static void drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *selection_data, guint target_type, guint time, gpointer data);
static gboolean drag_motion(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint t, gpointer user_data);
static void drag_leave(GtkWidget *widget, GdkDragContext *context, guint time, gpointer user_data);
static gboolean drag_drop(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data);

static void drag_data_delete(GtkWidget *widget, GdkDragContext *context, gpointer user_data);
static void drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint target_type, guint time, gpointer user_data);
static void drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data);
static void drag_end(GtkWidget *widget, GdkDragContext *context, gpointer user_data);

static void drag_destroy(GtkWidget *widget, gpointer user_data);

int dragdrop_init(DragDrop* dd, GlobalState *gs){
	dd->get_color_object = 0;
	dd->set_color_object_at = 0;
	dd->test_at = 0;
	dd->data_received = 0;
	dd->data_get = 0;
	dd->data_delete = 0;
	dd->drag_end = 0;
	dd->get_color_object_list = 0;
	dd->set_color_object_list_at = 0;

	dd->handler_map = 0;
	dd->data_type = DragDrop::DATA_TYPE_NONE;
	memset(&dd->data, 0, sizeof(dd->data));
	dd->widget = 0;
	dd->gs = gs;
	dd->dragwidget = 0;

	return 0;
}

int dragdrop_widget_attach(GtkWidget* widget, DragDropFlags flags, DragDrop *user_dd){

	DragDrop* dd=new DragDrop;
	memcpy(dd, user_dd, sizeof(DragDrop));
	dd->widget = widget;

	if (flags & DRAGDROP_SOURCE){
		GtkTargetList *target_list = gtk_drag_source_get_target_list(widget);
		if (target_list){
			gtk_target_list_add_table(target_list, targets, n_targets);
		}else{
			target_list = gtk_target_list_new(targets, n_targets);
			gtk_drag_source_set_target_list(widget, target_list);
		}
		g_signal_connect (widget, "drag-data-get", G_CALLBACK (drag_data_get), dd);
		g_signal_connect (widget, "drag-data-delete", G_CALLBACK (drag_data_delete), dd);
		g_signal_connect (widget, "drag-begin", G_CALLBACK (drag_begin), dd);
		g_signal_connect (widget, "drag-end", G_CALLBACK (drag_end), dd);
	}

	if (flags & DRAGDROP_DESTINATION){
		GtkTargetList *target_list = gtk_drag_dest_get_target_list(widget);
		if (target_list){
			gtk_target_list_add_table(target_list, targets, n_targets);
		}else{
			target_list = gtk_target_list_new(targets, n_targets);
			gtk_drag_dest_set_target_list(widget, target_list);
		}
		g_signal_connect (widget, "drag-data-received", G_CALLBACK(drag_data_received), dd);
		g_signal_connect (widget, "drag-leave", G_CALLBACK (drag_leave), dd);
		g_signal_connect (widget, "drag-motion", G_CALLBACK (drag_motion), dd);
		g_signal_connect (widget, "drag-drop", G_CALLBACK (drag_drop), dd);
	}

	g_signal_connect (widget, "destroy", G_CALLBACK (drag_destroy), dd);

	return 0;
}

static void drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *selection_data, guint target_type, guint time, gpointer user_data){
	bool success = false;

	if ((selection_data != nullptr) && (gtk_selection_data_get_length(selection_data) >= 0)){
		DragDrop *dd = (DragDrop*)user_data;
		if (dd->data_received){
			success = dd->data_received(dd, widget, context, x, y, selection_data, target_type, time);
		}
		if (!success)
		switch (target_type){
		case TARGET_COLOR_OBJECT_LIST:
			{
				struct ColorObjectList{
					uint64_t color_object_n;
					ColorObject* color_object;
				}data;
				memcpy(&data, gtk_selection_data_get_data(selection_data), sizeof(data));
				if (data.color_object_n > 1){
					ColorObject **color_objects = new ColorObject*[data.color_object_n];
					memcpy(color_objects, gtk_selection_data_get_data(selection_data) + offsetof(ColorObjectList, color_object), sizeof(ColorObject*) * data.color_object_n);

					if (dd->set_color_object_list_at)
						dd->set_color_object_list_at(dd, color_objects, data.color_object_n, x, y, gdk_drag_context_get_actions(context) & GDK_ACTION_MOVE);
					else if (dd->set_color_object_at)
						dd->set_color_object_at(dd, data.color_object, x, y, gdk_drag_context_get_actions(context) & GDK_ACTION_MOVE);

				}else{
					if (dd->set_color_object_at)
						dd->set_color_object_at(dd, data.color_object, x, y, gdk_drag_context_get_actions(context) & GDK_ACTION_MOVE);
				}

			}
			success = true;
			break;

		case TARGET_COLOR_OBJECT_LIST_SERIALIZED:
			{
				size_t length = gtk_selection_data_get_length(selection_data);
				char *buffer = new char [length + 1];
				buffer[length] = 0;
				memcpy(buffer, gtk_selection_data_get_data(selection_data), length);
				stringstream str(buffer);
				delete [] buffer;
				dynvSystem *params = dynv_system_create(dd->handler_map);
				dynv_xml_deserialize(params, str);

				uint32_t color_n = 0;
				dynvSystem **colors = (dynvSystem**)dynv_get_dynv_array_wd(params, "colors", 0, 0, &color_n);
				if (color_n > 0 && colors){
					if (color_n > 1){
						if (dd->set_color_object_list_at){
							ColorObject **color_objects = new ColorObject*[color_n];
							for (uint32_t i = 0; i < color_n; i++){
								color_objects[i] = new ColorObject();
								color_objects[i]->setName(dynv_get_string_wd(colors[i], "name", ""));
								Color *color = dynv_get_color_wdc(colors[i], "color", nullptr);
								if (color != nullptr)
									color_objects[i]->setColor(*color);
							}
							dd->set_color_object_list_at(dd, color_objects, color_n, x, y, false);
							for (uint32_t i = 0; i < color_n; i++){
								color_objects[i]->release();
							}
							delete [] color_objects;
						}else if (dd->set_color_object_at){
							ColorObject* color_object = new ColorObject();
							color_object->setName(dynv_get_string_wd(colors[0], "name", ""));
							Color *color = dynv_get_color_wdc(colors[0], "color", nullptr);
							if (color != nullptr)
								color_object->setColor(*color);
							dd->set_color_object_at(dd, color_object, x, y, false);
							color_object->release();
						}
					}else{
						if (dd->set_color_object_at){
							ColorObject* color_object = new ColorObject();
							color_object->setName(dynv_get_string_wd(colors[0], "name", ""));
							Color *color = dynv_get_color_wdc(colors[0], "color", nullptr);
							if (color != nullptr)
								color_object->setColor(*color);
							dd->set_color_object_at(dd, color_object, x, y, false);
							color_object->release();
						}
					}
				}
				if (colors){
					for (uint32_t i = 0; i < color_n; i++){
						dynv_system_release(colors[i]);
					}
					delete [] colors;
				}
			}
			success = true;
			break;

		case TARGET_STRING:
			{
				gchar* data = (gchar*)gtk_selection_data_get_data(selection_data);
				if (data[gtk_selection_data_get_length(selection_data)] != 0) break; //not null terminated
				ColorObject* color_object = nullptr;
				if (!converter_get_color_object(data, dd->gs, &color_object)){
					gtk_drag_finish (context, false, false, time);
					return;
				}
				dd->set_color_object_at(dd, color_object, x, y, gdk_drag_context_get_actions(context) & GDK_ACTION_MOVE);
				color_object->release();
			}
			success = true;
			break;

		case TARGET_COLOR:
			{
				guint16* data = (guint16*)gtk_selection_data_get_data(selection_data);
				Color color;
				color.rgb.red = data[0] / (double)0xFFFF;
				color.rgb.green = data[1] / (double)0xFFFF;
				color.rgb.blue = data[2] / (double)0xFFFF;

				ColorObject* color_object = new ColorObject("", color);
				dd->set_color_object_at(dd, color_object, x, y, gdk_drag_context_get_actions(context) & GDK_ACTION_MOVE);
				color_object->release();
			}
			success = true;
			break;

		default:
			g_assert_not_reached ();
		}
	}
	gtk_drag_finish(context, success, gdk_drag_context_get_actions(context) == GDK_ACTION_MOVE, time);
}


static gboolean drag_motion(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data){

	DragDrop *dd = (DragDrop*)user_data;

	GdkDragAction suggested_action;
	bool suggested_action_set = true;

	bool dragging_moves = dynv_get_bool_wd(dd->gs->getSettings(), "gpick.main.dragging_moves", true);
	if (dragging_moves){
		if ((gdk_drag_context_get_actions(context) & GDK_ACTION_MOVE) == GDK_ACTION_MOVE)
			suggested_action = GDK_ACTION_MOVE;
		else if ((gdk_drag_context_get_actions(context) & GDK_ACTION_COPY) == GDK_ACTION_COPY)
			suggested_action = GDK_ACTION_COPY;
		else
			suggested_action_set = false;
	}else{
		if ((gdk_drag_context_get_actions(context) & GDK_ACTION_COPY) == GDK_ACTION_COPY)
			suggested_action = GDK_ACTION_COPY;
		else if ((gdk_drag_context_get_actions(context) & GDK_ACTION_MOVE) == GDK_ACTION_MOVE)
			suggested_action = GDK_ACTION_MOVE;
		else
			suggested_action_set = false;
	}

	if (!dd->test_at){
		GdkAtom target = gtk_drag_dest_find_target(widget, context, 0);
		if (target){
			gdk_drag_status(context, suggested_action_set ? suggested_action : gdk_drag_context_get_selected_action(context), time);
		}else{
			gdk_drag_status(context, suggested_action_set ? suggested_action : GdkDragAction(0), time);
		}
		return TRUE;
	}

	if (dd->test_at(dd, x, y)){
		GdkAtom target = gtk_drag_dest_find_target(widget, context, 0);
		if (target){
			gdk_drag_status(context, suggested_action_set ? suggested_action : gdk_drag_context_get_selected_action(context), time);
		}else{
			gdk_drag_status(context, suggested_action_set ? suggested_action : GdkDragAction(0), time);
		}
	}else{
		gdk_drag_status(context, suggested_action_set ? suggested_action : GdkDragAction(0), time);
	}
	return TRUE;
}


static void drag_leave(GtkWidget *widget, GdkDragContext *context, guint time, gpointer user_data){
}


static gboolean drag_drop(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data){
	DragDrop *dd = (DragDrop*)user_data;

	GdkAtom target = gtk_drag_dest_find_target(widget, context, 0);

	if (target != GDK_NONE){
		gtk_drag_get_data(widget, context, target, time);
		if (dd->drag_end)
			dd->drag_end(dd, widget, context);
		return TRUE;
	}
	if (dd->drag_end)
		dd->drag_end(dd, widget, context);
	return FALSE;
}




static void drag_data_delete(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
	DragDrop *dd = (DragDrop*)user_data;
	if (dd->data_delete){
		dd->data_delete(dd, widget, context);
	}
}

static void drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint target_type, guint time, gpointer user_data)
{
	g_assert (selection_data != nullptr);
	DragDrop *dd = (DragDrop*)user_data;
	bool success = false;
	if (dd->data_get){
		success = dd->data_get(dd, widget, context, selection_data, target_type, time);
	}
	if (!success){
		if (dd->data_type == DragDrop::DATA_TYPE_COLOR_OBJECT){
			ColorObject* color_object = dd->data.color_object.color_object;
			if (!color_object) return;
			Color color;
			switch (target_type){
			case TARGET_COLOR_OBJECT_LIST:
				{
					struct{
						uint64_t color_object_n;
						ColorObject* color_object;
					}data;
					data.color_object_n = 1;
					data.color_object = color_object;
					gtk_selection_data_set(selection_data, gdk_atom_intern("color_object", TRUE), 8, (guchar *)&data, sizeof(data));
				}
				break;

			case TARGET_COLOR_OBJECT_LIST_SERIALIZED:
				{
					dynvSystem *params = dynv_system_create(dd->handler_map);
					dynvSystem **colors = new dynvSystem*[1];
					colors[0] = dynv_system_create(dd->handler_map);
					dynv_set_string(colors[0], "name", color_object->getName().c_str());
					dynv_set_color(colors[0], "color", &color_object->getColor());
					dynv_set_dynv_array(params, "colors", (const dynvSystem**)colors, 1);
					delete [] colors;

					stringstream str;
					str << "<?xml version=\"1.0\" encoding='UTF-8'?><root>" << endl;
					dynv_xml_serialize(params, str);
					str << "</root>" << endl;
					string xml_data = str.str();

					gtk_selection_data_set(selection_data, gdk_atom_intern("application/x-color_object-list", TRUE), 8, (guchar *)xml_data.c_str(), xml_data.length());
				}
				break;

			case TARGET_STRING:
				{
					string text;
					if (converter_get_text(color_object, ConverterArrayType::copy, dd->gs, text)){
						gtk_selection_data_set_text(selection_data, text.c_str(), text.length() + 1);
					}
				}
				break;

			case TARGET_COLOR:
				{
					color = color_object->getColor();
					guint16 data_color[4];
					data_color[0] = int(color.rgb.red * 0xFFFF);
					data_color[1] = int(color.rgb.green * 0xFFFF);
					data_color[2] = int(color.rgb.blue * 0xFFFF);
					data_color[3] = 0xffff;
					gtk_selection_data_set (selection_data, gdk_atom_intern ("application/x-color", TRUE), 16, (guchar *)data_color, 8);
				}
				break;

			case TARGET_ROOTWIN:
				g_print ("Dropped on the root window!\n");
				break;

			default:
				g_assert_not_reached ();
			}
		}else if (dd->data_type == DragDrop::DATA_TYPE_COLOR_OBJECTS){
			ColorObject** color_objects = dd->data.color_objects.color_objects;
			uint32_t color_object_n = dd->data.color_objects.color_object_n;
			if (!color_objects) return;
			Color color;

			switch (target_type){
			case TARGET_COLOR_OBJECT_LIST:
				{
					struct ColorObjectList{
						uint64_t color_object_n;
						ColorObject* color_object[1];
					};
					uint32_t data_length = sizeof(uint64_t) + sizeof(ColorObject*) * color_object_n;
					ColorObjectList *data = (ColorObjectList*)new char[data_length];

					data->color_object_n = color_object_n;
					memcpy(&data->color_object[0], color_objects, sizeof(ColorObject*) * color_object_n);

					gtk_selection_data_set(selection_data, gdk_atom_intern("color_object", TRUE), 8, (guchar *)data, data_length);
					delete [] (char*)data;
				}
				break;

			case TARGET_COLOR_OBJECT_LIST_SERIALIZED:
				{
					dynvSystem *params = dynv_system_create(dd->handler_map);
					if (color_object_n > 0){
						dynvSystem **colors = new dynvSystem*[color_object_n];
						for (uint32_t i = 0; i < color_object_n; i++){
							colors[i] = dynv_system_create(dd->handler_map);
							dynv_set_string(colors[i], "name", color_objects[i]->getName().c_str());
							dynv_set_color(colors[i], "color", &color_objects[i]->getColor());
						}
						dynv_set_dynv_array(params, "colors", (const dynvSystem**)colors, color_object_n);
						delete [] colors;
					}
					stringstream str;
					str << "<?xml version=\"1.0\" encoding='UTF-8'?><root>" << endl;
					dynv_xml_serialize(params, str);
					str << "</root>" << endl;
					string xml_data = str.str();

					gtk_selection_data_set(selection_data, gdk_atom_intern("application/x-color_object-list", TRUE), 8, (guchar *)xml_data.c_str(), xml_data.length());
				}
				break;

			case TARGET_STRING:
				{
					stringstream ss;
					string text;
					for (uint32_t i = 0; i != color_object_n; i++){
						if (converter_get_text(color_objects[i], ConverterArrayType::copy, dd->gs, text)){
							ss << text << endl;
						}
					}
					text = ss.str();
					gtk_selection_data_set_text(selection_data, text.c_str(), text.length() + 1);
				}
				break;

			case TARGET_COLOR:
				{
					ColorObject *color_object = color_objects[0];
					color = color_object->getColor();
					guint16 data_color[4];
					data_color[0] = int(color.rgb.red * 0xFFFF);
					data_color[1] = int(color.rgb.green * 0xFFFF);
					data_color[2] = int(color.rgb.blue * 0xFFFF);
					data_color[3] = 0xffff;
					gtk_selection_data_set (selection_data, gdk_atom_intern ("application/x-color", TRUE), 16, (guchar *)data_color, 8);
				}
				break;

			case TARGET_ROOTWIN:
				g_print ("Dropped on the root window!\n");
				break;

			default:
				g_assert_not_reached ();
			}



		}

	}
}

static void drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data){
	DragDrop *dd = (DragDrop*)user_data;

	if (dd->get_color_object_list){
		size_t color_object_n;
		ColorObject** color_objects = dd->get_color_object_list(dd, &color_object_n);
		if (color_objects){
			dd->data_type = DragDrop::DATA_TYPE_COLOR_OBJECTS;
			dd->data.color_objects.color_objects = color_objects;
			dd->data.color_objects.color_object_n = color_object_n;

			GtkWidget* dragwindow = gtk_window_new(GTK_WINDOW_POPUP);
			GtkWidget* hbox = gtk_vbox_new(true, 0);
			gtk_container_add(GTK_CONTAINER(dragwindow), hbox);
			gtk_window_resize(GTK_WINDOW(dragwindow), 164, 24 * std::min(color_object_n, (size_t)5));

			for (size_t i = 0; i < std::min(color_object_n, (size_t)5); i++){
				GtkWidget* color_widget = gtk_color_new();
				string text;
				converter_get_text(color_objects[i], ConverterArrayType::display, dd->gs, text);
				Color color = color_objects[i]->getColor();
				gtk_color_set_color(GTK_COLOR(color_widget), &color, text.c_str());
				gtk_box_pack_start(GTK_BOX(hbox), color_widget, true, true, 0);
			}

			gtk_drag_set_icon_widget(context, dragwindow, 0, 0);
			gtk_widget_show_all(dragwindow);

			dd->dragwidget = dragwindow;
			return;
		}
	}

	if (dd->get_color_object){
		ColorObject* color_object = dd->get_color_object(dd);
		if (color_object){
			dd->data_type = DragDrop::DATA_TYPE_COLOR_OBJECT;
			dd->data.color_object.color_object = color_object;
			GtkWidget* dragwindow = gtk_window_new(GTK_WINDOW_POPUP);
			GtkWidget* colorwidget = gtk_color_new();
			gtk_container_add(GTK_CONTAINER(dragwindow), colorwidget);
			gtk_window_resize(GTK_WINDOW(dragwindow), 164, 24);
			string text;
			converter_get_text(color_object, ConverterArrayType::display, dd->gs, text);
			Color color = color_object->getColor();
			gtk_color_set_color(GTK_COLOR(colorwidget), &color, text.c_str());
			gtk_drag_set_icon_widget(context, dragwindow, 0, 0);
			gtk_widget_show_all(dragwindow);
			dd->dragwidget = dragwindow;
			return;
		}
	}
}

static void drag_end(GtkWidget *widget, GdkDragContext *context, gpointer user_data){
	DragDrop *dd = (DragDrop*)user_data;

	if (dd->data_type == DragDrop::DATA_TYPE_COLOR_OBJECT){
		if (dd->data.color_object.color_object){
			dd->data.color_object.color_object->release();
			memset(&dd->data, 0, sizeof(dd->data));
		}
		dd->data_type = DragDrop::DATA_TYPE_NONE;
	}
	if (dd->data_type == DragDrop::DATA_TYPE_COLOR_OBJECTS){
		if (dd->data.color_objects.color_objects){
			for (uint32_t i = 0; i < dd->data.color_objects.color_object_n; i++){
				dd->data.color_objects.color_objects[i]->release();
			}
			delete [] dd->data.color_objects.color_objects;
			memset(&dd->data, 0, sizeof(dd->data));
		}
		dd->data_type = DragDrop::DATA_TYPE_NONE;
	}

	if (dd->dragwidget){
		gtk_widget_destroy(dd->dragwidget);
		dd->dragwidget = 0;
	}

	if (dd->drag_end)
		dd->drag_end(dd, widget, context);

}

static void drag_destroy(GtkWidget *widget, gpointer user_data){
	DragDrop *dd = (DragDrop*)user_data;
	dynv_handler_map_release(dd->handler_map);
	delete dd;
}
