/*
 * Virt Viewer: A virtual machine console viewer
 *
 * Copyright (C) 2007-2012 Red Hat, Inc.
 * Copyright (C) 2009-2012 Daniel P. Berrange
 * Copyright (C) 2010 Marc-André Lureau
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include <config.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <glib/gprintf.h>
#include <glib/gi18n.h>


#include "virt-gtk-compat.h"
#include "virt-viewer-window.h"
#include "virt-viewer-session.h"
#include "virt-viewer-app.h"
#include "virt-viewer-util.h"
#include "view/autoDrawer.h"

//#include <libusb-1.0/libusb.h>
//#include <spice-client.h>
//#include <usbredirfilter.h>




#ifdef HAVE_SPICE_GTK
#include "virt-viewer-session-spice.h"
#include <spice-client-gtk.h>
#endif

#if defined(G_OS_WIN32)
#include <windows.h>
#define PIPE_NAME TEXT("\\\\.\\pipe\\SpiceController-500")
static HANDLE pipe = INVALID_HANDLE_VALUE;
#include <io.h>
#endif


/* Signal handlers for main window (move in a VirtViewerMainWindow?) */
void virt_viewer_window_menu_view_zoom_out(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_view_zoom_in(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_view_zoom_reset(GtkWidget *menu, VirtViewerWindow *self);
gboolean virt_viewer_window_delete(GtkWidget *src, void *dummy, VirtViewerWindow *self);
void virt_viewer_window_menu_file_quit(GtkWidget *src, VirtViewerWindow *self);
void virt_viewer_window_guest_details_response(GtkDialog *dialog, gint response_id, gpointer user_data);
void virt_viewer_window_menu_help_about(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_help_guest_details(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_view_fullscreen(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_send(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_file_screenshot(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_file_usb_device_selection(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_file_smartcard_insert(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_file_smartcard_remove(GtkWidget *menu, VirtViewerWindow *self);
void virt_viewer_window_menu_view_release_cursor(GtkWidget *menu, VirtViewerWindow *self);

/* Internal methods */
static void virt_viewer_window_enable_modifiers(VirtViewerWindow *self);
static void virt_viewer_window_disable_modifiers(VirtViewerWindow *self);
static void virt_viewer_window_resize(VirtViewerWindow *self, gboolean keep_win_size);
static void virt_viewer_window_toolbar_setup(VirtViewerWindow *self);
static GtkMenu* virt_viewer_window_get_keycombo_menu(VirtViewerWindow *self);

static void restore_configuration(VirtViewerWindow *win);

G_DEFINE_TYPE (VirtViewerWindow, virt_viewer_window, G_TYPE_OBJECT)

#define GET_PRIVATE(o)                                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), VIRT_VIEWER_TYPE_WINDOW, VirtViewerWindowPrivate))

enum {
    PROP_0,
    PROP_WINDOW,
    PROP_DISPLAY,
    PROP_SUBTITLE,
    PROP_APP,   
};

struct _VirtViewerWindowPrivate {
    VirtViewerApp *app;

    GtkBuilder *builder;
    GtkWidget *window;
    GtkWidget *layout;
    GtkWidget *toolbar;
    //GtkWidget *toolbar_usb_device_selection;
    GtkWidget *toolbar_send_key;
    GtkAccelGroup *accel_group;
    VirtViewerNotebook *notebook;
    VirtViewerDisplay *display;

    gboolean accel_enabled;
    GValue accel_setting;
    GSList *accel_list;
    gboolean enable_mnemonics_save;
    gboolean grabbed;
    gint fullscreen_monitor;
    gboolean desktop_resize_pending;
    gboolean kiosk;

    gint zoomlevel;
    gboolean fullscreen;
    gchar *subtitle;
		
#ifdef G_OS_WIN32
	gint						 win_x;
	gint						 win_y;
#endif
	
};
/*option*/
static gboolean version = FALSE, enable_toolbar=FALSE, is_mode_vm=FALSE, passwd_is_needed=FALSE, complete_fullscreen=FALSE;
static char * title = NULL;
static char *port = 0;
static int usb_indexs[4] = {0};
/*global*/
static GKeyFile      *keyfile = NULL;
static gchar *conf_file;

static void
virt_viewer_window_get_property (GObject *object, guint property_id,
                                 GValue *value, GParamSpec *pspec)
{
    VirtViewerWindow *self = VIRT_VIEWER_WINDOW(object);
    VirtViewerWindowPrivate *priv = self->priv;

    switch (property_id) {
    case PROP_SUBTITLE:
        g_value_set_string(value, priv->subtitle);
        break;

    case PROP_WINDOW:
        g_value_set_object(value, priv->window);
        break;

    case PROP_DISPLAY:
        g_value_set_object(value, virt_viewer_window_get_display(self));
        break;

    case PROP_APP:
        g_value_set_object(value, priv->app);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
virt_viewer_window_set_property (GObject *object, guint property_id,
                                 const GValue *value, GParamSpec *pspec)
{
    VirtViewerWindowPrivate *priv = VIRT_VIEWER_WINDOW(object)->priv;

    switch (property_id) {
    case PROP_SUBTITLE:
        g_free(priv->subtitle);
        priv->subtitle = g_value_dup_string(value);
        virt_viewer_window_update_title(VIRT_VIEWER_WINDOW(object));
        break;

    case PROP_APP:
        g_return_if_fail(priv->app == NULL);
        priv->app = g_value_get_object(value);
        break;
	
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
virt_viewer_window_dispose (GObject *object)
{
    VirtViewerWindowPrivate *priv = VIRT_VIEWER_WINDOW(object)->priv;
    GSList *it;

    if (priv->display) {
        g_object_unref(priv->display);
        priv->display = NULL;
    }

    g_debug("Disposing window %p\n", object);

    if (priv->window) {
        gtk_widget_destroy(priv->window);
        priv->window = NULL;
    }
    if (priv->builder) {
        g_object_unref(priv->builder);
        priv->builder = NULL;
    }

    for (it = priv->accel_list ; it != NULL ; it = it->next) {
        g_object_unref(G_OBJECT(it->data));
    }
    g_slist_free(priv->accel_list);
    priv->accel_list = NULL;

    g_free(priv->subtitle);
    priv->subtitle = NULL;

    g_value_unset(&priv->accel_setting);
    g_clear_object(&priv->toolbar);

	//g_free(keyfile);
	//g_free(conf_file);
	//g_free(title);
    G_OBJECT_CLASS (virt_viewer_window_parent_class)->dispose (object);
}

static void
rebuild_combo_menu(GObject    *gobject G_GNUC_UNUSED,
                   GParamSpec *pspec G_GNUC_UNUSED,
                   gpointer    user_data)
{
    VirtViewerWindow *self = user_data;
    GtkWidget *menu;

    menu = GTK_WIDGET(gtk_builder_get_object(self->priv->builder, "menu-send"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu),
                              GTK_WIDGET(virt_viewer_window_get_keycombo_menu(self)));
    gtk_widget_set_sensitive(menu, (self->priv->display != NULL));
}

static void
virt_viewer_window_constructed(GObject *object)
{
    VirtViewerWindowPrivate *priv = VIRT_VIEWER_WINDOW(object)->priv;

    if (G_OBJECT_CLASS(virt_viewer_window_parent_class)->constructed)
        G_OBJECT_CLASS(virt_viewer_window_parent_class)->constructed(object);

    g_signal_connect(priv->app, "notify::enable-accel",
                     G_CALLBACK(rebuild_combo_menu), object);
    rebuild_combo_menu(NULL, NULL, object);
}

static void
virt_viewer_window_class_init (VirtViewerWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (VirtViewerWindowPrivate));

    object_class->get_property = virt_viewer_window_get_property;
    object_class->set_property = virt_viewer_window_set_property;
    object_class->dispose = virt_viewer_window_dispose;
    object_class->constructed = virt_viewer_window_constructed;

    g_object_class_install_property(object_class,
                                    PROP_SUBTITLE,
                                    g_param_spec_string("subtitle",
                                                        "Subtitle",
                                                        "Window subtitle",
                                                        "",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS));	

	g_object_class_install_property(object_class,
                                    PROP_WINDOW,
                                    g_param_spec_object("window",
                                                        "Window",
                                                        "GtkWindow",
                                                        GTK_TYPE_WIDGET,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_DISPLAY,
                                    g_param_spec_object("display",
                                                        "Display",
                                                        "VirtDisplay",
                                                        VIRT_VIEWER_TYPE_DISPLAY,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class,
                                    PROP_APP,
                                    g_param_spec_object("app",
                                                        "App",
                                                        "VirtViewerApp",
                                                        VIRT_VIEWER_TYPE_APP,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
	
}

static gboolean
can_activate_cb (GtkWidget *widget G_GNUC_UNUSED,
                 guint signal_id G_GNUC_UNUSED,
                 VirtViewerWindow *self G_GNUC_UNUSED)
{
    return TRUE;
}

#if GTK_CHECK_VERSION(3, 0, 0)
void init_with_css(void){
    /*--- CSS -----------------*/
    GtkCssProvider *provider; 
    GdkDisplay *display;
    GdkScreen *screen;
    /*-------------------------*/
    /** css  init */
     provider = gtk_css_provider_new ();
     display = gdk_display_get_default ();
     screen = gdk_display_get_default_screen (display);

     gtk_style_context_add_provider_for_screen (screen,
                                     GTK_STYLE_PROVIDER (provider),
                                     GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

     gsize bytes_written, bytes_read;

     #ifndef G_OS_WIN32
      const gchar* home = g_build_filename(g_path_get_dirname(LOCALE_DIR),"css", "tcloud.css", NULL);  
     #else
      gchar* current_path = g_build_path(G_DIR_SEPARATOR_S, g_path_get_dirname(g_find_program_in_path(g_get_application_name())), NULL);
      const gchar* home = g_build_path(G_DIR_SEPARATOR_S, g_path_get_dirname(current_path), "share", "css", "tcloud.css", NULL);
     #endif

     GError *error = 0;
    
     gtk_css_provider_load_from_path(provider,
                                      g_filename_to_utf8(home, strlen(home), &bytes_read, &bytes_written, &error),
                                    NULL);
     
     g_object_unref (provider);
}
#endif

static void log_handler(const gchar *log_domain,
                        GLogLevelFlags log_level,
                        const gchar *message,
                        gpointer user_data)
{   
   GTimeVal  time;
   gchar *tmp_buffer;
   FILE *logfile;
   g_get_current_time( &time );
   /* Convert offset to real date */
   
   tmp_buffer = g_time_val_to_iso8601(&time);
   if(user_data)
        logfile = fopen (g_build_filename((const gchar *)user_data, "log", "evdi_gtk.log", NULL), "a");
   else
        logfile = fopen (g_build_filename(g_get_user_config_dir(), "evdi_gtk.log", NULL), "a");
   
   if (logfile == NULL)
   {
        /* Fall back to console output if unable to open file */
        g_print("Error, Rerouted to console: %s\n", message);
        return;
   }

   fprintf (logfile, "%s:%s\n", tmp_buffer, message);
   fclose (logfile);
   g_free(tmp_buffer);
}

static void
virt_viewer_window_init (VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv;
    GtkWidget *vbox;
    GdkColor color;
    GSList *accels;

    self->priv = GET_PRIVATE(self);
    priv = self->priv;

    priv->fullscreen_monitor = -1;
    g_value_init(&priv->accel_setting, G_TYPE_STRING);

    priv->notebook = virt_viewer_notebook_new();
    priv->builder = virt_viewer_util_load_ui("virt-viewer.xml");

    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(self->priv->builder, "menu-send")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(self->priv->builder, "menu-file-screenshot")), FALSE);

    gtk_builder_connect_signals(priv->builder, self);

    priv->accel_group = GTK_ACCEL_GROUP(gtk_builder_get_object(priv->builder, "accelgroup"));

    /* make sure they can be activated even if the menu item is not visible */
    g_signal_connect(gtk_builder_get_object(priv->builder, "menu-view-fullscreen"),
                     "can-activate-accel", G_CALLBACK(can_activate_cb), self);
    g_signal_connect(gtk_builder_get_object(priv->builder, "menu-file-smartcard-insert"),
                     "can-activate-accel", G_CALLBACK(can_activate_cb), self);
    g_signal_connect(gtk_builder_get_object(priv->builder, "menu-file-smartcard-remove"),
                     "can-activate-accel", G_CALLBACK(can_activate_cb), self);
    g_signal_connect(gtk_builder_get_object(priv->builder, "menu-view-release-cursor"),
                     "can-activate-accel", G_CALLBACK(can_activate_cb), self);
    g_signal_connect(gtk_builder_get_object(priv->builder, "menu-view-zoom-reset"),
                     "can-activate-accel", G_CALLBACK(can_activate_cb), self);
    g_signal_connect(gtk_builder_get_object(priv->builder, "menu-view-zoom-in"),
                     "can-activate-accel", G_CALLBACK(can_activate_cb), self);
    g_signal_connect(gtk_builder_get_object(priv->builder, "menu-view-zoom-out"),
                     "can-activate-accel", G_CALLBACK(can_activate_cb), self);

    vbox = GTK_WIDGET(gtk_builder_get_object(priv->builder, "viewer-box"));
    virt_viewer_window_toolbar_setup(self);

    gtk_box_pack_end(GTK_BOX(vbox), priv->layout, TRUE, TRUE, 0);
    gdk_color_parse("red", &color);
    gtk_widget_modify_bg(priv->layout, GTK_STATE_NORMAL, &color);

    priv->window = GTK_WIDGET(gtk_builder_get_object(priv->builder, "viewer"));
    gtk_window_add_accel_group(GTK_WINDOW(priv->window), priv->accel_group);

    virt_viewer_window_update_title(self);
    gtk_window_set_resizable(GTK_WINDOW(priv->window), TRUE);
#if GTK_CHECK_VERSION(3, 0, 0)
    gtk_window_set_has_resize_grip(GTK_WINDOW(priv->window), FALSE);
#endif
    priv->accel_enabled = TRUE;

    accels = gtk_accel_groups_from_object(G_OBJECT(priv->window));
    for ( ; accels ; accels = accels->next) {
        priv->accel_list = g_slist_append(priv->accel_list, accels->data);
        g_object_ref(G_OBJECT(accels->data));
    }

    priv->zoomlevel = 100;

//pipo

#if defined(G_OS_WIN32)
    gtk_window_set_default_icon_name("rtclient");
    int ret = open_pipe();
    if(ret<0)
    {
        g_warning("open pipe error!");
    }
#endif


//usb

#if GTK_CHECK_VERSION(3,0,0)
			init_with_css();
#endif

GError *error = NULL;


#if defined(G_OS_WIN32)
    gchar *locale_path;
    gchar *log_path;
#endif

#if defined(G_OS_WIN32)
    log_path = g_build_path(G_DIR_SEPARATOR_S, g_path_get_dirname(g_find_program_in_path(g_get_application_name())), NULL);
    locale_path = g_build_path(G_DIR_SEPARATOR_S, g_path_get_dirname(log_path), "share", "locale", NULL);
    bindtextdomain(GETTEXT_PACKAGE, locale_path);
#else
    bindtextdomain(GETTEXT_PACKAGE, LOCALE_DIR);
#endif  

    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    
    

    keyfile = g_key_file_new();
    int mode = S_IRWXU;

#if defined(G_OS_WIN32)
    TCHAR szBuff[256] = {0}, *szPath = TEXT("software\\evdi-client");
    HKEY hkey = NULL;
    DWORD hsize = 256;
    if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, szPath, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return 1;

    if(RegQueryValueEx(hkey, TEXT("InstallPath"), 0, NULL, (UCHAR *)szBuff, &hsize) != ERROR_SUCCESS){
        return 1;
    }
    conf_file = g_build_filename((const gchar *)szBuff, "conf", NULL);
    //if(!getenv("EVDI_LOG_CONSOLE"))
        //g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_MASK, log_handler, szBuff);
#else
    conf_file = g_build_filename("/etc/evdi", "config", NULL);
   // if(!getenv("EVDI_LOG_CONSOLE"))
       // g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_MASK, log_handler, NULL);
#endif


    if (g_mkdir_with_parents(conf_file, mode) == -1)
        g_warning("failed to create config directory");
    g_free(conf_file);


#if defined(G_OS_WIN32)
    conf_file = g_build_filename((const gchar *)szBuff, "conf", "gtk_settings", NULL);
    RegCloseKey(hkey);
#else
    conf_file = g_build_filename("/etc/evdi", "config", "gtk_settings", NULL);
#endif

	if(!g_file_test(conf_file, G_FILE_TEST_IS_DIR))
		{
                g_mkdir_with_parents(conf_file, 0755);
				g_warning("create conf_file");
		}
					 	
    if (!g_key_file_load_from_file(keyfile, conf_file,
                                   G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS, &error)) {
        g_warning("Couldn't load configuration: %s", error->message);
        g_clear_error(&error);
		error = NULL;
    }

//g_free(conf_file);
			
#if defined(G_OS_WIN32)
				g_free(locale_path);
				g_free(log_path);
#endif

//restore_configuration(self);

}

static void
virt_viewer_window_desktop_resize(VirtViewerDisplay *display G_GNUC_UNUSED,
                                  VirtViewerWindow *self)
{
    if (!gtk_widget_get_visible(self->priv->window)) {
        self->priv->desktop_resize_pending = TRUE;
        return;
    }
    virt_viewer_window_resize(self, FALSE);
}


G_MODULE_EXPORT void
virt_viewer_window_menu_view_zoom_out(GtkWidget *menu G_GNUC_UNUSED,
                                      VirtViewerWindow *self)
{
    virt_viewer_window_set_zoom_level(self, self->priv->zoomlevel - 10);
}

G_MODULE_EXPORT void
virt_viewer_window_menu_view_zoom_in(GtkWidget *menu G_GNUC_UNUSED,
                                     VirtViewerWindow *self)
{
    virt_viewer_window_set_zoom_level(self, self->priv->zoomlevel + 10);
}

G_MODULE_EXPORT void
virt_viewer_window_menu_view_zoom_reset(GtkWidget *menu G_GNUC_UNUSED,
                                        VirtViewerWindow *self)
{
    virt_viewer_window_set_zoom_level(self, 100);
}

/* Kick GtkWindow to tell it to adjust to our new widget sizes */
static void
virt_viewer_window_queue_resize(VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv = self->priv;
#if GTK_CHECK_VERSION(3, 0, 0)
    GtkRequisition nat;

    gtk_window_set_default_size(GTK_WINDOW(priv->window), -1, -1);
    gtk_widget_get_preferred_size(GTK_WIDGET(priv->window), NULL, &nat);
    gtk_window_resize(GTK_WINDOW(priv->window), nat.width, nat.height);
#else
    gtk_window_resize(GTK_WINDOW(priv->window), 1, 1);
#endif
}

/*
 * This code attempts to resize the top level window to be large enough
 * to contain the entire display desktop at 1:1 ratio. If the local desktop
 * isn't large enough that it goes as large as possible and lets the display
 * scale down to fit, maintaining aspect ratio
 */
static void
virt_viewer_window_resize(VirtViewerWindow *self, gboolean keep_win_size)
{
    GdkRectangle fullscreen;
    GdkScreen *screen;
    int width, height;
    double desktopAspect;
    double screenAspect;
    guint desktopWidth, display_width;
    guint desktopHeight, display_height;
    VirtViewerWindowPrivate *priv = self->priv;

    if (priv->fullscreen)
        return;

    g_debug("Preparing main window resize");
    if (!priv->display) {
        g_debug("Skipping inactive resize");
        return;
    }

    virt_viewer_display_get_desktop_size(VIRT_VIEWER_DISPLAY(priv->display),
                                         &desktopWidth, &desktopHeight);

    screen = gtk_widget_get_screen(priv->window);
    gdk_screen_get_monitor_geometry(screen,
                                    gdk_screen_get_monitor_at_window
                                    (screen, gtk_widget_get_window(priv->window)),
                                    &fullscreen);

    g_return_if_fail(desktopWidth > 0);
    g_return_if_fail(desktopHeight > 0);

    desktopAspect = (double)desktopWidth / (double)desktopHeight;
    screenAspect = (double)fullscreen.width / (double)fullscreen.height;

    display_width = desktopWidth * priv->zoomlevel / 100.0;
    display_height = desktopHeight * priv->zoomlevel / 100.0;

    if ((display_width > fullscreen.width) ||
        (display_height > fullscreen.height)) {
        /* Doesn't fit native res, so go as large as possible
           maintaining aspect ratio */
        if (screenAspect > desktopAspect) {
            width = fullscreen.height * desktopAspect;
            height = fullscreen.height;
        } else {
            width = fullscreen.width;
            height = fullscreen.width / desktopAspect;
        }
        width *= 100.0 / priv->zoomlevel;
        height *= 100.0 / priv->zoomlevel;
    } else {
        width = desktopWidth;
        height = desktopHeight;
    }

    g_debug("Decided todo %dx%d (desktop is %dx%d, fullscreen is %dx%d",
              width, height, desktopWidth, desktopHeight,
              fullscreen.width, fullscreen.height);

    virt_viewer_display_set_desktop_size(VIRT_VIEWER_DISPLAY(priv->display),
                                         width, height);

    if (!keep_win_size)
        virt_viewer_window_queue_resize(self);
}

static void
virt_viewer_window_move_to_monitor(VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv = self->priv;
    GdkRectangle mon;
    gint n = priv->fullscreen_monitor;

    if (n == -1)
        return;

    gdk_screen_get_monitor_geometry(gdk_screen_get_default(), n, &mon);
    gtk_window_move(GTK_WINDOW(priv->window), mon.x, mon.y);

    gtk_widget_set_size_request(GTK_WIDGET(priv->window),
                                mon.width,
                                mon.height);
}

static gboolean
mapped(GtkWidget *widget, GdkEvent *event G_GNUC_UNUSED,
       VirtViewerWindow *self)
{
    g_signal_handlers_disconnect_by_func(widget, mapped, self);
    self->priv->fullscreen = FALSE;
    virt_viewer_window_enter_fullscreen(self, self->priv->fullscreen_monitor);
    return FALSE;
}

static void
virt_viewer_window_menu_fullscreen_set_active(VirtViewerWindow *self, gboolean active)
{
    GtkCheckMenuItem *check = GTK_CHECK_MENU_ITEM(gtk_builder_get_object(self->priv->builder, "menu-view-fullscreen"));

    g_signal_handlers_block_by_func(check, virt_viewer_window_menu_view_fullscreen, self);
    gtk_check_menu_item_set_active(check, active);
    g_signal_handlers_unblock_by_func(check, virt_viewer_window_menu_view_fullscreen, self);
}

void
virt_viewer_window_leave_fullscreen(VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv = self->priv;
    GtkWidget *menu = GTK_WIDGET(gtk_builder_get_object(priv->builder, "top-menu"));

    /* if we enter and leave fullscreen mode before being shown, make sure to
     * disconnect the mapped signal handler */
    g_signal_handlers_disconnect_by_func(priv->window, mapped, self);

    if (!priv->fullscreen)
        return;

    virt_viewer_window_menu_fullscreen_set_active(self, FALSE);
    priv->fullscreen = FALSE;
    priv->fullscreen_monitor = -1;
    if (priv->display) {
        virt_viewer_display_set_monitor(priv->display, -1);
        virt_viewer_display_set_fullscreen(priv->display, FALSE);
    }
    ViewAutoDrawer_SetActive(VIEW_AUTODRAWER(priv->layout), FALSE);
    gtk_widget_show(menu);
    gtk_widget_hide(priv->toolbar);
    gtk_widget_set_size_request(GTK_WIDGET(priv->window), -1, -1);
    gtk_window_unfullscreen(GTK_WINDOW(priv->window));

}

void
virt_viewer_window_enter_fullscreen(VirtViewerWindow *self, gint monitor)
{
    VirtViewerWindowPrivate *priv = self->priv;
    GtkWidget *menu = GTK_WIDGET(gtk_builder_get_object(priv->builder, "top-menu"));

    if (priv->fullscreen && priv->fullscreen_monitor != monitor)
        virt_viewer_window_leave_fullscreen(self);

    if (priv->fullscreen)
        return;

    priv->fullscreen_monitor = monitor;
    priv->fullscreen = TRUE;

    if (!gtk_widget_get_mapped(priv->window)) {
        /*
         * To avoid some races with metacity, the window should be placed
         * as early as possible, before it is (re)allocated & mapped
         * Position & size should not be queried yet. (rhbz#809546).
         */
        virt_viewer_window_move_to_monitor(self);
        g_signal_connect(priv->window, "map-event", G_CALLBACK(mapped), self);
        return;
    }

    virt_viewer_window_menu_fullscreen_set_active(self, TRUE);
    gtk_widget_hide(menu);
    gtk_widget_show(priv->toolbar);
    ViewAutoDrawer_SetActive(VIEW_AUTODRAWER(priv->layout), TRUE);
    ViewAutoDrawer_Close(VIEW_AUTODRAWER(priv->layout));

    if (priv->display) {
        virt_viewer_display_set_monitor(priv->display, monitor);
        virt_viewer_display_set_fullscreen(priv->display, TRUE);
    }
    virt_viewer_window_move_to_monitor(self);

    gtk_window_fullscreen(GTK_WINDOW(priv->window));
}

#define MAX_KEY_COMBO 4
struct keyComboDef {
    guint keys[MAX_KEY_COMBO];
    const char *label;
    const gchar* accel_path;
};

static const struct keyComboDef keyCombos[] = {
    { { GDK_Control_L, GDK_Alt_L, GDK_Delete, GDK_VoidSymbol }, N_("Ctrl+Alt+_Del"), "<virt-viewer>/send/secure-attention"},
    { { GDK_Control_L, GDK_Alt_L, GDK_BackSpace, GDK_VoidSymbol }, N_("Ctrl+Alt+_Backspace"), NULL},
    { { GDK_VoidSymbol }, "" , NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F1, GDK_VoidSymbol }, N_("Ctrl+Alt+F_1"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F2, GDK_VoidSymbol }, N_("Ctrl+Alt+F_2"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F3, GDK_VoidSymbol }, N_("Ctrl+Alt+F_3"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F4, GDK_VoidSymbol }, N_("Ctrl+Alt+F_4"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F5, GDK_VoidSymbol }, N_("Ctrl+Alt+F_5"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F6, GDK_VoidSymbol }, N_("Ctrl+Alt+F_6"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F7, GDK_VoidSymbol }, N_("Ctrl+Alt+F_7"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F8, GDK_VoidSymbol }, N_("Ctrl+Alt+F_8"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F9, GDK_VoidSymbol }, N_("Ctrl+Alt+F_9"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F10, GDK_VoidSymbol }, N_("Ctrl+Alt+F1_0"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F11, GDK_VoidSymbol }, N_("Ctrl+Alt+F11"), NULL},
    { { GDK_Control_L, GDK_Alt_L, GDK_F12, GDK_VoidSymbol }, N_("Ctrl+Alt+F12"), NULL},
    { { GDK_VoidSymbol }, "" , NULL},
    { { GDK_Print, GDK_VoidSymbol }, "_PrintScreen", NULL},
};

static guint
get_nkeys(const guint *keys)
{
    guint i;

    for (i = 0; keys[i] != GDK_VoidSymbol; )
        i++;

    return i;
}

G_MODULE_EXPORT void
virt_viewer_window_menu_send(GtkWidget *menu,
                             VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv = self->priv;

    g_return_if_fail(priv->display != NULL);
    guint *keys = g_object_get_data(G_OBJECT(menu), "vv-keys");
    g_return_if_fail(keys != NULL);

    virt_viewer_display_send_keys(VIRT_VIEWER_DISPLAY(priv->display),
                                  keys, get_nkeys(keys));
}

static void
virt_viewer_menu_add_combo(VirtViewerWindow *self, GtkMenu *menu,
                           const guint *keys, const gchar *label, const gchar* accel_path)
{
    GtkWidget *item;

    if (keys == NULL || keys[0] == GDK_VoidSymbol) {
        item = gtk_separator_menu_item_new();
    } else {
        item = gtk_menu_item_new_with_mnemonic(label);
        if (accel_path) {
            gtk_menu_item_set_accel_path(GTK_MENU_ITEM(item), accel_path);
            /* make accel work in fullscreen */
            g_signal_connect(item, "can-activate-accel", G_CALLBACK(can_activate_cb), self);
        }
        guint *ckeys = g_memdup(keys, (get_nkeys(keys) + 1) * sizeof(guint));
        g_object_set_data_full(G_OBJECT(item), "vv-keys", ckeys, g_free);
        g_signal_connect(item, "activate", G_CALLBACK(virt_viewer_window_menu_send), self);
    }

    gtk_container_add(GTK_CONTAINER(menu), item);
}

static guint*
accel_key_to_keys(const GtkAccelKey *key)
{
    guint val;
    GArray *a = g_array_new(FALSE, FALSE, sizeof(guint));

    g_warn_if_fail((key->accel_mods &
                    ~(GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)) == 0);

    /* first, send the modifiers */
    if (key->accel_mods & GDK_SHIFT_MASK) {
        val = GDK_Shift_L;
        g_array_append_val(a, val);
    }

    if (key->accel_mods & GDK_CONTROL_MASK) {
        val = GDK_Control_L;
        g_array_append_val(a, val);
    }

    if (key->accel_mods & GDK_MOD1_MASK) {
        val = GDK_Alt_L;
        g_array_append_val(a, val);
    }

    /* only after, the non-modifier key (ctrl-t, not t-ctrl) */
    val = key->accel_key;
    g_array_append_val(a, val);

    val = GDK_VoidSymbol;
    g_array_append_val(a, val);

    return (guint*)g_array_free(a, FALSE);
}

struct accelCbData
{
    VirtViewerWindow *self;
    GtkMenu *menu;
};

static void
accel_map_item_cb(gpointer data,
                  const gchar *accel_path,
                  guint accel_key,
                  GdkModifierType accel_mods,
                  gboolean changed G_GNUC_UNUSED)
{
    struct accelCbData *d = data;
    GtkAccelKey key = {
        .accel_key = accel_key,
        .accel_mods = accel_mods
    };

    if (!g_str_has_prefix(accel_path, "<virt-viewer>"))
        return;
    if (accel_key == GDK_VoidSymbol || accel_key == 0)
        return;

    guint *keys = accel_key_to_keys(&key);
    gchar *label = gtk_accelerator_get_label(accel_key, accel_mods);
    virt_viewer_menu_add_combo(d->self, d->menu, keys, label, NULL);
    g_free(label);
    g_free(keys);
}

static GtkMenu*
virt_viewer_window_get_keycombo_menu(VirtViewerWindow *self)
{
    gint i;
    VirtViewerWindowPrivate *priv = self->priv;
    GtkMenu *menu = GTK_MENU(gtk_menu_new());
    gtk_menu_set_accel_group(menu, priv->accel_group);

    for (i = 0 ; i < G_N_ELEMENTS(keyCombos); i++) {
        virt_viewer_menu_add_combo(self, menu, keyCombos[i].keys, keyCombos[i].label, keyCombos[i].accel_path);
    }

    if (virt_viewer_app_get_enable_accel(priv->app)) {
        struct accelCbData d = {
            .self = self,
            .menu = menu
        };

        gtk_accel_map_foreach(&d, accel_map_item_cb);
    }

    gtk_widget_show_all(GTK_WIDGET(menu));
    return menu;
}

void
virt_viewer_window_disable_modifiers(VirtViewerWindow *self)
{
    GtkSettings *settings = gtk_settings_get_default();
    VirtViewerWindowPrivate *priv = self->priv;
    GValue empty;
    GSList *accels;

    if (!priv->accel_enabled)
        return;

    /* This stops F10 activating menu bar */
    memset(&empty, 0, sizeof empty);
    g_value_init(&empty, G_TYPE_STRING);
    g_object_get_property(G_OBJECT(settings), "gtk-menu-bar-accel", &priv->accel_setting);
    g_object_set_property(G_OBJECT(settings), "gtk-menu-bar-accel", &empty);

    /* This stops global accelerators like Ctrl+Q == Quit */
    for (accels = priv->accel_list ; accels ; accels = accels->next) {
        if (virt_viewer_app_get_enable_accel(priv->app) &&
            priv->accel_group == accels->data)
            continue;
        gtk_window_remove_accel_group(GTK_WINDOW(priv->window), accels->data);
    }

    /* This stops menu bar shortcuts like Alt+F == File */
    g_object_get(settings,
                 "gtk-enable-mnemonics", &priv->enable_mnemonics_save,
                 NULL);
    g_object_set(settings,
                 "gtk-enable-mnemonics", FALSE,
                 NULL);

    priv->accel_enabled = FALSE;
}

void
virt_viewer_window_enable_modifiers(VirtViewerWindow *self)
{
    GtkSettings *settings = gtk_settings_get_default();
    VirtViewerWindowPrivate *priv = self->priv;
    GSList *accels;

    if (priv->accel_enabled)
        return;

    /* This allows F10 activating menu bar */
    g_object_set_property(G_OBJECT(settings), "gtk-menu-bar-accel", &priv->accel_setting);

    /* This allows global accelerators like Ctrl+Q == Quit */
    for (accels = priv->accel_list ; accels ; accels = accels->next) {
        if (virt_viewer_app_get_enable_accel(priv->app) &&
            priv->accel_group == accels->data)
            continue;
        gtk_window_add_accel_group(GTK_WINDOW(priv->window), accels->data);
    }

    /* This allows menu bar shortcuts like Alt+F == File */
    g_object_set(settings,
                 "gtk-enable-mnemonics", priv->enable_mnemonics_save,
                 NULL);

    priv->accel_enabled = TRUE;
}


G_MODULE_EXPORT gboolean
virt_viewer_window_delete(GtkWidget *src G_GNUC_UNUSED,
                          void *dummy G_GNUC_UNUSED,
                          VirtViewerWindow *self)
{
    g_debug("Window closed");
    virt_viewer_app_maybe_quit(self->priv->app, self);
    return TRUE;
}


G_MODULE_EXPORT void
virt_viewer_window_menu_file_quit(GtkWidget *src G_GNUC_UNUSED,
                                  VirtViewerWindow *self)
{
    virt_viewer_app_maybe_quit(self->priv->app, self);
}


static void
virt_viewer_window_set_fullscreen(VirtViewerWindow *self,
                                  gboolean fullscreen)
{
    if (fullscreen) {
        virt_viewer_window_enter_fullscreen(self, -1);
    } else {
        /* leave all windows fullscreen state */
        if (virt_viewer_app_get_fullscreen(self->priv->app))
            g_object_set(self->priv->app, "fullscreen", FALSE, NULL);
        /* or just this window */
        else
            virt_viewer_window_leave_fullscreen(self);
    }
}

static void
virt_viewer_window_toolbar_leave_fullscreen(GtkWidget *button G_GNUC_UNUSED,
                                            VirtViewerWindow *self)
{
    virt_viewer_window_set_fullscreen(self, FALSE);
}

static void keycombo_menu_location(GtkMenu *menu G_GNUC_UNUSED, gint *x, gint *y,
                                   gboolean *push_in, gpointer user_data)
{
    VirtViewerWindow *self = user_data;
    GtkAllocation allocation;
    GtkWidget *toplevel = gtk_widget_get_toplevel(self->priv->toolbar_send_key);

    *push_in = TRUE;
    gdk_window_get_origin(gtk_widget_get_window(toplevel), x, y);
    gtk_widget_translate_coordinates(self->priv->toolbar_send_key, toplevel,
                                     *x, *y, x, y);
    gtk_widget_get_allocation(self->priv->toolbar_send_key, &allocation);
    *y += allocation.height;
}

static void
virt_viewer_window_toolbar_send_key(GtkWidget *button G_GNUC_UNUSED,
                                    VirtViewerWindow *self)
{
    GtkMenu *menu = virt_viewer_window_get_keycombo_menu(self);
    gtk_menu_attach_to_widget(menu, GTK_WIDGET(self->priv->window), NULL);
    g_object_ref_sink(menu);
    gtk_menu_popup(menu, NULL, NULL, keycombo_menu_location, self,
                   0, gtk_get_current_event_time());
    g_object_unref(menu);
}


G_MODULE_EXPORT void
virt_viewer_window_menu_view_fullscreen(GtkWidget *menu,
                                        VirtViewerWindow *self)
{
    gboolean fullscreen = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menu));

    virt_viewer_window_set_fullscreen(self, fullscreen);
}

static void add_if_writable (GdkPixbufFormat *data, GHashTable *formats)
{
    if (gdk_pixbuf_format_is_writable(data)) {
        gchar **extensions;
        gchar **it;
        extensions = gdk_pixbuf_format_get_extensions(data);
        for (it = extensions; *it != NULL; it++) {
            g_hash_table_insert(formats, g_strdup(*it), data);
        }
        g_strfreev(extensions);
    }
}

static GHashTable *init_image_formats(void)
{
    GHashTable *format_map;
    GSList *formats = gdk_pixbuf_get_formats();

    format_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_slist_foreach(formats, (GFunc)add_if_writable, format_map);
    g_slist_free (formats);

    return format_map;
}

static GdkPixbufFormat *get_image_format(const char *filename)
{
    static GOnce image_formats_once = G_ONCE_INIT;
    const char *ext;

    g_once(&image_formats_once, (GThreadFunc)init_image_formats, NULL);

    ext = strrchr(filename, '.');
    if (ext == NULL)
        return NULL;

    ext++; /* skip '.' */

    return g_hash_table_lookup(image_formats_once.retval, ext);
}

static void
virt_viewer_window_save_screenshot(VirtViewerWindow *self,
                                   const char *file)
{
    VirtViewerWindowPrivate *priv = self->priv;
    GdkPixbuf *pix = virt_viewer_display_get_pixbuf(VIRT_VIEWER_DISPLAY(priv->display));
    GdkPixbufFormat *format = get_image_format(file);

    if (format == NULL) {
        g_debug("unknown file extension, falling back to png");
        if (!g_str_has_suffix(file, ".png")) {
            char *png_filename;
            png_filename = g_strconcat(file, ".png", NULL);
            gdk_pixbuf_save(pix, png_filename, "png", NULL,
                            "tEXt::Generator App", PACKAGE, NULL);
            g_free(png_filename);
        } else {
            gdk_pixbuf_save(pix, file, "png", NULL,
                            "tEXt::Generator App", PACKAGE, NULL);
        }
    } else {
        char *type = gdk_pixbuf_format_get_name(format);
        g_debug("saving to %s", type);
        gdk_pixbuf_save(pix, file, type, NULL, NULL);
        g_free(type);
    }

    g_object_unref(pix);
}

G_MODULE_EXPORT void
virt_viewer_window_menu_file_screenshot(GtkWidget *menu G_GNUC_UNUSED,
                                        VirtViewerWindow *self)
{
    GtkWidget *dialog;
    VirtViewerWindowPrivate *priv = self->priv;
    const char *image_dir;

    g_return_if_fail(priv->display != NULL);

    dialog = gtk_file_chooser_dialog_new("Save screenshot",
                                         NULL,
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER (dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                 GTK_WINDOW(self->priv->window));
    image_dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    if (image_dir != NULL)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (dialog), image_dir);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER (dialog), _("Screenshot"));

    if (gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename;

        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
        virt_viewer_window_save_screenshot(self, filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

G_MODULE_EXPORT void
virt_viewer_window_menu_file_usb_device_selection(GtkWidget *menu G_GNUC_UNUSED,
                                                  VirtViewerWindow *self)
{
    virt_viewer_session_usb_device_selection(virt_viewer_app_get_session(self->priv->app),
                                             GTK_WINDOW(self->priv->window));
}

G_MODULE_EXPORT void
virt_viewer_window_menu_file_smartcard_insert(GtkWidget *menu G_GNUC_UNUSED,
                                              VirtViewerWindow *self)
{
    virt_viewer_session_smartcard_insert(virt_viewer_app_get_session(self->priv->app));
}

G_MODULE_EXPORT void
virt_viewer_window_menu_file_smartcard_remove(GtkWidget *menu G_GNUC_UNUSED,
                                              VirtViewerWindow *self)
{
    virt_viewer_session_smartcard_remove(virt_viewer_app_get_session(self->priv->app));
}

G_MODULE_EXPORT void
virt_viewer_window_menu_view_release_cursor(GtkWidget *menu G_GNUC_UNUSED,
                                            VirtViewerWindow *self)
{
    g_return_if_fail(self->priv->display != NULL);
    virt_viewer_display_release_cursor(VIRT_VIEWER_DISPLAY(self->priv->display));
}

G_MODULE_EXPORT void
virt_viewer_window_menu_help_guest_details(GtkWidget *menu G_GNUC_UNUSED,
                                           VirtViewerWindow *self)
{
    GtkBuilder *ui = virt_viewer_util_load_ui("virt-viewer-guest-details.xml");
    char *name = NULL;
    char *uuid = NULL;

    g_return_if_fail(ui != NULL);

    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(ui, "guestdetailsdialog"));
    GtkWidget *namelabel = GTK_WIDGET(gtk_builder_get_object(ui, "namevaluelabel"));
    GtkWidget *guidlabel = GTK_WIDGET(gtk_builder_get_object(ui, "guidvaluelabel"));

    g_return_if_fail(dialog && namelabel && guidlabel);

    g_object_get(self->priv->app, "guest-name", &name, "uuid", &uuid, NULL);

    if (!name || *name == '\0')
        name = g_strdup(_("Unknown"));
    if (!uuid || *uuid == '\0')
        uuid = g_strdup(_("Unknown"));
    gtk_label_set_text(GTK_LABEL(namelabel), name);
    gtk_label_set_text(GTK_LABEL(guidlabel), uuid);
    g_free(name);
    g_free(uuid);

    gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                 GTK_WINDOW(self->priv->window));

    gtk_builder_connect_signals(ui, self);

    gtk_widget_show_all(dialog);

    g_object_unref(G_OBJECT(ui));
}

G_MODULE_EXPORT void
virt_viewer_window_guest_details_response(GtkDialog *dialog,
                                          gint response_id,
                                          gpointer user_data G_GNUC_UNUSED)
{
    if (response_id == GTK_RESPONSE_CLOSE)
        gtk_widget_hide(GTK_WIDGET(dialog));
}

G_MODULE_EXPORT void
virt_viewer_window_menu_help_about(GtkWidget *menu G_GNUC_UNUSED,
                                   VirtViewerWindow *self)
{
    GtkBuilder *about = virt_viewer_util_load_ui("virt-viewer-about.xml");

    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(about, "about"));
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), VERSION BUILDID);

    gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                 GTK_WINDOW(self->priv->window));

    gtk_builder_connect_signals(about, self);

    gtk_widget_show_all(dialog);

    g_object_unref(G_OBJECT(about));
}

#if defined(G_OS_WIN32)
int handle_open_pipe_error(void){
     DWORD errval  = GetLastError();
     if (errval != ERROR_PIPE_BUSY) 
     {
         gchar *errstr = g_win32_error_message(errval);
         g_warning("Could not open pipe(%ld) %s\n", errval, errstr);
         g_free(errstr);
         return -1;
     }
     if ( !WaitNamedPipe(PIPE_NAME, 500)) 
     { 
         g_warning("Could not open pipe: 500 msecond wait timed out.\n"); 
         return -1;
     } 
     return 1;
}
int open_pipe(void)
{

    pipe = CreateFile (PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (pipe == INVALID_HANDLE_VALUE ) {
       return handle_open_pipe_error();
    }
    return 1;
}

int handle_write_pipe_error(void){
    DWORD errval  = GetLastError();
    gchar *errstr = g_win32_error_message(errval);
    g_warning("Write to pipe failed (%ld) %s\n", errval, errstr);
    g_free(errstr);
    return -1;
}
int write_to_pipe (const void* data, size_t len)
{
    DWORD written;
 
    if (!WriteFile (pipe, data, len, &written, NULL) || written != len) {
        return handle_write_pipe_error();
    }
    
    return 1;
}

typedef struct ControllerMsg {
    uint32_t version;
    uint32_t id;
    uint32_t result;
} ControllerMsg;

typedef struct _USBFILTERSET{
    ControllerMsg header;
    DWORD nCount;
    unsigned short vid;
    unsigned short pid;
}UsbFilterSet;

int send_usb_value(uint32_t id, unsigned short vid, unsigned short pid)
{
    UsbFilterSet usbFilterSet = {
        {
            CONTROLL_VERSION,
            id,
            TRUE,
        },
        1,
        vid,
        pid,
    };
    return write_to_pipe (&usbFilterSet, sizeof(usbFilterSet));
}

int send_value(uint32_t id, uint32_t value)
{
    ControllerMsg msg = {
      CONTROLL_VERSION,
      id,
      value
    };
    return write_to_pipe (&msg, sizeof(msg));
}
void handle_read_error(void){
    DWORD errval  = GetLastError();
    gchar *errstr = g_win32_error_message(errval);
    // return empty msg 
    if(errval != ERROR_PIPE_NOT_CONNECTED){
        g_warning("Read from pipe failed (%ld) %s\n", errval, errstr);
        g_free(errstr); 
    }
}
ssize_t read_from_pipe (void* data, size_t size)
{
    ssize_t read;
    DWORD bytes;
    if (!ReadFile (pipe, data, size, &bytes, NULL)) {
        handle_read_error();       
    }

    read = bytes;
    return read;
}

void handle_pipe_message(void)
{
    ControllerMsg msg;
    while ((read_from_pipe (&msg, sizeof(msg))) == sizeof(msg)) {
        switch(msg.id){
            case EVDI_USB_FILTER_SET_REPLY:
                g_message("set usb filter  \n");
                break;
            case EVDI_USB_FILTER_GET_REPLY:
                g_message("get usb filter  \n");
                break;
            default:
                g_message("default id  %d\n", msg.id);
                break;
        }
        break;
    }
    
}
/**
 * [send_and_read_from_pipe  use pipe
 * @param win   SpiceWindow
 * @param id    uint32_t
 * @param value uint32_t
 */
void send_and_read_from_pipe (uint32_t id, uint32_t value){

    int write_ret = send_value(id, value);
    if(write_ret > 0){
        handle_pipe_message();
    }
    
}

void send_usb_and_read_from_pipe(uint32_t id, unsigned short vid, unsigned short pid){

    int write_ret = send_usb_value(id, vid, pid);
    if(write_ret > 0){
        handle_pipe_message();
    }
    
}
#endif


static void send_cat(VirtViewerWindow *win){
    SpiceGrabSequence *seq = spice_grab_sequence_new_from_string("Control_L+Alt_L+Delete");
//    spice_display_send_keys(SPICE_DISPLAY(win->priv->display), seq->keysyms, seq->nkeysyms, SPICE_DISPLAY_KEY_EVENT_CLICK);
	virt_viewer_display_send_keys(VIRT_VIEWER_DISPLAY(win->priv->display),
                                  seq->keysyms, seq->nkeysyms);
}
static void menu_cb_sending_keys(GtkAction *action, void *data)
{
    // sending sending Control_L+Alt_L+Delete
    VirtViewerWindow *win = data;
    send_cat(win);
}

static SpiceSession * virt_viewer_window_getspice_session(VirtViewerWindow *self)
{
	SpiceSession *session = NULL;
	VirtViewerSession *vsession =NULL;

	g_object_get(self->priv->app, "session", &vsession, NULL);
	
	g_return_val_if_fail(vsession != NULL, NULL);

    g_object_get(vsession, "spice-session", &session, NULL);

    g_object_unref(vsession);

    return session;
}

static void pop_poweroff_window(gpointer data){
    char cmd[64];
    int ret = 0;

    VirtViewerWindow *win = data;
	VirtViewerWindowPrivate *priv = win->priv;
    GtkWidget *label, *area;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                    _("Forced Poweroff"),
                    GTK_WINDOW(priv->window),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    _("_OK"), GTK_RESPONSE_ACCEPT,       
                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                    NULL);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_MENU);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
    gtk_box_set_spacing(GTK_BOX(gtk_bin_get_child(GTK_BIN(dialog))), 20);
    gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    
    label = gtk_label_new(_("Will be forced to close the desktop, Confirm or not?"));  
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE, 0); 

    gtk_window_set_keep_above(GTK_WINDOW(priv->window), FALSE);
    /* show and run */
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));

	SpiceSession *session = virt_viewer_window_getspice_session(win);
	

		g_object_get(session,
						"port",&port,
						NULL);
		g_warning("port = %s",port);
		
    switch (result) {
        case GTK_RESPONSE_ACCEPT:
#ifndef G_OS_WIN32		

        sprintf(cmd, "/usr/bin/python /opt/pythonclient/bin/manager vm shutdown %s", port);
       ret = system(cmd);
        if(ret){
            g_message("shutdown successful");
        }
#else
        send_and_read_from_pipe(EVDI_POWEROFF, TRUE);
#endif
            break;

        default:
            break;
    }
    gtk_widget_destroy(dialog);
}

static void menu_cb_poweroff(GtkAction *action, void *data)
{
    pop_poweroff_window(data);
}

static void pop_close_window(gpointer data, int not_closed, int type){
    VirtViewerWindow *win = data;
	VirtViewerWindowPrivate *priv = win->priv;
    GtkWidget *label, *area;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                    type == 1 ? _("Close Window") : _("Close Client"),
                    GTK_WINDOW(priv->window),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    _("_OK"), GTK_RESPONSE_OK,
                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                    NULL);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_MENU);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
    gtk_box_set_spacing(GTK_BOX(gtk_bin_get_child(GTK_BIN(dialog))), 20);
    gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    
    label = type == 1 ? gtk_label_new(_("Do you want to close the window?")): 
                            gtk_label_new(_("Do you want to close the client?"));  
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE, 0); 

    gtk_window_set_keep_above(GTK_WINDOW(priv->window), FALSE);
    /* show and run */
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));

    switch (result) {
        case GTK_RESPONSE_OK:
#if defined(G_OS_WIN32)
        if(not_closed){
            send_and_read_from_pipe(EVDI_EXIT_PROGRAM, TRUE);
            g_message("sending exit to host example!");
        } 
#endif
            //connection_disconnect(win->conn);
            virt_viewer_app_maybe_quit(priv->app, win);
            break;
        default:
            break;
    }
    gtk_widget_destroy(dialog);
}


