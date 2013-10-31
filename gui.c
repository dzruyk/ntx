#include <gdk/gdkkeysyms.h>
#define GETTEXT_PACKAGE "gtk20"
#include <glib/gi18n-lib.h>
#include <string.h>

#include "fontsel.h"
#include "console.h"
#include "internal.h"
#include "chn.h"

#ifndef UTF8_CHAR_LEN_MAX
#define UTF8_CHAR_LEN_MAX 6
#endif

#define MAXMSGBUF 128

GtkWidget *main_window;
GtkWidget *console;

static gboolean mouse_enabled = FALSE;
static gboolean keyboard_enabled = TRUE;

/* Console widget signal handler ids.
 */
static gulong console_button_press_id;
static gulong console_button_release_id;
static gulong console_motion_notify_id;
static gulong console_key_press_id;
static gulong console_text_pasted_id;
static gulong console_scroll_id;

extern gboolean key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data);
extern gboolean client_in_ios_mode ();

static gboolean
console_motion_notify_event_cb (GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  gchar buf[MAXMSGBUF+1];
  guint len;
  static gdouble prev_x = -1.0, prev_y = -1.0;
  gdouble x, y;

  g_assert (widget != NULL && IS_CONSOLE (widget));
  g_assert (event != NULL);

  g_assert (mouse_enabled == TRUE);

  len = 0;
  x = event->x;
  y = event->y;

  console_window_to_display_coords (CONSOLE (console), &x, &y);

  if (prev_x != x || prev_y != y)
    {
      len = snprintf (buf, MAXMSGBUF, "-13#%u#%u", (guint)x+1, (guint)y+1);
      prev_x = x;
      prev_y = y;
    }

  if (len > 0)
    {
      buf[len] = 0x1B;
      chn_write (buf, len+1);
    }

  return FALSE;
}

static gboolean
console_scroll_event_cb (GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
  g_assert (widget != NULL && IS_CONSOLE (widget));
  g_assert (event != NULL);

  switch (event->type)
    {
    case GDK_SCROLL:
      if (event->direction == GDK_SCROLL_UP)
        key_send_up ();
      else if (event->direction == GDK_SCROLL_DOWN)
        key_send_down ();
      break;

    default:
      break;
    }

  return FALSE;
}


//FIXME: move me to another file
static void
utf8_buffer_send (const gchar *s)
{
  gchar *p;

  g_assert (s != NULL);
  g_assert (g_utf8_validate (s, -1, NULL));

  p = s;

  while (*p != '\0')
    {
      gchar tmp[64];
      gint len;
      gunichar ch;

      ch = g_utf8_get_char (p);

      tmp[0] = '+';

      len = g_unichar_to_utf8 (ch, tmp + 1) + 1;
      tmp[len] = '\0';
      key_iconv_send (tmp, len);

      g_utf8_next_char (p);
    }
}

static gboolean
console_text_pasted_cb (GtkWidget *widget, const gchar *s, gpointer user_data)
{
  g_debug ("text-pasted event callback %s", s);

  if (client_in_telnet_mode ())
    {
      g_debug("text-pasted in telnet mode");
      chn_write (s, strlen(s));
    }
  else
    {
      g_debug("text-pasted in IOS mode");
      utf8_buffer_send (s);
    }
}

static gboolean
console_button_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  gchar buf[MAXMSGBUF+1];
  guint len;
  gdouble x, y;

  g_assert (widget != NULL && IS_CONSOLE (widget));
  g_assert (event != NULL);

  len = 0;
  x = event->x;
  y = event->y;

  console_window_to_display_coords (CONSOLE (console), &x, &y);

  switch (event->type)
    {
    case GDK_BUTTON_PRESS:
      if (event->button == 1)
        len = snprintf (buf, MAXMSGBUF, "-11#1#%u#%u", (guint)x+1, (guint)y+1);
      else if (event->button == 2)
        len = snprintf (buf, MAXMSGBUF, "-11#2#%u#%u", (guint)x+1, (guint)y+1);
      else if (event->button == 3)
        len = snprintf (buf, MAXMSGBUF, "-11#R#%u#%u", (guint)x+1, (guint)y+1);
      break;

    case GDK_2BUTTON_PRESS:
      len = snprintf (buf, MAXMSGBUF, "-12#%u#%u", (guint)x+1, (guint)y+1);
      break;

    case GDK_BUTTON_RELEASE:
      len = snprintf (buf, MAXMSGBUF, "-11#0#%u#%u", (guint)x+1, (guint)y+1);
      break;

    default:
      break;
    }

  if (len > 0)
    {
      buf[len] = 0x1B;
      chn_write (buf, len+1);
    }

  return FALSE;
}

static gboolean
console_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (IS_CONSOLE (widget), FALSE);

  if (client_in_telnet_mode())
    {
      gunichar uc;

      switch (event->keyval)
        {
        case GDK_Return:
        case GDK_KP_Enter:
            chn_write ("\r\n", 2);
          break;

        case GDK_BackSpace:
          chn_write ("\b", 1);
          break;

        case GDK_Tab:
          chn_write ("\t", 1);
          break;

        default:
          uc = gdk_keyval_to_unicode (event->keyval);
          if (uc > 0)
            {
              gchar buf[UTF8_CHAR_LEN_MAX];
              gint len;

              len = g_unichar_to_utf8 (uc, buf);
              g_assert (len > 0);
              chn_write (buf, len);
            }
          break;
        }
    }
  else
    key_send (event);

  return FALSE;
}

