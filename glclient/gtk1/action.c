/*
 * PANDA -- a simple transaction monitor
 * Copyright (C) 1998-1999 Ogochan.
 * Copyright (C) 2000-2008 Ogochan & JMA (Japan Medical Association).
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
#define	DEBUG
#define	TRACE
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include    <sys/types.h>
#include	<sys/stat.h>
#include    <unistd.h>
#include	<sys/time.h>
#include 	<gnome.h>
#include	<gtkpanda/gtkpanda.h>

#include	"types.h"
#include	"glclient.h"
#include	"glterm.h"
#include	"message.h"
#include	"debug.h"
#include	"marshaller.h"
#define		_ACTION_C
#include	"action.h"
#include	"queue.h"

static struct changed_hander {
	GtkObject       *object;
	GtkSignalFunc	func;
	gpointer	data;
	gint		block_flag;
	struct changed_hander *next;
} *changed_hander_list = NULL;

static Bool TimerFlag = FALSE;

extern	void
RegisterChangedHandler(
	GtkObject *object,
	GtkSignalFunc func,
	gpointer data)
{
  struct changed_hander *p = xmalloc (sizeof (struct changed_hander));
ENTER_FUNC;
	p->object = object;
	p->func = func;
	p->data = data;
	p->next = changed_hander_list;
	p->block_flag = FALSE;
	changed_hander_list = p;
LEAVE_FUNC;
}

extern void
BlockChangedHandlers(void)
{
  struct changed_hander *p;

ENTER_FUNC;
	for (p = changed_hander_list; p != NULL; p = p->next) {
		p->block_flag = TRUE;		 
		gtk_signal_handler_block_by_func (p->object, p->func, p->data);
	}
LEAVE_FUNC;
}

extern void
UnblockChangedHandlers(void)
{
  struct changed_hander *p;
ENTER_FUNC;
	for (p = changed_hander_list; p != NULL; p = p->next) {
		if (p->block_flag) {
			gtk_signal_handler_unblock_by_func (p->object, p->func, p->data);
		}
	}
LEAVE_FUNC;
}

extern	GtkWidget	*
GetWindow(
	GtkWidget	*widget)
{
	GtkWidget	*window;
ENTER_FUNC;
	window = gtk_widget_get_toplevel(widget);
LEAVE_FUNC;
	return (window);
}

extern	char	*
GetWindowName(
	GtkWidget	*widget)
{
	GtkWidget	*window;
	static char	wname[SIZE_LONGNAME];

ENTER_FUNC;
	window = GetWindow(widget);
#if	1	/*	This logic is escape code for GTK bug.	*/
	strcpy(wname,glade_get_widget_long_name(widget));
	*(strchr(wname,'.')) = 0;
#else
	strcpy(wname,gtk_widget_get_name(window));
#endif
LEAVE_FUNC;
	return (wname);
}

static	gint
_GrabFocus(gpointer data)
{
	gtk_widget_grab_focus(data);
	return FALSE;
}

extern	void
GrabFocus(GtkWidget *widget)
{
	static guint tag = 0;

	gtk_widget_grab_focus(widget);
	if (tag != 0) {
		gtk_idle_remove (tag);	
	}
	/* Do not erase.  The interval is necessary in GtkCombo. */
	tag = gtk_idle_add(_GrabFocus, widget);
}

extern  void
ResetScrolledWindow(
    GtkWidget   *widget,
    gpointer    user_data)
{
    GtkAdjustment   *adj;

    if  (   GTK_IS_SCROLLED_WINDOW(widget)  ) {
		adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(widget));
		if	(	adj	) {
			adj->value = 0.0;
			gtk_adjustment_value_changed(adj);
		}
		adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(widget));
		if	(	adj	) {
			adj->value = 0.0;
			gtk_adjustment_value_changed(adj);
		}
    }
    if  (   GTK_IS_CONTAINER(widget)    ) {
        gtk_container_forall(GTK_CONTAINER(widget), ResetScrolledWindow, NULL);
    }
}

static	void
_ResetTimer(
	    GtkWidget	*widget,
	    gpointer	data)
{
	if (GTK_IS_CONTAINER (widget))
		gtk_container_forall (GTK_CONTAINER (widget), _ResetTimer, NULL);
	else if (GTK_IS_PANDA_TIMER (widget))
		gtk_panda_timer_reset (GTK_PANDA_TIMER (widget));
}

