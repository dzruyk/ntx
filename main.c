#include <gtk/gtk.h>
#include <glib/gprintf.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "console.h"
#include "chn.h"
#include "internal.h"

static void
input_cb (guchar *data, gsize len, gpointer user_data)
{
   client_do_input (data, len);
}

static void
error_cb (const GError *error, gpointer user_data)
{
  g_debug ("error_cb");

  if (error != NULL)
    g_fprintf (stderr, "error cause: %s\r\n", error->message);

  gtk_main_quit ();
}

static void
disconnect_cb (const GError *err, gpointer user_data)
{
  g_debug ("disconnect_cb");

  if (err != NULL)
    g_fprintf (stderr, "disconnect cause: %s\r\n", err->message);

  gtk_main_quit ();
}

int
main (int argc, char *argv[])
{
  ChannelCallbacks callbacks;

  g_type_init ();

  gui_init (&argc, &argv);

  client_init ();

  callbacks.input = input_cb;
  callbacks.disconnect = disconnect_cb;
  callbacks.error = error_cb;
  callbacks.user_data = NULL;

  chn_set_callbacks (&callbacks);

  if (g_strcmp0 (getenv ("ntx_channel"), "echo") == 0)
    chn_echo_init ();

#ifdef __unix__
  else if (g_strcmp0 (getenv ("ntx_channel"), "pty") == 0)
    chn_pty_init (argc > 1 ? argv[1] : "/bin/sh");
#endif

  else
    chn_telnet_init (argc > 1 ? argv[1] : "localhost", argc > 2 ? atoi (argv[2]) : 23);

  chn_connect ();

  gtk_main ();

  chn_disconnect ();
  chn_finalize ();

  client_deinit ();

  return 0;
}