#ifdef USE_USBREDIR
static void remove_cb(GtkContainer *container, GtkWidget *widget, void *data)
{
    gtk_window_resize(GTK_WINDOW(data), 1, 1);
}
GList *control_usb_configuration(GList *rules_ret, int *count, int action){
    
    gchar **keys = NULL;
    gsize nkeys, i, j=0;
    GError *error = NULL;
    UsbredirDisplayRule *rule = NULL;
    GList *rules = NULL;


    keys = g_key_file_get_keys(keyfile, "usb", &nkeys, &error);
    if (error != NULL) {
        if (error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND)
            g_warning("Failed to read configuration file keys: %s", error->message);
        g_clear_error(&error);
        return NULL;
    }

    // at least three usb keys
    if (nkeys < 3 || nkeys % 3 != 0){
        g_warning("no keys found in usb");
        return NULL;
    }
        

    if(nkeys > 0)
        g_return_if_fail(keys != NULL);

    switch(action){
        case CONF_WRITE:
            // for(i = 0; i < *count; ++i){
            // }
            break;
        case CONF_READ:
            
            for (i = 0; i < nkeys / 3 ; i++) {
                if ( (g_str_equal(keys[i*3], "vender0") && g_str_equal(keys[i*3+1], "product0") && g_str_equal(keys[i*3+2], "desc0")) ||
                    (g_str_equal(keys[i*3], "vender1") && g_str_equal(keys[i*3+1], "product1") && g_str_equal(keys[i*3+2], "desc1")) ||
                    (g_str_equal(keys[i*3], "vender2") && g_str_equal(keys[i*3+1], "product2") && g_str_equal(keys[i*3+2], "desc2")) ||
                    (g_str_equal(keys[i*3], "vender3") && g_str_equal(keys[i*3+1], "product3") && g_str_equal(keys[i*3+2], "desc3"))){
                    rule = malloc(sizeof(UsbredirDisplayRule));
                    
                    rule->vender_id = g_key_file_get_string(keyfile, "usb", keys[i*3], &error);
                    rule->product_id = g_key_file_get_string(keyfile, "usb", keys[i*3 + 1], &error);
                    rule->desc = g_key_file_get_string(keyfile, "usb", keys[i*3 + 2], &error);
                    if (!rule->vender_id || rule->vender_id == NULL)
                        continue;

                    if (!rule->product_id || rule->product_id == NULL)
                        continue;

                    if (!rule->desc || rule->desc == NULL)
                        continue;

                    if (strlen(rule->vender_id) == 0 || 
                            strlen(rule->product_id) ==0 || strlen(rule->desc) == 0)
                        continue;
                    j++;
                    rule->index = (int)(keys[i*3][strlen(keys[i*3]) - 1] - '0');
                    usb_indexs[rule->index] = 1;
                    g_message("print read configure %s, %s, %s, %d", rule->vender_id, rule->product_id, rule->desc, rule->index);
                    rules = g_list_append(rules, rule);
                }
            }
            *count = j;
            break;
        default:
            break;
    }

    return rules;
}