extern	void
_AddChangedWidget(
	GtkWidget	*widget)
{
	const char	*name;
	char		*wname;
	char		*key;
	GtkWidget	*window;
	WindowData	*wdata;

ENTER_FUNC;
	window = gtk_widget_get_toplevel(widget);
	if		(  TimerFlag  )	{
		ResetTimer(GTK_WINDOW (window));
	}
	name = glade_get_widget_long_name(widget);
	wname = gtk_widget_get_name(window);
	if ((wdata = g_hash_table_lookup(WindowTable,wname)) != NULL) {
		if (g_hash_table_lookup(wdata->ChangedWidgetTable, name) == NULL) {
			key = strdup(name);
			g_hash_table_insert(wdata->ChangedWidgetTable, key, key);
		}
	}
LEAVE_FUNC;
}

extern	void
AddChangedWidget(
	GtkWidget	*widget)
{
	if		(  !fInRecv  ) {
		_AddChangedWidget(widget);
	}
}

extern	void
ClearKeyBuffer(void)
{
	GdkEvent	*event; 
ENTER_FUNC;
	while( (event = gdk_event_get()) != NULL) {
		if ( (event->type == GDK_KEY_PRESS ||
			  event->type == GDK_KEY_RELEASE) ) {
 			gdk_event_free(event); 
		} else {
			gtk_main_do_event(event);
		}
	}
LEAVE_FUNC;
}

extern	void
StartTimer(
	char		*event,
	int			timeout,
	GtkFunction function,
	GtkWidget	*widget)
{
	GtkWidget	*window;
	static gint timeout_handler_id;
ENTER_FUNC;
	window = gtk_widget_get_toplevel(widget);
	gtk_object_set_data(GTK_OBJECT(window), "timeout_event", event);
	timeout_handler_id = gtk_timeout_add (timeout, function, widget);
	gtk_object_set_data(GTK_OBJECT(window), "timeout_handler_id", &timeout_handler_id);
	TimerFlag = TRUE;
LEAVE_FUNC;
}

extern	char	*
GetTimerEvent(
	GtkWindow	*window)
{
	static char *timeout_event;
ENTER_FUNC;
	timeout_event = (char *)gtk_object_get_data(GTK_OBJECT(window), "timeout_event");
LEAVE_FUNC;	
	return (timeout_event);
}

extern	void
ResetTimer(
	GtkWindow	*window)
{
	gtk_container_forall (GTK_CONTAINER (window), _ResetTimer, NULL);
}

extern void
StopTimer(
		GtkWindow	*window)
{
	gint *timeout_handler_id;
ENTER_FUNC;	
	if (TimerFlag) {
		timeout_handler_id = gtk_object_get_data(GTK_OBJECT(window), 
												 "timeout_handler_id");
		if ((timeout_handler_id) && (*timeout_handler_id != 0)) {
			gtk_timeout_remove(*timeout_handler_id);
			*timeout_handler_id = 0;
		}
		gtk_object_set_data(GTK_OBJECT(window), "timeout_handler_id", 
							timeout_handler_id);
		TimerFlag = FALSE;
	}
LEAVE_FUNC;
}

static	WindowData	*
CreateWindowData(
	char	*wname)
{
	char		*fname;
	GladeXML	*xml;
	WindowData	*wdata;
	GtkWidget	*window;

ENTER_FUNC;
	fname = CacheFileName(wname);
	xml = glade_xml_new(fname, NULL);
	if ( xml == NULL ) {
		wdata = NULL;
	} else {
		window = glade_xml_get_widget_by_long_name(xml, wname);
		if (window == NULL) {
			Warning("Window %s not found in %s", wname, fname);
			return NULL;
		}
		wdata = New(WindowData);
		wdata->xml = xml;
		wdata->name = StrDup(wname);
		wdata->title = StrDup(GTK_WINDOW (window)->title);
		wdata->ChangedWidgetTable = NewNameHash();
		wdata->UpdateWidgetQueue = NewQueue();
		glade_xml_signal_autoconnect(xml);
		g_hash_table_insert(WindowTable,strdup(wname),wdata);
	}
LEAVE_FUNC;
	return (wdata);
}

extern void
SetTitle(GtkWidget	*window,
	char *window_title)
{
	char		buff[SIZE_BUFF];

	if ( glSession->title != NULL && strlen(glSession->title) > 0 ) {
		snprintf(buff, sizeof(buff), "%s - %s", window_title, glSession->title);
	} else {
		snprintf(buff, sizeof(buff), "%s", window_title);
	}
	gtk_window_set_title (GTK_WINDOW(window), buff);
}

