#include <gtk/gtk.h>
#include <glib.h>

#include "nvt.h"
#include "chn.h"

#define TELNET_PORT_DEFAULT 23

/* The following enumeration lists supported telnet options.
 * See RFC857, RFC1073, RFC1091 for their corresponding descriptions.
 */
enum
{
  OPT_ECHO          = 1,  /* echo */
  OPT_TERMINAL_TYPE = 24, /* negotiate terminal type */
  OPT_NAWS	    = 31  /* negotiate about window size */
};

extern ChannelCallbacks channel_callbacks;
extern ChannelFuncs channel_funcs;

static struct ChannelTelnetSettings
{
  int port;
  gchar *host;
} settings;

static const gchar* chn_telnet_get_name ();
static void         chn_telnet_finalize ();
static gboolean     chn_telnet_connect ();
static void         chn_telnet_disconnect ();
static gboolean     chn_telnet_is_connected ();
static gsize        chn_telnet_write             (const void *buf, gsize len);
static gsize        chn_telnet_prepend           (const void *buf, gsize len);
static void         chn_telnet_connected_cb      (gpointer user_data);
static void         chn_telnet_subnegotiation_cb (gint          opcode,
						  const guchar *arg,
						  gint          len,
						  gpointer      user_data);
static void         chn_telnet_command_cb        (gint     command,
						  gint     opcode,
						  gpointer user_data);
static void         chn_telnet_input_bytes_cb    (guchar  *data,
					          gint     len,
					          gpointer user_data);
static void         chn_telnet_disconnect_cb     (const GError *err,
						  gpointer      user_data);
static void         chn_telnet_error_cb          (const GError *err,
						  gpointer      user_data);


gint
chn_telnet_init (const gchar *host, gint port)
{

  g_debug ("chn_telnet_init: host=%s port=%d", host, port);

  if (host != NULL)
    {
      if (settings.host != NULL)
	g_free (settings.host);
      settings.host = g_strdup (host);
    }
  else
    {
      if (settings.host != NULL)
	g_free (settings.host);
      settings.host = g_strdup("localhost");
     }

  if (port > 0)
    settings.port = port;
  else
    settings.port = TELNET_PORT_DEFAULT;

  NVT_CALLBACKS (input_bytes) = chn_telnet_input_bytes_cb;
  NVT_CALLBACKS (command) = chn_telnet_command_cb;
  NVT_CALLBACKS (subnegotiation) = chn_telnet_subnegotiation_cb;
  NVT_CALLBACKS (connected) = chn_telnet_connected_cb;
  NVT_CALLBACKS (disconnect) = chn_telnet_disconnect_cb;
  NVT_CALLBACKS (error) = chn_telnet_error_cb;
  NVT_CALLBACKS (user_data) = NULL;

  channel_funcs.connect = chn_telnet_connect;
  channel_funcs.disconnect = chn_telnet_disconnect;
  channel_funcs.finalize = chn_telnet_finalize;
  channel_funcs.get_name = chn_telnet_get_name;
  channel_funcs.is_connected = chn_telnet_is_connected;
  channel_funcs.prepend = chn_telnet_prepend;
  channel_funcs.write = chn_telnet_write;

  return 0;
}

static const gchar*
chn_telnet_get_name ()
{
  return "chn_telnet";
}

static gboolean
chn_telnet_is_connected ()
{
  return nvt_is_connected ();
}

static void
chn_telnet_finalize ()
{
  nvt_finalize ();
}

static gboolean
chn_telnet_connect ()
{
  return nvt_connect (settings.host, settings.port);
}

static void
chn_telnet_disconnect ()
{
  nvt_disconnect ();
}

static gsize
chn_telnet_write (const void *buf, gsize len)
{
  g_assert (buf != NULL);

  return nvt_write (buf, len);
}

static gsize
chn_telnet_prepend (const void *buf, gsize len)
{
  g_assert (buf != NULL);

  return nvt_prepend (buf, len);
}

static void
chn_telnet_connected_cb (gpointer user_data)
{
  g_debug ("chn_telnet_connected_cb");

  nvt_do (OPT_ECHO);
}

static void
chn_telnet_command_cb (gint cmd, gint opcode, gpointer user_data)
{
  gchar *cn;

  g_debug ("chn_telnet_command_cb");

  if (cmd == DO)
    cn = "do";
  else if (cmd == DONT)
    cn = "dont";
  else if (cmd == WILL)
    cn = "will";
  else if (cmd == WONT)
    cn = "wont";
  else
    cn = NULL;

  if (cn != NULL)
    g_debug ("<- %s opcode %d", cn, opcode);
  else
    g_debug ("<- %d opcode %d", cmd, opcode);

  if (cmd == DO)
    {
      if (opcode == OPT_ECHO)
	nvt_wont (OPT_ECHO);
      else if (opcode == OPT_TERMINAL_TYPE)
	nvt_will (OPT_TERMINAL_TYPE);
      else if (opcode == OPT_NAWS)
	{
	  guchar arg[] = { 0, 80, 0, 24 };

	  nvt_will (OPT_NAWS);
	  g_debug ("SB -> NAWS");
	  nvt_subneg (opcode, arg, 4);
	}
      else
	nvt_wont (opcode);
    }
  else if (cmd == WILL)
    nvt_dont (opcode);
}

static void
chn_telnet_subnegotiation_cb (gint opcode, const guchar *arg, gint len, gpointer user_data)
{
  g_debug ("chn_telnet: subneg opcode %d len %d ", opcode, len);

  if (opcode == OPT_TERMINAL_TYPE)
    {
      if (len >= 1 && arg[0] == 1)
	{
	  guchar arg[] = { '\0', 't', 'e', 'l', 'n', 'e', 't' };

	  g_debug ("SB -> SEND terminal type");

	  nvt_subneg (OPT_TERMINAL_TYPE, arg, sizeof (arg));
	}
    }
}

static void
chn_telnet_input_bytes_cb (guchar *data, gint len, gpointer user_data)
{
  g_assert (data != NULL && len > 0);

  if (channel_callbacks.input != NULL)
    (*channel_callbacks.input) (data, len, channel_callbacks.user_data);
}

void
chn_telnet_error_cb (const GError *error, gpointer user_data)
{

  if (channel_callbacks.error != NULL)
    (*channel_callbacks.error) (error, channel_callbacks.user_data);
}

void
chn_telnet_disconnect_cb (const GError *err, gpointer user_data)
{

  if (channel_callbacks.disconnect != NULL)
    (*channel_callbacks.disconnect) (err, channel_callbacks.user_data);
}