typedef struct CheckDevice{
    SpiceUsbDevice *device;
    int index;
}CheckDevice;

static void auto_clicked_cb(GtkWidget *check, gpointer user_data){
    CheckDevice *check_device;
    gchar *desc;
	GError *err = NULL;
    char title_vid[10], title_pid[10], title_desc[10];
    char value_vid[10], value_pid[10], value_desc[256];
    check_device = g_object_get_data(G_OBJECT(check), "auto-usb-device");

    int vid = spice_usb_device_get_vid(check_device->device);
    int pid = spice_usb_device_get_pid(check_device->device);
    desc = spice_usb_device_get_description(check_device->device, NULL);

    if(check_device->index < 0 || check_device->index > 3){
        return;
    }
    snprintf(title_vid, sizeof(title_vid), "vender%d", check_device->index);
    snprintf(title_pid, sizeof(title_pid), "product%d", check_device->index);
    snprintf(title_desc, sizeof(title_desc), "desc%d", check_device->index);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check))){

        snprintf(value_vid, sizeof(value_vid), "%x", vid);
        snprintf(value_pid, sizeof(value_pid), "%x", pid);
        snprintf(value_desc, sizeof(value_desc), "%s", desc);
        g_message("click button and write to conf %s %s %d", value_vid, value_pid, check_device->index);
        g_key_file_set_string(keyfile, "usb", title_vid, value_vid);
        g_key_file_set_string(keyfile, "usb", title_pid, value_pid);
        g_key_file_set_string(keyfile, "usb", title_desc, desc);
    }
    else{
        g_message("click button and delete to conf %d", check_device->index);
        g_key_file_set_string(keyfile, "usb", title_vid, "");
        g_key_file_set_string(keyfile, "usb", title_pid, "");
        g_key_file_set_string(keyfile, "usb", title_desc, "");
    }
		//===write
				gchar *conf;
				if ((conf = g_key_file_to_data(keyfile, NULL, &err)) == NULL ||
								!g_file_set_contents(conf_file, conf, -1, &err)) {
								g_warning("Couldn't save configuration: %s", err->message);
								g_error_free(err);
								err = NULL;
						}
				g_message("conf = %s",conf);
				g_free(conf);

    return;
}
static int find_avaliable_index(void){
    int i;
    for(i=0 ; i < 4; i++){
        if(usb_indexs[i] == 0)
            return i;
    }
    return -1;
}
static void select_auto_usb_devices(VirtViewerWindow *win)
{
    GtkWidget *dialog, *area;
     /* Create the widgets */
    SpiceUsbDeviceManager *manager;
    GError *err = NULL;
    GPtrArray *devices = NULL;
    int i, j, index, auto_index=0,rules_count=0;
    gboolean can_redirect, is_auto_connect = FALSE;
    GtkWidget *align, *check, *label;
    gchar *desc;
    SpiceUsbDevice *device;
    CheckDevice *check_device = NULL;
    GList *rules= NULL;
    UsbredirDisplayRule *rule=NULL;

    dialog = gtk_dialog_new_with_buttons(
                    _("Configure USB device to enable redirection in boot"),
                    GTK_WINDOW(win->priv->window),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    _("_OK"), GTK_RESPONSE_ACCEPT,
                    NULL);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_MENU);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
    gtk_box_set_spacing(GTK_BOX(gtk_bin_get_child(GTK_BIN(dialog))), 12);

    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    label = gtk_label_new(_("Once checking, the device will connect to desktop automatically in next desktop boot."));  
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE , 1);   

    // read current usb list in file
    rules = control_usb_configuration(NULL, &rules_count, CONF_READ);
        
    // read current usb list in session
    manager = spice_usb_device_manager_get(virt_viewer_window_getspice_session(win), &err);
    if(err){
        g_message("%s", err->message);
        return;
    }
    devices = spice_usb_device_manager_get_devices(manager);

    
    for (i = 0; i < devices->len; i++){
        is_auto_connect = FALSE;
        device = g_ptr_array_index(devices, i);
        if(err)
            g_clear_error(&err);
        
        can_redirect = spice_usb_device_manager_can_redirect_device(manager, device, &err);

        if (!can_redirect) 
            continue;

        // find if in configure rules
        for(j = 0; j< rules_count; ++j){
            rule = (UsbredirDisplayRule *)g_list_nth_data(rules, j);
            if(!rule){
                continue;
            }
            int vid = (int)strtol(rule->vender_id, NULL, 16);
            int pid = (int)strtol(rule->product_id, NULL, 16);
            if(vid == spice_usb_device_get_vid(device) && 
                pid == spice_usb_device_get_pid(device)){
                is_auto_connect = TRUE;
                auto_index = rule->index;
                rules = g_list_remove(rules, rule);
                break;
            }
        }

        desc = spice_usb_device_get_description(device, NULL);
        check = gtk_check_button_new_with_label(desc);
        g_free(desc);

        align = gtk_alignment_new(0, 0, 0, 0);
        gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 12, 0);
        gtk_container_add(GTK_CONTAINER(align), check);
        gtk_box_pack_end(GTK_BOX(area), align, FALSE, FALSE, 0);
        check_device = malloc(sizeof(CheckDevice));
        check_device->device = device;
        
        if(is_auto_connect)
            check_device->index =  auto_index;
        else{
            index = find_avaliable_index();
            if(index>=0){
                check_device->index = index;
                usb_indexs[check_device->index] = 1;
            }
            else{
                check_device->index = -1;
            }
        }
        
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), is_auto_connect);
        g_message("is auto reconnct %d index %d rules count  %d", is_auto_connect, check_device->index, rules_count);
        g_object_set_data(
            G_OBJECT(check), "auto-usb-device", check_device);

        g_signal_connect(G_OBJECT(check), "clicked",
                     G_CALLBACK(auto_clicked_cb), check);
        restore_configuration(win);
					
        gtk_widget_show_all(check);
        
    }

    GList *l = NULL;
 
    for(l = rules;  l != NULL; l = l->next){

        rule = (UsbredirDisplayRule *)(l->data);
        if(!rule)
            continue;
        g_message("vender_id %s", rule->vender_id);
        g_message("product_id %s", rule->product_id);
        check = gtk_check_button_new_with_label(rule->desc);
        align = gtk_alignment_new(0, 0, 0, 0);
        gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 12, 0);
        
        gtk_container_add(GTK_CONTAINER(align), check);
        gtk_box_pack_end(GTK_BOX(area), align, FALSE, FALSE, 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(check), FALSE);

    }
    gtk_window_set_keep_above(GTK_WINDOW(win->priv->window), FALSE);
    /* show and run */
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_list_free(l);
    g_list_free(rules);
}

