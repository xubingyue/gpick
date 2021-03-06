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

#include "ColorWidget.h"
#include "../Color.h"
#include "../MathUtil.h"
#include <math.h>
#include <boost/math/special_functions/round.hpp>
#include <iostream>
using namespace std;

#define GTK_COLOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), GTK_TYPE_COLOR, GtkColorPrivate))

G_DEFINE_TYPE(GtkColor, gtk_color, GTK_TYPE_DRAWING_AREA);

static gboolean gtk_color_expose(GtkWidget *widget, GdkEventExpose *event);
static gboolean gtk_color_button_release(GtkWidget *widget, GdkEventButton *event);
static gboolean gtk_color_button_press(GtkWidget *widget, GdkEventButton *event);
static void gtk_color_size_request (GtkWidget *widget, GtkRequisition *requisition);

static void gtk_color_finalize(GObject *color_obj);
static GtkWindowClass *parent_class = nullptr;
enum {
	ACTIVATED, LAST_SIGNAL,
};
static guint gtk_color_signals[LAST_SIGNAL] = { };
typedef struct GtkColorPrivate GtkColorPrivate;
typedef struct GtkColorPrivate {
	Color color;
	Color text_color;
	gchar* text;
	bool rounded_rectangle;
	bool h_center;
	bool secondary_color;
	double roundness;
	transformation::Chain *transformation_chain;
}GtkColorPrivate;

static void gtk_color_class_init(GtkColorClass *color_class)
{
	GObjectClass *obj_class;
	GtkWidgetClass *widget_class;
	parent_class = (GtkWindowClass*)g_type_class_peek_parent(G_OBJECT_CLASS(color_class));
	obj_class = G_OBJECT_CLASS(color_class);
	widget_class = GTK_WIDGET_CLASS(color_class);
	widget_class->expose_event = gtk_color_expose;
	widget_class->button_release_event = gtk_color_button_release;
	widget_class->button_press_event = gtk_color_button_press;
	widget_class->size_request = gtk_color_size_request;
	obj_class->finalize = gtk_color_finalize;
	g_type_class_add_private(obj_class, sizeof(GtkColorPrivate));
	gtk_color_signals[ACTIVATED] = g_signal_new("activated", G_OBJECT_CLASS_TYPE(obj_class), G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(GtkColorClass, activated), nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void gtk_color_init(GtkColor *color) {
	gtk_widget_add_events (GTK_WIDGET (color), GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK | GDK_2BUTTON_PRESS | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
}

GtkWidget* gtk_color_new(void) {
	GtkWidget* widget = (GtkWidget*) g_object_new(GTK_TYPE_COLOR, nullptr);
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);

	//gtk_widget_set_size_request(GTK_WIDGET(widget), 16+widget->style->xthickness*2, 16+widget->style->ythickness*2);

	color_set(&ns->color, 0);
	ns->text = 0;
	ns->rounded_rectangle = false;
	ns->h_center = false;
	ns->secondary_color = false;
	ns->roundness = 20;
	ns->transformation_chain = 0;

	GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS);

	return widget;
}

static void gtk_color_size_request (GtkWidget *widget, GtkRequisition *requisition){
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);

	gint width = 32 + widget->style->xthickness * 2;
	gint height = 16 + widget->style->ythickness * 2;

	if (ns->rounded_rectangle){
		width += ns->roundness;
		height += ns->roundness;
	}

	requisition->width = width;
	requisition->height = height;
}

static void gtk_color_finalize(GObject *color_obj){

	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(color_obj);
	if (ns->text){
		g_free(ns->text);
		ns->text = 0;
	}

	G_OBJECT_CLASS(parent_class)->finalize (color_obj);
}

void gtk_color_get_color(GtkColor* widget, Color* color){
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);
	color_copy(&ns->color, color);
}

void gtk_color_set_text_color(GtkColor* widget, Color* color) {
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);
	color_copy(color, &ns->text_color);
	gtk_widget_queue_draw(GTK_WIDGET(widget));
}

