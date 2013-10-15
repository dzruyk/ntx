#include <gtk/gtk.h>
#include <glib.h>
#include <string.h>

#include "nvt.h"
#include "chn.h"


#define BUFFER_SIZE 1024

static const gchar* chn_echo_get_name ();
static void         chn_echo_finalize ();
static gboolean     chn_echo_connect ();
static void         chn_echo_disconnect ();
static gboolean     chn_echo_is_connected ();
static gsize        chn_echo_write (const void *buf, gsize len);
static gsize        chn_echo_prepend (const void *buf, gsize len);

static gboolean is_connected = FALSE;
static guchar   prepend[BUFFER_SIZE];
static gsize    prepend_len;

extern ChannelCallbacks channel_callbacks;
extern ChannelFuncs     channel_funcs;

gint
chn_echo_init ( )
{
  g_debug ("chn_echo_init");

  channel_funcs.connect = chn_echo_connect;
  channel_funcs.disconnect = chn_echo_disconnect;
  channel_funcs.finalize = chn_echo_finalize;
  channel_funcs.get_name = chn_echo_get_name;
  channel_funcs.is_connected = chn_echo_is_connected;
  channel_funcs.prepend = chn_echo_prepend;
  channel_funcs.write = chn_echo_write;

  return 0;
}

static const gchar*
chn_echo_get_name ()
{
  return "chn_echo";
}

static gboolean
chn_echo_is_connected ()
{
  return is_connected;
}

static void
chn_echo_finalize ()
{
  g_debug ("chn_echo_finalize");

  if (is_connected)
    is_connected = FALSE;
}

static gboolean
chn_echo_connect ()
{
  g_debug ("chn_echo_connect");

  is_connected = TRUE;

  return TRUE;
}

static void
chn_echo_disconnect ()
{
  g_debug ("chn_echo_disconnect");

  if (!is_connected)
    g_warning ("no connection exists");

  is_connected = FALSE;
}

static gsize
chn_echo_write (const void *buf, gsize len)
{
  guchar buffer[BUFFER_SIZE*2];
  gsize buffer_len, total;

  g_assert (buf != NULL);

  buffer_len = 0;
  total = 0;

  do {
      gsize n;

      buffer_len = 0;

      if (prepend_len > 0)
        {
          g_assert (sizeof(buffer) >= sizeof(prepend));
          memcpy (buffer, prepend, prepend_len);
          buffer_len += prepend_len;
          prepend_len = 0;
        }

      n = MIN(len, sizeof(buffer)-buffer_len);

      memcpy (buffer+buffer_len, buf, n);
      buffer_len += n;

      if (channel_callbacks.input != NULL)
        (*channel_callbacks.input) (buffer, buffer_len, channel_callbacks.user_data);

      len -= n;
      total += n;

    } while (len > 0);

  return total;
}

static gsize
chn_echo_prepend (const void *buf, gsize len)
{
  gsize n;

  g_assert (buf != NULL);

  n = MIN(sizeof(prepend)-prepend_len, len);

  if (n > 0)
    {
      memcpy (prepend+prepend_len, buf, n);
      prepend_len += n;
    }

  return n;
}