static void usb_connect_failed(GObject               *object,
                               SpiceUsbDevice        *device,
                               GError                *error,
                               gpointer               data)
{
    GtkWidget *dialog;

    if (error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED)
        return;

    dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_CLOSE,
                                    _("USB redirection error"));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "%s", error->message);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}


static void select_usb_devices(VirtViewerWindow *win)
{
    GtkWidget *dialog, *area, *usb_device_widget;
     /* Create the widgets */
    
    dialog = gtk_dialog_new_with_buttons(
                    _("Select USB devices for redirection"),
                    GTK_WINDOW(win->priv->window),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    _("_Close"), GTK_RESPONSE_ACCEPT,
                    NULL);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_MENU);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
    gtk_box_set_spacing(GTK_BOX(gtk_bin_get_child(GTK_BIN(dialog))), 12);

    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    usb_device_widget = spice_usb_device_widget_new(virt_viewer_window_getspice_session(win),
                                                    NULL); /* default format */

    g_signal_connect(usb_device_widget, "connect-failed",
                     G_CALLBACK(usb_connect_failed), NULL);

    gtk_box_pack_start(GTK_BOX(area), usb_device_widget, TRUE, TRUE, 0);

    /* This shrinks the dialog when USB devices are unplugged */
    g_signal_connect(usb_device_widget, "remove",
                     G_CALLBACK(remove_cb), dialog);

    gtk_window_set_keep_above(GTK_WINDOW(win->priv->window), FALSE);
    /* show and run */
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void menu_cb_select_usb_devices(GtkAction *action, void *data)
{
    VirtViewerWindow *win = data;
    select_usb_devices(win);
}