void gtk_color_set_roundness(GtkColor* widget, double roundness){
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);
	ns->roundness = roundness;

	gint width = 32 + GTK_WIDGET(widget)->style->xthickness * 2;
	gint height = 16 + GTK_WIDGET(widget)->style->ythickness * 2;

	if (ns->rounded_rectangle){
		width += ns->roundness;
		height += ns->roundness;
	}

	gtk_widget_set_size_request(GTK_WIDGET(widget), width, height);
	gtk_widget_queue_draw(GTK_WIDGET(widget));
}

void gtk_color_set_color(GtkColor* widget, Color* color, const char* text) {
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);
	color_copy(color, &ns->color);
	if (ns->secondary_color){

	}else{
		if (ns->transformation_chain){
			Color c;
			ns->transformation_chain->apply(&ns->color, &c);
			color_get_contrasting(&c, &ns->text_color);
		}else{
			color_get_contrasting(&ns->color, &ns->text_color);
		}
	}

	if (ns->text)
		g_free(ns->text);
	ns->text = 0;
	if (text)
		ns->text = g_strdup(text);
	gtk_widget_queue_draw(GTK_WIDGET(widget));
}

void gtk_color_set_rounded(GtkColor* widget, bool rounded_rectangle){
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);
	ns->rounded_rectangle = rounded_rectangle;

	//if (ns->rounded_rectangle)
	//	gtk_widget_set_size_request(GTK_WIDGET(widget), 32+GTK_WIDGET(widget)->style->xthickness*2, 48+GTK_WIDGET(widget)->style->ythickness*2);

	gtk_widget_queue_draw(GTK_WIDGET(widget));
}

void gtk_color_set_hcenter(GtkColor* widget, bool hcenter){
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);
	ns->h_center = hcenter;
	gtk_widget_queue_draw(GTK_WIDGET(widget));
}


static void cairo_rounded_rectangle(cairo_t *cr, double x, double y, double width, double height, double roundness){

	double strength = 0.3;

	cairo_move_to(cr, x+roundness, y);
	cairo_line_to(cr, x+width-roundness, y);
	cairo_curve_to(cr, x+width-roundness*strength, y, x+width, y+roundness*strength, x+width, y+roundness);
	cairo_line_to(cr, x+width, y+height-roundness);
	cairo_curve_to(cr, x+width, y+height-roundness*strength, x+width-roundness*strength, y+height, x+width-roundness, y+height);
	cairo_line_to(cr, x+roundness, y+height);
	cairo_curve_to(cr, x+roundness*strength, y+height, x, y+height-roundness*strength, x, y+height-roundness);
	cairo_line_to(cr, x, y+roundness);
	cairo_curve_to(cr, x, y+roundness*strength, x+roundness*strength, y, x+roundness, y);
	cairo_close_path (cr);

}