extern	void
ShowWindow(
	char	*wname,
	byte	type)
{
	WindowData	*data;
	GtkWidget	*widget;
	GtkWidget	*window;

ENTER_FUNC;
	widget = NULL;
	dbgprintf("ShowWindow [%s][%d]",wname,type);
	if ((data = g_hash_table_lookup(WindowTable,wname)) == NULL) {
		if ((type == SCREEN_NEW_WINDOW) ||
			(type == SCREEN_CURRENT_WINDOW)) {
			data = CreateWindowData(wname);
		}
	}
	if (data == NULL) {
		// FIXME sometimes comes here.
		return;
	}
	window = glade_xml_get_widget_by_long_name((GladeXML *)data->xml, wname);
	g_return_if_fail(window != NULL);
	switch	(type) {
	  case	SCREEN_NEW_WINDOW:
	  case	SCREEN_CURRENT_WINDOW:
        SetTitle(window, data->title);
		gtk_widget_set_sensitive (window, TRUE);
		gtk_widget_show_all(window);
		break;
	  case	SCREEN_CLOSE_WINDOW:
		StopTimer(GTK_WINDOW(window));
		if (GTK_WINDOW(window)->focus_widget != NULL ){
			widget = GTK_WIDGET(GTK_WINDOW(window)->focus_widget);
		}
		gtk_widget_set_sensitive (window, FALSE);
		if ((widget != NULL) && GTK_IS_BUTTON (widget)){
			gtk_button_released (GTK_BUTTON(widget));
		}
		gtk_widget_hide_all(window);
		/* fall through */
	  default:
		break;
	}
LEAVE_FUNC;
}

static	GdkCursor *Busycursor;

extern	GdkWindow	*
ShowBusyCursor(
	GtkWidget	*widget)
{
	static GdkWindow	*pane;
	GtkWidget	*window;
	GdkWindowAttr	attr;
ENTER_FUNC;
	memset (&attr, 0, sizeof (GdkWindowAttr));
	attr.wclass = GDK_INPUT_ONLY;
	attr.window_type = GDK_WINDOW_CHILD;
	Busycursor = gdk_cursor_new (GDK_WATCH);
	attr.cursor = Busycursor;
	attr.x = attr.y = 0;
	attr.width = attr.height = 32767;

	if		(  widget  !=  NULL  ) {
		window = gtk_widget_get_toplevel(widget);
	} else {
		window = NULL;
	}
	pane = gdk_window_new(window->window, &attr, GDK_WA_CURSOR);
	gdk_window_show (pane);
	gdk_flush ();
LEAVE_FUNC;
	return	(pane); 
}

extern	void
HideBusyCursor(GdkWindow *pane)
{
ENTER_FUNC;
	gdk_cursor_destroy (Busycursor);
	gdk_window_destroy (pane);
LEAVE_FUNC;
}

extern	gpointer	*
GetObjectData(GtkWidget	*widget,
			  char *object_key)
{
	return gtk_object_get_data(GTK_OBJECT(widget), object_key);
}

extern	void
SetObjectData(GtkWidget	*widget,
			  char *object_key,
			  gpointer	*data)
{
	gpointer	*object;	
	object = gtk_object_get_data(GTK_OBJECT(widget), object_key);
	if ( object == NULL ) {
		object = xmalloc(sizeof(data));
	}
	memcpy(object, data, sizeof(data));
	gtk_object_set_data(GTK_OBJECT(widget), object_key, object);
}


extern	GtkWidget*
GetWidgetByLongName(char *name)
{
	WidgetData	*data;
	GtkWidget	*widget;
	
	widget = NULL;
	data = g_hash_table_lookup(WidgetDataTable, name);
	if (data != NULL) {
	    widget = glade_xml_get_widget_by_long_name(
			(GladeXML *)data->window->xml, name);
	}
	return widget;
}

extern	GtkWidget*
GetWidgetByName(char *name)
{
	WindowData	*wdata;
	GtkWidget	*widget;
	
	widget = NULL;
	wdata = g_hash_table_lookup(WindowTable,ThisWindowName);
	if (wdata != NULL && wdata->xml != NULL) {
	    widget = glade_xml_get_widget((GladeXML *)wdata->xml, name);
	}
	return widget;
}

extern	GtkWidget*
GetWidgetByWindowNameAndLongName(char *windowName,
	char *widgetName)
{
	WindowData	*wdata;
	GtkWidget	*widget;
	
	widget = NULL;
	wdata = g_hash_table_lookup(WindowTable,windowName);
	if (wdata != NULL && wdata->xml != NULL) {
	    widget = glade_xml_get_widget_by_long_name(
			(GladeXML *)wdata->xml, widgetName);
	}
	return widget;
}

extern	GtkWidget*
GetWidgetByWindowNameAndName(char *windowName,
	char *widgetName)
{
	WindowData	*wdata;
	GtkWidget	*widget;
	
	widget = NULL;
	wdata = g_hash_table_lookup(WindowTable,windowName);
	if (wdata != NULL && wdata->xml != NULL) {
	    widget = glade_xml_get_widget(
			(GladeXML *)wdata->xml, widgetName);
	}
	return widget;
}