static void menu_cb_select_auto_usb_devices(GtkAction *action, void *data)
{
    VirtViewerWindow *win = data;
    select_auto_usb_devices(win);
}
#endif

typedef struct {
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *entry;
    GtkWidget *error_label;
    GtkWidget *label;
    VirtViewerWindow *win;
    int pop_action;
} PasswordWidgets;

static void check_password(GtkDialog *dialog, gint reponse_id, gpointer data){
    
     PasswordWidgets *passwdWidget = data;
     const gchar *passwd;
     if(reponse_id == GTK_RESPONSE_ACCEPT){
        gtk_label_set_text(GTK_LABEL(passwdWidget->error_label), "");
        passwd = gtk_entry_get_text(GTK_ENTRY(passwdWidget->entry));
        if(g_str_equal(passwd, "oe1234")){
            switch(passwdWidget->pop_action){
                case 1:
                    pop_close_window(passwdWidget->win, TRUE, 2);
                    break;
                case 0:
                    pop_poweroff_window(passwdWidget->win);
                    break;
                default:
                    break; 
            }
        }
        else{
            gtk_label_set_text(GTK_LABEL(passwdWidget->error_label), _("incorrect password!"));
            return;
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void ask_to_verify_to_password(VirtViewerWindow *widget, gpointer data, int pop_action){

    VirtViewerWindow *win = data;
	VirtViewerWindowPrivate *priv = win->priv;
    GtkWidget *area;
    PasswordWidgets *passwdWidget = g_new0(PasswordWidgets, 1);
    passwdWidget->win = win;
    passwdWidget->pop_action = pop_action;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                    _("Verification"),
                    GTK_WINDOW(priv->window),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    _("_OK"), GTK_RESPONSE_ACCEPT,       
                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                    NULL);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_MENU);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
    gtk_box_set_spacing(GTK_BOX(gtk_bin_get_child(GTK_BIN(dialog))), 20);
    gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    
    passwdWidget->error_label = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(passwdWidget->error_label), 0.0, 0.5);

    passwdWidget->label = gtk_label_new(_("Password:"));  
    gtk_misc_set_alignment(GTK_MISC(passwdWidget->label), 0.0, 0.5);

    passwdWidget->entry = gtk_entry_new();
    
    gtk_entry_set_visibility(GTK_ENTRY(passwdWidget->entry), FALSE);
#if GTK_CHECK_VERSION(3, 12, 0)
    gtk_entry_set_max_width_chars(GTK_ENTRY(passwdWidget->entry), 6);
#endif
    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

#if GTK_CHECK_VERSION(3,0,0)
    passwdWidget->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    passwdWidget->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
    passwdWidget->vbox = gtk_vbox_new(FALSE, 1);
    passwdWidget->hbox = gtk_hbox_new(FALSE, 1);
#endif

#if GTK_CHECK_VERSION(3,0,0)
    gtk_widget_set_halign(passwdWidget->label, GTK_ALIGN_START);
    gtk_widget_set_valign(passwdWidget->entry, GTK_ALIGN_CENTER);
#endif

#if GTK_CHECK_VERSION(3,10,0)
    gtk_widget_set_valign(passwdWidget->label, GTK_ALIGN_BASELINE);
#endif

    gtk_box_pack_start(GTK_BOX(passwdWidget->hbox), passwdWidget->label, TRUE, TRUE, 6); 
    gtk_box_pack_start(GTK_BOX(passwdWidget->hbox), passwdWidget->entry, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(passwdWidget->vbox),
                   passwdWidget->hbox, TRUE, TRUE, 6);
    gtk_box_pack_start(GTK_BOX(passwdWidget->vbox), passwdWidget->error_label, TRUE, TRUE, 6);


    gtk_box_pack_start(GTK_BOX(area),
                       passwdWidget->vbox, TRUE, TRUE, 6);

    g_signal_connect(dialog, "response",
                     G_CALLBACK(check_password), passwdWidget);

    gtk_window_set_keep_above(GTK_WINDOW(priv->window), FALSE);
    /* show and run */
    gtk_widget_show_all(dialog);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
}