static gboolean gtk_color_expose(GtkWidget *widget, GdkEventExpose *event) {

	GtkStateType state;

	if (GTK_WIDGET_HAS_FOCUS (widget))
		state = GTK_STATE_SELECTED;
	else
		state = GTK_STATE_ACTIVE;


	cairo_t *cr;

	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);

	cr = gdk_cairo_create(widget->window);

	cairo_rectangle(cr, event->area.x, event->area.y, event->area.width, event->area.height);
	cairo_clip(cr);
	Color color;

	if (ns->rounded_rectangle){

		cairo_rounded_rectangle(cr, widget->style->xthickness, widget->style->ythickness, widget->allocation.width-widget->style->xthickness*2, widget->allocation.height-widget->style->ythickness*2, ns->roundness);

		if (ns->transformation_chain){
			ns->transformation_chain->apply(&ns->color, &color);
		}else{
			color_copy(&ns->color, &color);
		}

		cairo_set_source_rgb(cr, boost::math::round(color.rgb.red * 255.0) / 255.0, boost::math::round(color.rgb.green * 255.0) / 255.0, boost::math::round(color.rgb.blue * 255.0) / 255.0);
		cairo_fill_preserve(cr);

		if (GTK_WIDGET_HAS_FOCUS(widget)){
			cairo_set_source_rgb(cr, widget->style->fg[GTK_STATE_NORMAL].red/65536.0, widget->style->fg[GTK_STATE_NORMAL].green/65536.0, widget->style->fg[GTK_STATE_NORMAL].blue/65536.0);
			cairo_set_line_width(cr, 3);
		}else{
			cairo_set_source_rgb(cr, 0, 0, 0);
			cairo_set_line_width(cr, 1);
		}

		cairo_stroke(cr);

	}else{
		cairo_rectangle(cr, event->area.x, event->area.y, event->area.width, event->area.height);

		if (ns->transformation_chain){
			ns->transformation_chain->apply(&ns->color, &color);
		}else{
			color_copy(&ns->color, &color);
		}
		cairo_set_source_rgb(cr, color.rgb.red, color.rgb.green, color.rgb.blue);
		cairo_fill(cr);
	}

	if (ns->text){

		PangoLayout *layout;
		PangoFontDescription *font_description;
		font_description = pango_font_description_new();
		layout = pango_cairo_create_layout(cr);

		pango_font_description_set_family(font_description, "monospace");
		pango_font_description_set_weight(font_description, PANGO_WEIGHT_NORMAL);
		pango_font_description_set_absolute_size(font_description, 14 * PANGO_SCALE);
		pango_layout_set_font_description(layout, font_description);
		pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);

		if (ns->transformation_chain){
			if (ns->secondary_color){
				ns->transformation_chain->apply(&ns->text_color, &color);
			}else{
				color_copy(&ns->text_color, &color);
			}
		}else{
			color_copy(&ns->text_color, &color);
		}
		cairo_set_source_rgb(cr, color.rgb.red, color.rgb.green, color.rgb.blue);

		pango_layout_set_markup(layout, ns->text, -1);
		pango_layout_set_width(layout, (widget->allocation.width - widget->style->xthickness * 2) * PANGO_SCALE);
		pango_layout_set_height(layout, (widget->allocation.height - widget->style->ythickness * 2) * PANGO_SCALE);

		int width, height;
		pango_layout_get_pixel_size(layout, &width, &height);
		cairo_move_to(cr, widget->style->xthickness, widget->style->ythickness + (widget->allocation.height - height) / 2);

		if (ns->h_center)
			pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		else
			pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

		pango_cairo_update_layout(cr, layout);
		pango_cairo_show_layout(cr, layout);


		g_object_unref (layout);
		pango_font_description_free (font_description);

	}

	cairo_destroy(cr);


	return FALSE;
}


static gboolean gtk_color_button_release(GtkWidget *widget, GdkEventButton *event){
	gtk_widget_grab_focus(widget);
	return FALSE;
}

static gboolean gtk_color_button_press(GtkWidget *widget, GdkEventButton *event){
	gtk_widget_grab_focus(widget);

	if ((event->type == GDK_2BUTTON_PRESS) && (event->button == 1)) {
		g_signal_emit(widget, gtk_color_signals[ACTIVATED], 0);
	}

	return FALSE;
}

void gtk_color_set_transformation_chain(GtkColor* widget, transformation::Chain *chain){
	GtkColorPrivate *ns = GTK_COLOR_GET_PRIVATE(widget);
	ns->transformation_chain = chain;
	if (!ns->secondary_color){
		if (ns->transformation_chain){
			Color c;
			ns->transformation_chain->apply(&ns->color, &c);
			color_get_contrasting(&c, &ns->text_color);
		}else{
			color_get_contrasting(&ns->color, &ns->text_color);
		}
	}
	gtk_widget_queue_draw(GTK_WIDGET(widget));
}
