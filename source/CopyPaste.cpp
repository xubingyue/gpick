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

#include "CopyPaste.h"
#include "ColorObject.h"
#include "ColorList.h"
#include "GlobalState.h"
#include "Converter.h"
#include <gtk/gtk.h>
#include <string.h>
#include <string>
using namespace std;

enum {
	TARGET_STRING = 1,
	TARGET_ROOTWIN,
	TARGET_COLOR,
	TARGET_COLOR_OBJECT,
};

static GtkTargetEntry targets[] = {
	//{ (char*)"colorobject", GTK_TARGET_SAME_APP, TARGET_COLOR_OBJECT },
	{ (char*)"application/x-color", 0, TARGET_COLOR },
	{ (char*)"text/plain", 0, TARGET_STRING },
	{ (char*)"STRING", 0, TARGET_STRING },
	//{ (char*)"application/x-rootwin-drop", 0, TARGET_ROOTWIN }
};

static guint n_targets = G_N_ELEMENTS (targets);

typedef struct CopyPasteArgs{
	ColorObject* color_object;
	GlobalState* gs;
}CopyPasteArgs;

static void clipboard_get(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint target_type, CopyPasteArgs* args){
	g_assert(selection_data != nullptr);
	Color color;
	switch (target_type){
	case TARGET_COLOR_OBJECT:
		gtk_selection_data_set (selection_data, gdk_atom_intern ("colorobject", false), 8, (guchar *)&args->color_object, sizeof(ColorObject*));
		break;

	case TARGET_STRING:
		{
			string text;
			converter_get_text(args->color_object, ConverterArrayType::copy, args->gs, text);
			gtk_selection_data_set_text(selection_data, text.c_str(), text.length() + 1);
		}
		break;

	case TARGET_COLOR:
		{
			color = args->color_object->getColor();
			guint16 data_color[4];

			data_color[0] = int(color.rgb.red * 0xFFFF);
			data_color[1] = int(color.rgb.green * 0xFFFF);
			data_color[2] = int(color.rgb.blue * 0xFFFF);
			data_color[3] = 0xffff;

			gtk_selection_data_set (selection_data, gdk_atom_intern ("application/x-color", false), 16, (guchar *)data_color, 8);
		}
		break;

	case TARGET_ROOTWIN:
		g_print ("Dropped on the root window!\n");
		break;

	default:
		g_assert_not_reached ();
	}

}

static void clipboard_clear(GtkClipboard *clipboard, CopyPasteArgs* args){
	args->color_object->release();
	delete args;
}


int copypaste_set_color_object(ColorObject* color_object, GlobalState* gs){
	CopyPasteArgs* args = new CopyPasteArgs;
	args->color_object = color_object->reference();
	args->gs = gs;

	if (gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), targets, n_targets,
			(GtkClipboardGetFunc)clipboard_get, (GtkClipboardClearFunc)clipboard_clear, args)){
		return 0;
	}
	return -1;
}


int copypaste_get_color_object(ColorObject** out_color_object, GlobalState* gs){
	GdkAtom *avail_targets;
	gint avail_n_targets;

	GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	if (gtk_clipboard_wait_for_targets(clipboard, &avail_targets, &avail_n_targets)){

		for (uint32_t j = 0; j < n_targets; ++j){
			for (int32_t i = 0; i < avail_n_targets; ++i){
				gchar* atom_name = gdk_atom_name(avail_targets[i]);

				if (g_strcmp0(targets[j].target, atom_name) == 0){
					GtkSelectionData *selection_data = gtk_clipboard_wait_for_contents(clipboard, avail_targets[i]);
					bool success = false;
					if (selection_data){

						switch (targets[j].info){
						case TARGET_COLOR_OBJECT:
							{
								ColorObject* color_object;
								memcpy(&color_object, gtk_selection_data_get_data(selection_data), sizeof(ColorObject*));
								*out_color_object = color_object;
								success = true;
							}
							break;

						case TARGET_STRING:
							{
								gchar* data = (gchar*)gtk_selection_data_get_data(selection_data);
								if (data[gtk_selection_data_get_length(selection_data)] != 0) break; //not null terminated
								ColorObject* color_object;
								if (converter_get_color_object(data, gs, &color_object)){
									*out_color_object = color_object;
									success = true;
								}
							}
							break;

						case TARGET_COLOR:
							{
								guint16* data = (guint16*)gtk_selection_data_get_data(selection_data);
								Color color;
								color.rgb.red = data[0] / (double)0xFFFF;
								color.rgb.green = data[1] / (double)0xFFFF;
								color.rgb.blue = data[2] / (double)0xFFFF;

								ColorObject* color_object = color_list_new_color_object(gs->getColorList(), &color);
								*out_color_object = color_object;
								success = true;
							}

							break;

						default:
							g_assert_not_reached ();
						}

					}

					if (success){
						g_free(atom_name);
						g_free(avail_targets);
						return 0;
					}
				}

				g_free(atom_name);
			}
		}

		g_free(avail_targets);
	}
	return -1;
}

int copypaste_is_color_object_available(GlobalState* gs){
	GdkAtom *avail_targets;
	gint avail_n_targets;
	if (gtk_clipboard_wait_for_targets(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), &avail_targets, &avail_n_targets)){

		for (uint32_t j = 0; j < n_targets; ++j){
			for (int32_t i = 0; i < avail_n_targets; ++i){
				gchar* atom_name = gdk_atom_name(avail_targets[i]);

				if (g_strcmp0(targets[j].target, atom_name) == 0){

					g_free(atom_name);
					g_free(avail_targets);
					return 0;
				}

				g_free(atom_name);
			}
		}

		g_free(avail_targets);
	}
	return -1;
}