static void ask_to_close_windows_with_password(VirtViewerWindow *widget,  gpointer data){
    if(passwd_is_needed)
        ask_to_verify_to_password(widget, data, 1);
    else {
        pop_close_window(data, TRUE, 2);
    }
}

static void ask_to_close_windows(VirtViewerWindow *widget,  gpointer data){
    pop_close_window(data, FALSE, 1);
}

GOptionGroup*
virt_viewer_window_get_option_group(void)
{
    static const GOptionEntry options [] = {
        { "toolbar", '\0', 0, G_OPTION_ARG_NONE, &enable_toolbar,
          N_("enable toolbar"), NULL },
        { "modeVM", '\0', 0, G_OPTION_ARG_NONE, &is_mode_vm,
          N_("is mode VM"), NULL },
        { "forceFullscreen", '\0', 0, G_OPTION_ARG_NONE, &complete_fullscreen,
          N_("Force in a fullscreen mode"), NULL },
        { "exitNeedPassword", '\0', 0, G_OPTION_ARG_NONE, &passwd_is_needed,
          N_("require password when exit"), NULL },
        { "title", '\0', 0, G_OPTION_ARG_STRING, &title,
          N_("Set the window title"), NULL },
        { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
    };
    GOptionGroup *group;
    group = g_option_group_new("virt-viewer", NULL, NULL, NULL, NULL);
    g_option_group_add_entries(group, options);

    return group;
}

static SpiceMainChannel*
get_main(VirtViewerDisplay *self)
{
    VirtViewerSessionSpice *session;

    session = VIRT_VIEWER_SESSION_SPICE(virt_viewer_display_get_session(self));

    return virt_viewer_session_spice_get_main_channel(session);
}

static const char *spice_gtk_session_properties[] = {
    "auto-clipboard",
    "auto-usbredir",
};



static gboolean is_gtk_session_property(const gchar *property)
{
    int i;

    for (i = 0; i < G_N_ELEMENTS(spice_gtk_session_properties); i++) {
        if (!strcmp(spice_gtk_session_properties[i], property)) {
            return TRUE;
        }
    }
    return FALSE;
}

static void restore_configuration(VirtViewerWindow *win)
{
    gboolean state;
    gchar *str;
    gchar **keys = NULL;
    gsize nkeys, i;
    GError *error = NULL;
    gpointer object;
    gint loop_time;

    keys = g_key_file_get_keys(keyfile, "general", &nkeys, &error);
    if (error != NULL) {
        if (error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND)
            g_warning("Failed to read configuration file keys: %s", error->message);
        g_clear_error(&error);
        return;
    }

    if (nkeys > 0)
        g_return_if_fail(keys != NULL);

    for (i = 0; i < nkeys; ++i) {
        if (g_str_equal(keys[i], "grab-sequence"))
            continue;
        state = g_key_file_get_boolean(keyfile, "general", keys[i], &error);
        if (error != NULL) {
            g_clear_error(&error);
            continue;
        }

        if (is_gtk_session_property(keys[i])) {
            object = virt_viewer_window_getspice_session(win);
        } else {
            object = win->priv->display;
        }
        g_object_set(object, keys[i], state, NULL);
    }

    g_strfreev(keys);

    str = g_key_file_get_string(keyfile, "general", "grab-sequence", &error);
    if (error == NULL) {
        SpiceGrabSequence *seq = spice_grab_sequence_new_from_string(str);
        spice_display_set_grab_keys(SPICE_DISPLAY(win->priv->display), seq);
        spice_grab_sequence_free(seq);
        g_free(str);
    }
    g_clear_error(&error);

    /*loop_time = g_key_file_get_integer(keyfile, "general", "loop_time", &error);
    if(error == NULL)
    {
        if(loop_time < 800){
            loop_time = 1000;
        }
        win->loop_time = loop_time;
    }*/

    g_clear_error(&error);
}

static void menu_cb_mouse_mode(GtkAction *action, void *data)
{
    VirtViewerWindow *win = data;
    SpiceMainChannel *cmain = get_main(win->priv->display);
    int mode;
    GtkWidget *label, *area;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                    _("Switch Mouse Mode"),
                    GTK_WINDOW(win->priv->window),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    _("_OK"), GTK_RESPONSE_ACCEPT,
                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                    NULL);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_MENU);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
    gtk_box_set_spacing(GTK_BOX(gtk_bin_get_child(GTK_BIN(dialog))), 20);
    gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    
    label = gtk_label_new(_("Mouse switches between server mode and client side mode, please do this when mouse is abnormal, you can switch with shortcut key (Shift+F11)."));  
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE, 0); 

    gtk_window_set_keep_above(GTK_WINDOW(win->priv->window), FALSE);
    
    /* show and run */
    gtk_widget_show_all(dialog);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));

    switch (result) {
        case GTK_RESPONSE_ACCEPT:
            
            g_object_get(cmain, "mouse-mode", &mode, NULL);
            if (mode == SPICE_MOUSE_MODE_CLIENT)
                mode = SPICE_MOUSE_MODE_SERVER;
            else
                mode = SPICE_MOUSE_MODE_CLIENT;

            spice_main_request_mouse_mode(cmain, mode);
            break;

        default:
            break;
    }
    gtk_widget_destroy(dialog);   
}