/* This callback is called when the console widget size changes.
 */
void
console_size_allocate_cb (GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
  gchar buf[MAXMSGBUF+1];
  guint len;

  g_assert (IS_CONSOLE (widget));
  g_assert (allocation != NULL);

  /* `Screen size changed' sequence is sent only when in non-TELNET mode.
   */

  if (!client_in_telnet_mode())
    {
      buf[0] = '-';
      buf[1] = '9';
      buf[2] = 0x1B;
      len = 3;

      chn_write (buf, len);
    }
}

void
gui_keyboard_enable ()
{
  if (keyboard_enabled)
    return;

  g_signal_handler_unblock (G_OBJECT (console), console_key_press_id);

  keyboard_enabled = TRUE;
}

void
gui_keyboard_disable ()
{
  if (!keyboard_enabled)
    return;

  g_signal_handler_block (G_OBJECT (console), console_key_press_id);

  keyboard_enabled = FALSE;
}

void
gui_mouse_enable ()
{
  if (mouse_enabled)
    return;

  g_signal_handler_unblock (G_OBJECT (console), console_button_press_id);
  g_signal_handler_unblock (G_OBJECT (console), console_button_release_id);
  g_signal_handler_unblock (G_OBJECT (console), console_motion_notify_id);
  g_signal_handler_unblock (G_OBJECT (console), console_scroll_id);

  mouse_enabled = TRUE;
}

void
gui_mouse_disable ()
{
  if (!mouse_enabled)
    return;

  g_signal_handler_block (G_OBJECT (console), console_button_press_id);
  g_signal_handler_block (G_OBJECT (console), console_button_release_id);
  g_signal_handler_block (G_OBJECT (console), console_motion_notify_id);
  g_signal_handler_block (G_OBJECT (console), console_scroll_id);

  mouse_enabled = FALSE;
}

void
gui_init (gint *argc, gchar ***argv)
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *status_bar;
  //GtkWidget *menu_bar;

  gtk_set_locale ();

  gtk_init (argc, argv);

  //gdk_window_set_debug_updates (TRUE);

  g_log_set_fatal_mask ("Gdk", G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  main_window = window;
  gtk_window_set_title (GTK_WINDOW (window), "console");
  gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
  //gtk_window_set_default_size (GTK_WINDOW (window), 600, 400);
  gtk_window_set_resizable (GTK_WINDOW (window), TRUE);
  gtk_window_set_title (GTK_WINDOW (window), "ntx");

  g_signal_connect (G_OBJECT(window), "destroy", G_CALLBACK (gtk_main_quit), NULL);

  //console = console_new ();
  console = console_new_with_size (80, 25);

  //g_signal_connect (GTK_WIDGET (console), "key-press-event", G_CALLBACK (console_key_press_event2), NULL);
  //g_signal_connect (GTK_WIDGET (console), "motion-notify-event", G_CALLBACK (console_pointer_motion_event2), NULL);

  console_set_cursor_timer (CONSOLE (console), CONSOLE_BLINK_MEDIUM);

  g_signal_connect (GTK_WIDGET (console), "size-allocate", G_CALLBACK (console_size_allocate_cb), NULL);

  console_key_press_id =
    g_signal_connect (GTK_WIDGET (console), "key-press-event", G_CALLBACK (console_key_press_event_cb), NULL);
  console_motion_notify_id =
    g_signal_connect (GTK_WIDGET (console), "motion-notify-event", G_CALLBACK (console_motion_notify_event_cb), NULL);
  console_button_press_id =
    g_signal_connect (GTK_WIDGET (console), "button-press-event", G_CALLBACK (console_button_event_cb), NULL);
  console_button_release_id =
    g_signal_connect (GTK_WIDGET (console), "button-release-event", G_CALLBACK (console_button_event_cb), NULL);
  console_scroll_id =
    g_signal_connect (GTK_WIDGET (console), "scroll-event", G_CALLBACK (console_scroll_event_cb), NULL);
  console_text_pasted_id = 
    g_signal_connect (GTK_WIDGET (console), "text-pasted", G_CALLBACK (console_text_pasted_cb), NULL);

  g_signal_handler_block (G_OBJECT (console), console_motion_notify_id);
  g_signal_handler_block (G_OBJECT (console), console_button_press_id);
  g_signal_handler_block (G_OBJECT (console), console_scroll_id);

  vbox = gtk_vbox_new (FALSE, 0);

  status_bar = gtk_statusbar_new ();

//  menu_bar = gtk_menu_bar_new ();

//  gtk_box_pack_start (GTK_BOX (vbox), menu_bar, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), console, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), status_bar, FALSE, FALSE, 0);

  gtk_container_add (GTK_CONTAINER (window), vbox);

  gtk_widget_show_all (window);
}

