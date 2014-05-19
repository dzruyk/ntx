#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include "nvt.h"
#include "os.h"


#define DEFAULT_TIMEOUT 10

enum
{
  CR    = 0x0d,
  LF    = 0x0a
};

enum
{
  STATE_0,
  STATE_IAC,
  STATE_OPT,
  STATE_SB,
  STATE_SB2,
  STATE_IAC2
};

#define MAXREADBUF       1024
#define MAXWRITEBUF      1024
#define SUBNEGBUF        128


static GSocketConnection *connection;
static GSocketClient     *client;
static GIOChannel        *channel;
static gint              source_id;
static NvtCallbacks      callbacks;
static guchar            subnegbuf[SUBNEGBUF]; /* subnegotiation buffer */
static guint             subneglen = 0;        /* bytes in subnegotiation buffer */
static gint              state = STATE_0;      /* telnet protocol FSM state */
static gint              command;              /* telnet command, one of WILL, WONT, DO, DONT */
static guchar            prepbuf[MAXREADBUF];  /* read prepend buffer */
static gint              preplen = 0;          /* number of bytes in prepend buffer*/
static gboolean          crflag = 0;           /* carriage return recieved in STATE_0 */

static void nvt_real_disconnect ();

static void
debug (const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);

  fprintf (stderr, "debug: ");
  vfprintf (stderr, fmt, ap);
  fprintf (stderr, "\r\n");

  va_end (ap);
}

NvtCallbacks*
nvt_callbacks (void)
{
  return &callbacks;
}

void
nvt_subneg (gint cmd, guchar *arg, gsize len)
{
  GIOStatus status;
  GError *err;
  guchar buf[MAXWRITEBUF];
  gsize n, written;

  len = MIN (len, MAXWRITEBUF-5);

  buf[0] = IAC;
  buf[1] = SB;
  buf[2] = cmd;
  n = 3;

  memcpy (buf + n, arg, len);
  n += len;

  buf[n] = IAC;
  buf[n+1] = SE;
  n += 2;

  err = NULL;
  status = g_io_channel_write_chars (channel, (gchar *)buf, n, &written, &err);

  g_assert ((status != G_IO_STATUS_ERROR && err == NULL) || (status == G_IO_STATUS_ERROR && err != NULL));

  if (err != NULL)
    {
      if (callbacks.error != NULL)
        callbacks.error (err, callbacks.user_data);
      g_error_free (err);
    }
}

static void
nvt_cmd (gint cmd, gint opcode)
{
  GIOStatus status;
  GError *err;
  guchar buf[3];
  gsize len, written;

  buf[0] = IAC;
  buf[1] = cmd;
  len = 2;

  if (opcode >= 0)
    {
      buf[2] = opcode;
      len += 1;
    }

  err = NULL;
  status = g_io_channel_write_chars (channel, (gchar *)buf, len, &written, &err);

  g_assert ((status != G_IO_STATUS_ERROR && err == NULL) || (status == G_IO_STATUS_ERROR && err != NULL));

  if (err != NULL)
    {
      if (callbacks.error != NULL)
        callbacks.error (err, callbacks.user_data);
      g_error_free (err);
    }
}

void
nvt_will (gint opcode)
{
  debug ("-> will %d", opcode);

  nvt_cmd (WILL, opcode);
}

void
nvt_wont (gint opcode)
{
  debug ("-> wont %d", opcode);

  nvt_cmd (WONT, opcode);
}

void
nvt_do (gint opcode)
{
  debug ("-> do %d", opcode);

  nvt_cmd (DO, opcode);
}

void
nvt_dont (gint opcode)
{
  debug ("-> dont %d", opcode);

  nvt_cmd (DONT, opcode);
}