static void
virt_viewer_window_toolbar_setup(VirtViewerWindow *self)
{
    GtkWidget *button;
	GtkWidget *exitVDI;
	VirtViewerWindowPrivate *priv = self->priv;
    
    priv->toolbar = g_object_ref(gtk_toolbar_new());
    gtk_toolbar_set_show_arrow(GTK_TOOLBAR(priv->toolbar), FALSE);
    gtk_widget_set_no_show_all(priv->toolbar, TRUE);
    gtk_toolbar_set_style(GTK_TOOLBAR(priv->toolbar), GTK_TOOLBAR_BOTH_HORIZ);

    //Close connection
   /*   //button = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_CLOSE));
    //gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("Disconnect"));
    //gtk_widget_show(GTK_WIDGET(button));
    //gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM (button), 0);
    //g_signal_connect(button, "clicked", G_CALLBACK(virt_viewer_window_menu_file_quit), self);

    // USB Device selection
    button = gtk_image_new_from_icon_name("virt-viewer-usb",
                                          GTK_ICON_SIZE_INVALID);
    button = GTK_WIDGET(gtk_tool_button_new(button, NULL));
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(button), _("USB device selection"));
    gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("USB device selection"));
    gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM(button), 0);
    g_signal_connect(button, "clicked", G_CALLBACK(virt_viewer_window_menu_file_usb_device_selection), self);
    priv->toolbar_usb_device_selection = button;
    //gtk_widget_show_all(button);

    // Send key 
  button = GTK_WIDGET(gtk_tool_button_new(NULL, NULL));
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(button), "preferences-desktop-keyboard-shortcuts");
    gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("Send key combination"));
    gtk_widget_show(GTK_WIDGET(button));
    gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM(button), 0);
    g_signal_connect(button, "clicked", G_CALLBACK(virt_viewer_window_toolbar_send_key), self);
    gtk_widget_set_sensitive(button, FALSE);
    priv->toolbar_send_key = button;

    // Leave fullscreen 
    button = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_LEAVE_FULLSCREEN));
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(button), _("Leave fullscreen"));
    gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("Leave fullscreen"));
    gtk_tool_item_set_is_important(GTK_TOOL_ITEM(button), TRUE);
    gtk_widget_show(GTK_WIDGET(button));
    gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM(button), 0);
    g_signal_connect(button, "clicked", G_CALLBACK(virt_viewer_window_toolbar_leave_fullscreen), self);*/
			
	 if(complete_fullscreen){
        // TODO change icon
        button = gtk_image_new_from_icon_name("rclose-vdi",
                                              GTK_ICON_SIZE_INVALID);
        button = GTK_WIDGET(gtk_tool_button_new(button, NULL));
        gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("Close Client"));
    
    }
    else{
       button = gtk_image_new_from_icon_name("rwindow-vdi",
                                              GTK_ICON_SIZE_INVALID);
       button = GTK_WIDGET(gtk_tool_button_new(button, NULL));
       gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("Exit Fullscreen"));
    }

    gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM (button), 0);
    if(complete_fullscreen){
        g_signal_connect(button, "clicked", G_CALLBACK(ask_to_close_windows_with_password), self);
    }
    else{
        g_signal_connect(button, "clicked", G_CALLBACK(virt_viewer_window_toolbar_leave_fullscreen), self);
    }
    gtk_widget_show_all (GTK_WIDGET (button));

    button = GTK_WIDGET(gtk_separator_tool_item_new());
    gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM (button), 0);
    gtk_widget_show_all (GTK_WIDGET (button));

	
	if(!is_mode_vm){
					/* Close connection */
					exitVDI = gtk_image_new_from_icon_name("rexit-vdi-1",
																								GTK_ICON_SIZE_INVALID);
					
					exitVDI = GTK_WIDGET(gtk_tool_button_new(exitVDI, NULL));
					gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(exitVDI), _("Exit Desktop"));
					gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM (exitVDI), 0);
					gtk_widget_set_name(GTK_WIDGET(exitVDI), "exit-vdi");
					g_signal_connect(exitVDI, "clicked", G_CALLBACK(ask_to_close_windows), self);
					gtk_widget_set_sensitive(exitVDI, enable_toolbar);
					gtk_widget_show_all (GTK_WIDGET (exitVDI));
			}
			
			button = gtk_image_new_from_icon_name("rmouse-mode-1",
																						GTK_ICON_SIZE_INVALID);
			button = GTK_WIDGET(gtk_tool_button_new(button, NULL));
	
			gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (button), _("Switch Mouse Mode"));
			gtk_toolbar_insert (GTK_TOOLBAR (priv->toolbar), GTK_TOOL_ITEM (button), 0);
			g_signal_connect (button, "clicked", G_CALLBACK (menu_cb_mouse_mode), self);
			gtk_widget_set_sensitive(button, enable_toolbar);
			gtk_widget_show_all (GTK_WIDGET (button));
	
			/* Send key */
			button = gtk_image_new_from_icon_name("rsend-alt-1",
																						GTK_ICON_SIZE_INVALID);
			button = GTK_WIDGET(gtk_tool_button_new(button, NULL));
			
			gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("Send Ctrl+Alt+Delete"));
			gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM(button), 0);
     		g_signal_connect(button, "clicked", G_CALLBACK(menu_cb_sending_keys), self);
			gtk_widget_set_sensitive(button, enable_toolbar);
			gtk_widget_show_all(GTK_WIDGET(button));
	
			/* USB Device selection */
			 //USE_USBREDIR
#ifdef  USE_USBREDIR
			GtkWidget *menu;
			GtkWidget *usb_item;
			GtkWidget *usb_auto_item;
	
			menu = gtk_menu_new();
			gtk_widget_set_name(menu, "usb_menu");
	
			usb_item = gtk_menu_item_new_with_label(_("Select usb redirection"));
			usb_auto_item = gtk_menu_item_new_with_label(_("Configure automatic redirection"));
	
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), usb_item);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), usb_auto_item);
	
			g_signal_connect(usb_item, "activate", G_CALLBACK(menu_cb_select_usb_devices), self);
			
			g_signal_connect(usb_auto_item, "activate", G_CALLBACK(menu_cb_select_auto_usb_devices), self);
	
			button = gtk_image_new_from_icon_name("rusb-redir-1",
																						GTK_ICON_SIZE_INVALID);
			button = GTK_WIDGET(gtk_menu_tool_button_new(button, NULL));
	
			gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("USB"));
			gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM(button), 0);
			gtk_container_set_border_width(GTK_CONTAINER(menu), 0);
			gtk_widget_set_sensitive(button, enable_toolbar);
			gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(button), menu);
			gtk_widget_show_all(menu);
			gtk_widget_show_all(button);
#endif
			/* Poweroff function */
			button = gtk_image_new_from_icon_name("rpower-off-vdi-1",
																						GTK_ICON_SIZE_INVALID);
			button = GTK_WIDGET(gtk_tool_button_new(button, NULL));
			gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(button), _("Forced Poweroff"));
			
			gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM (button), 0);
			g_signal_connect(button, "clicked", G_CALLBACK(menu_cb_poweroff), self);
	
			gtk_widget_set_sensitive(button, enable_toolbar);
			gtk_widget_show_all (GTK_WIDGET (button));
	
	
			
			button = GTK_WIDGET(gtk_separator_tool_item_new());
			gtk_toolbar_insert(GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM (button), 0);
			gtk_widget_show_all (GTK_WIDGET (button));
	
			/* main text */
			button = gtk_image_new_from_icon_name("rPC-vdi-1",
																						GTK_ICON_SIZE_INVALID);
			button = GTK_WIDGET(gtk_tool_button_new(button, NULL));
			gtk_tool_button_set_label(GTK_TOOL_BUTTON(button), title);
			gtk_tool_item_set_is_important(GTK_TOOL_ITEM(button), TRUE);
			gtk_widget_show(GTK_WIDGET (button));
			gtk_toolbar_insert (GTK_TOOLBAR(priv->toolbar), GTK_TOOL_ITEM (button), 0);
			gtk_widget_set_sensitive(button, FALSE);
			gtk_widget_show_all (GTK_WIDGET (button));

    priv->layout = ViewAutoDrawer_New();

    ViewAutoDrawer_SetActive(VIEW_AUTODRAWER(priv->layout), FALSE);
    ViewOvBox_SetOver(VIEW_OV_BOX(priv->layout), priv->toolbar);
    ViewOvBox_SetUnder(VIEW_OV_BOX(priv->layout), GTK_WIDGET(priv->notebook));
    ViewAutoDrawer_SetOffset(VIEW_AUTODRAWER(priv->layout), -1);
    ViewAutoDrawer_SetFill(VIEW_AUTODRAWER(priv->layout), FALSE);
    ViewAutoDrawer_SetOverlapPixels(VIEW_AUTODRAWER(priv->layout), 1);
    ViewAutoDrawer_SetNoOverlapPixels(VIEW_AUTODRAWER(priv->layout), 0);
    gtk_widget_show(priv->layout);
}

VirtViewerNotebook*
virt_viewer_window_get_notebook (VirtViewerWindow *self)
{
    return VIRT_VIEWER_NOTEBOOK(self->priv->notebook);
}

GtkWindow*
virt_viewer_window_get_window (VirtViewerWindow *self)
{
    return GTK_WINDOW(self->priv->window);
}

static void
virt_viewer_window_pointer_grab(VirtViewerDisplay *display G_GNUC_UNUSED,
                                VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv = self->priv;

    priv->grabbed = TRUE;
    virt_viewer_window_update_title(self);
}

static void
virt_viewer_window_pointer_ungrab(VirtViewerDisplay *display G_GNUC_UNUSED,
                                  VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv = self->priv;

    priv->grabbed = FALSE;
    virt_viewer_window_update_title(self);
}

static void
virt_viewer_window_keyboard_grab(VirtViewerDisplay *display G_GNUC_UNUSED,
                                 VirtViewerWindow *self)
{
    virt_viewer_window_disable_modifiers(self);
}

static void
virt_viewer_window_keyboard_ungrab(VirtViewerDisplay *display G_GNUC_UNUSED,
                                   VirtViewerWindow *self)
{
    virt_viewer_window_enable_modifiers(self);
}

void
virt_viewer_window_update_title(VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv = self->priv;
    char *title;
    gchar *ungrab = NULL;

    if (priv->grabbed) {
        gchar *label;
        GtkAccelKey key = { 0 };

        if (virt_viewer_app_get_enable_accel(priv->app))
            gtk_accel_map_lookup_entry("<virt-viewer>/view/release-cursor", &key);

        if (key.accel_key || key.accel_mods) {
            g_debug("release-cursor accel key: key=%u, mods=%x, flags=%u", key.accel_key, key.accel_mods, key.accel_flags);
            label = gtk_accelerator_get_label(key.accel_key, key.accel_mods);
        } else {
            label = g_strdup(_("Shift_L+F11"));
        }

        ungrab = g_strdup_printf(_("(Press %s to release pointer)"), label);
        g_free(label);
    }

    if (!ungrab && !priv->subtitle)
        title = g_strdup(g_get_application_name());
    else
        /* translators:
         * This is "<ungrab (or empty)><space (or empty)><subtitle (or empty)> - <appname>"
         * Such as: "(Press Ctrl+Alt to release pointer) BigCorpTycoon MOTD - Virt Viewer"
         */
        title = g_strdup_printf(_("%s%s%s - %s"),
                                /* translators: <ungrab empty> */
                                ungrab ? ungrab : "",
                                /* translators: <space> */
                                ungrab && priv->subtitle ? _(" ") : "",
                                priv->subtitle,
                                g_get_application_name());

    gtk_window_set_title(GTK_WINDOW(priv->window), title);

    g_free(title);
    g_free(ungrab);
}

void
virt_viewer_window_set_usb_options_sensitive(VirtViewerWindow *self, gboolean sensitive)
{
    VirtViewerWindowPrivate *priv;
    GtkWidget *menu;

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));

    priv = self->priv;
    menu = GTK_WIDGET(gtk_builder_get_object(priv->builder, "menu-file-usb-device-selection"));
    gtk_widget_set_sensitive(menu, sensitive);
   // gtk_widget_set_visible(priv->toolbar_usb_device_selection, sensitive);
}

static void
display_show_hint(VirtViewerDisplay *display,
                  GParamSpec *pspec G_GNUC_UNUSED,
                  VirtViewerWindow *self)
{
    guint hint;

    g_object_get(display, "show-hint", &hint, NULL);

    hint = (hint & VIRT_VIEWER_DISPLAY_SHOW_HINT_READY);

    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(self->priv->builder, "menu-file-screenshot")), hint);
}
static gboolean
window_key_pressed (GtkWidget *widget G_GNUC_UNUSED,
                    GdkEvent  *event,
                    GtkWidget *display)
{
    gtk_widget_grab_focus(display);
    return gtk_widget_event(display, event);
}

void
virt_viewer_window_set_display(VirtViewerWindow *self, VirtViewerDisplay *display)
{
    VirtViewerWindowPrivate *priv;

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));
    g_return_if_fail(display == NULL || VIRT_VIEWER_IS_DISPLAY(display));

    priv = self->priv;
    if (priv->display) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(priv->notebook), 1);
        g_object_unref(priv->display);
        priv->display = NULL;
    }

    if (display != NULL) {
        priv->display = g_object_ref(display);

        virt_viewer_display_set_zoom_level(VIRT_VIEWER_DISPLAY(priv->display), priv->zoomlevel);
        virt_viewer_display_set_monitor(VIRT_VIEWER_DISPLAY(priv->display), priv->fullscreen_monitor);
        virt_viewer_display_set_fullscreen(VIRT_VIEWER_DISPLAY(priv->display), priv->fullscreen);

        gtk_widget_show_all(GTK_WIDGET(display));
        gtk_notebook_append_page(GTK_NOTEBOOK(priv->notebook), GTK_WIDGET(display), NULL);
        gtk_widget_realize(GTK_WIDGET(display));

        virt_viewer_signal_connect_object(priv->window, "key-press-event",
                                          G_CALLBACK(window_key_pressed), display, 0);

        /* switch back to non-display if not ready */
        if (!(virt_viewer_display_get_show_hint(display) &
              VIRT_VIEWER_DISPLAY_SHOW_HINT_READY))
            gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook), 0);

        virt_viewer_signal_connect_object(display, "display-pointer-grab",
                                          G_CALLBACK(virt_viewer_window_pointer_grab), self, 0);
        virt_viewer_signal_connect_object(display, "display-pointer-ungrab",
                                          G_CALLBACK(virt_viewer_window_pointer_ungrab), self, 0);
        virt_viewer_signal_connect_object(display, "display-keyboard-grab",
                                          G_CALLBACK(virt_viewer_window_keyboard_grab), self, 0);
        virt_viewer_signal_connect_object(display, "display-keyboard-ungrab",
                                          G_CALLBACK(virt_viewer_window_keyboard_ungrab), self, 0);
        virt_viewer_signal_connect_object(display, "display-desktop-resize",
                                          G_CALLBACK(virt_viewer_window_desktop_resize), self, 0);
        virt_viewer_signal_connect_object(display, "notify::show-hint",
                                          G_CALLBACK(display_show_hint), self, 0);

        display_show_hint(display, NULL, self);

        if (virt_viewer_display_get_enabled(display))
            virt_viewer_window_desktop_resize(display, self);

        gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(self->priv->builder, "menu-send")), TRUE);
        //gtk_widget_set_sensitive(self->priv->toolbar_send_key, TRUE);
    }
}

static void
virt_viewer_window_enable_kiosk(VirtViewerWindow *self)
{
    VirtViewerWindowPrivate *priv;

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));
    priv = self->priv;

    ViewOvBox_SetOver(VIEW_OV_BOX(priv->layout), gtk_drawing_area_new());
    ViewAutoDrawer_SetActive(VIEW_AUTODRAWER(priv->layout), FALSE);
    ViewAutoDrawer_SetOverlapPixels(VIEW_AUTODRAWER(priv->layout), 0);

    /* You probably also want X11 Option "DontVTSwitch" "true" */
    /* and perhaps more distro/desktop-specific options */
    virt_viewer_window_disable_modifiers(self);
}

void
virt_viewer_window_show(VirtViewerWindow *self)
{
    if (self->priv->display)
        virt_viewer_display_set_enabled(self->priv->display, TRUE);

    gtk_widget_show(self->priv->window);

    if (self->priv->desktop_resize_pending) {
        virt_viewer_window_resize(self, FALSE);
        self->priv->desktop_resize_pending = FALSE;
    }

    if (self->priv->kiosk)
        virt_viewer_window_enable_kiosk(self);

    if (self->priv->fullscreen)
        virt_viewer_window_move_to_monitor(self);
}

void
virt_viewer_window_hide(VirtViewerWindow *self)
{
    if (self->priv->kiosk) {
        g_warning("Can't hide windows in kiosk mode");
        return;
    }

    gtk_widget_hide(self->priv->window);

    if (self->priv->display) {
        VirtViewerDisplay *display = self->priv->display;
        virt_viewer_display_set_enabled(display, FALSE);
    }
}

void
virt_viewer_window_set_zoom_level(VirtViewerWindow *self, gint zoom_level)
{
    VirtViewerWindowPrivate *priv;

    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));
    priv = self->priv;

    if (zoom_level < 10)
        zoom_level = 10;
    if (zoom_level > 400)
        zoom_level = 400;
    priv->zoomlevel = zoom_level;

    if (!priv->display)
        return;

    virt_viewer_display_set_zoom_level(VIRT_VIEWER_DISPLAY(priv->display), priv->zoomlevel);

    virt_viewer_window_queue_resize(self);
}

gint virt_viewer_window_get_zoom_level(VirtViewerWindow *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_WINDOW(self), 100);
    return self->priv->zoomlevel;
}

GtkMenuItem*
virt_viewer_window_get_menu_displays(VirtViewerWindow *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_WINDOW(self), NULL);

    return GTK_MENU_ITEM(gtk_builder_get_object(self->priv->builder, "menu-displays"));
}

GtkBuilder*
virt_viewer_window_get_builder(VirtViewerWindow *self)
{
    g_return_val_if_fail(VIRT_VIEWER_IS_WINDOW(self), NULL);

    return self->priv->builder;
}

VirtViewerDisplay*
virt_viewer_window_get_display(VirtViewerWindow *self)
{
    g_return_val_if_fail(VIRT_VIEWER_WINDOW(self), FALSE);

    return self->priv->display;
}

void
virt_viewer_window_set_kiosk(VirtViewerWindow *self, gboolean enabled)
{
    g_return_if_fail(VIRT_VIEWER_IS_WINDOW(self));
    g_return_if_fail(enabled == !!enabled);

    if (self->priv->kiosk == enabled)
        return;

    self->priv->kiosk = enabled;

    if (enabled)
        virt_viewer_window_enable_kiosk(self);
    else
        g_debug("disabling kiosk not implemented yet");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  indent-tabs-mode: nil
 * End:
 */