static gboolean
nvt_read (GIOChannel *channel, GIOCondition cond, gpointer user_data)
{
  unsigned char buf[2*MAXREADBUF];
  GError *err = NULL;
  GIOStatus status;
  gsize i, len, n;

  switch (cond)
    {
    case G_IO_IN:

      if (preplen > 0)
        {
          g_assert (preplen <= sizeof (buf));
          memcpy (buf, prepbuf, preplen);
          status = g_io_channel_read_chars (channel, (gchar *)buf+preplen, sizeof (buf)-preplen, &len, &err);
          preplen = 0;
        }
      else
        status = g_io_channel_read_chars (channel, (gchar *)buf, sizeof(buf), &len, &err);

      switch (status)
        {
        case G_IO_STATUS_NORMAL:

          //g_debug ("read %d bytes", len);

          n = 0;

          for (i = 0; i < len; i++)
            {
              guchar c;

              c = buf[i];

              switch (state)
                {
                case STATE_0:
                  if (c == IAC)
                    {
                      state = STATE_IAC;
                    }
                  else
                    {
                      if (!crflag)
                        if (c == CR)
                          crflag = TRUE;
                        else
                          {
                            buf[n] = c;
                            n++;
                          }
                      else
                        {
                          if (c == LF)
                            {
                              buf[n++] = CR;
                              buf[n++] = LF;
                            }
                          else if (c != 0)
                            {
                              buf[n++] = CR;
                              buf[n++] = c;
                            }
                          else
                            {
                              buf[n] = CR;
                              n++;
                            }
                          crflag = 0;
                        }
                    }
                  break;

                case STATE_IAC:
                  switch (c)
                    {
                    case IAC:
                      state = STATE_0;
                      buf[n] = IAC;
                      n++;
                      break;

                    case SB:
                      state = STATE_SB;
                      break;

                    case WILL:
                    case WONT:
                    case DO:
                    case DONT:
                      state = STATE_OPT;
                      command = c;
                      break;

                    default:
                      if (callbacks.command != NULL)
                        (*callbacks.command) (c, -1, callbacks.user_data);
                      state = STATE_0;
                      break;
                    }
                  break;

                case STATE_OPT:
                  if (callbacks.command != NULL)
                    (*callbacks.command) (command, c, callbacks.user_data);
                  else
                    {
                      switch (command)
                        {
                        case DO:
                          nvt_wont (c);
                          break;
                        case WILL:
                          nvt_dont (c);
                          break;
                        }
                    }

                  command = 0;
                  state = STATE_0;
                  break;
                
                case STATE_SB:
                  command = c;
                  subneglen = 0;
                  state = STATE_SB2;
                  break;

                case STATE_SB2:
                  if (c == IAC)
                    state = STATE_IAC2;
                  else
                    {
                      if (subneglen < SUBNEGBUF)
                        {
                          subnegbuf[subneglen] = c;
                          subneglen++;
                        }
                    }
                  break;
                
                case STATE_IAC2: /* ignore codes after IAC in subnegotiation, except IAC and SE */
                  if (c == IAC)
                    {
                      if (subneglen < SUBNEGBUF)
                        {
                          subnegbuf[subneglen] = IAC;
                          subneglen++;
                        }
                      state = STATE_SB2;
                    }
                  else if (c == SE)
                    {
                      if (callbacks.subnegotiation != NULL)
                        (*callbacks.subnegotiation) (command, subnegbuf, subneglen, callbacks.user_data);
                      state = STATE_0;
                    }
                  break;

                default:
                  g_warn_if_reached ();
                }
            }

          if (n > 0)
            {
              if (callbacks.input_bytes != NULL)
                (*callbacks.input_bytes) (buf, n, callbacks.user_data);
            }

          break;

        case G_IO_STATUS_ERROR:
          if (callbacks.error != NULL)
            (*callbacks.error) (err, callbacks.user_data);
          nvt_real_disconnect ();
          break;

        case G_IO_STATUS_EOF:
          if (callbacks.disconnect != NULL)
            (*callbacks.disconnect) (NULL, callbacks.user_data);
          nvt_real_disconnect ();
          break;

        case G_IO_STATUS_AGAIN:
          break;
        }
      break;

    default:
      g_warn_if_reached ();
    }

  return TRUE;
}

static void
nvt_ready (GObject *object, GAsyncResult *res, gpointer user_data)
{
  GSocketClient *aclient;
  GSocket *sock;
  GError *err;

  g_return_if_fail (object != NULL && res != NULL);
  g_return_if_fail (G_IS_SOCKET_CLIENT (object));

  aclient = G_SOCKET_CLIENT (object);
  g_assert (aclient == client);

  err = NULL;

  connection = g_socket_client_connect_finish (client, res, &err);

  g_assert ((connection != NULL && err == NULL) || (connection == NULL && err != NULL));

  if (err != NULL)
    {
      g_assert (connection == NULL);
      if (callbacks.error != NULL)
        (callbacks.error) (err, callbacks.user_data);
      g_error_free (err);
      return;
    }

  sock = g_socket_connection_get_socket (connection);
  g_assert (sock != NULL);
  g_socket_set_blocking (sock, FALSE);

  channel = os_g_io_channel_sock_new (g_socket_get_fd (sock));

  g_assert (channel != NULL);
  g_io_channel_set_encoding (channel, NULL, NULL);
  g_io_channel_set_buffered (channel, FALSE);
  g_io_channel_set_close_on_unref (channel, FALSE);

  g_assert (source_id == 0);
  source_id = g_io_add_watch (channel, G_IO_IN, (GIOFunc) nvt_read, NULL);
  g_assert (source_id > 0);

  if (callbacks.connected != NULL)
    (callbacks.connected) (callbacks.user_data);
}

gsize
nvt_write (const void *buf, gsize len)
{
  guchar out[MAXWRITEBUF*2];
  const guchar *c;
  gsize total;

  total = 0;
  c = buf;

  if (channel == NULL)
    return 0;

  while (len > 0)
    {
      gsize n, i, count;
      gsize written;
      GIOStatus status;
      GError *err;

      n = MIN (MAXWRITEBUF, len);
      count = 0;

      for (i = 0; i < n; i++)
        {
          if (c[i] == IAC)
            out[count++] = IAC;
          out[count++] = c[i];
        }

      err = NULL;
      status = g_io_channel_write_chars (channel, (gchar *)out, count, &written, &err);

      g_assert ((status != G_IO_STATUS_ERROR && err == NULL) || (status == G_IO_STATUS_ERROR && err != NULL));
        
      if (err != NULL)
        {
          if (status != G_IO_STATUS_AGAIN)
            {
              if (callbacks.error != NULL)
                callbacks.error (err, callbacks.user_data);
            }
          else
            {
              /* It is difficult to calculate exactly how many user bytes were
               * written when status is G_IO_STATUS_AGAIN and written > 0, but
               * it is better to count them then not to count at all.
               */
              total += written;
            }
          g_error_free (err);
          break;
        }

      total += n;
      c += n;
      len -= n;
    }

  return total;
}

void
nvt_init ()
{
  if (client == NULL)
    {
      client = g_socket_client_new ();
      g_socket_client_set_family (client, G_SOCKET_FAMILY_IPV4);
      g_socket_client_set_socket_type (client, G_SOCKET_TYPE_STREAM);
      g_socket_client_set_protocol (client, G_SOCKET_PROTOCOL_TCP);
#ifdef g_socket_client_set_timeout
      g_socket_client_set_timeout (client, DEFAULT_TIMEOUT);
#endif
      state = STATE_0;
      subneglen = 0;
    }
  else
    g_warning ("nvt_init: attempt to init twice");
}

static void
nvt_real_disconnect ()
{
  GError *err;

  /* Remove socket file descriptor from the event loop.
   */
  if (source_id > 0)
    {
      g_source_remove (source_id);
      source_id = 0;
    }

  /* Close the IO Stream, this will close socket file too.
   */
  if (connection != NULL)
    {
      err = NULL;
      g_io_stream_close (G_IO_STREAM (connection), NULL, &err);
      if (err != NULL)
        {
          fprintf (stderr, "nvt_real_disconnect: g_io_stream_close %s\n", err->message);
          g_error_free (err);
        }
      g_object_unref (connection);
      connection = NULL;
    }

  if (channel != NULL)
    {
      g_io_channel_unref (channel);
      channel = NULL;
    }
}

void
nvt_finalize ()
{

  nvt_real_disconnect ();

  if (client != NULL)
    {
      g_object_unref (client);
      client = NULL;
    }
}

gboolean
nvt_connect (const gchar *host, gshort port)
{
  GSocketConnectable *address;

  if (host == NULL || port < 0)
    return FALSE;

  if (client == NULL)
    nvt_init ();

  g_assert (client != NULL);

  address = g_network_address_new (host, port);
  g_assert (address != NULL);
  g_socket_client_connect_async (client, address, NULL, nvt_ready, NULL);
  g_object_unref (address);

  return TRUE;
}

gboolean
nvt_is_connected ()
{
  g_assert (client != NULL);

  if (connection != NULL)
    return TRUE;
  else
    return FALSE;
}

void
nvt_disconnect ()
{
  nvt_real_disconnect ();
}

gsize
nvt_prepend (const void *buf, gsize len)
{
  gint n;

  n = MIN(len, sizeof (prepbuf)-preplen);

  if (n > 0)
    {
      memcpy (prepbuf+preplen, buf, n);
      preplen += n;
    }

  return n;
}

