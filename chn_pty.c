#include <sys/resource.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib.h>

#include "nvt.h"
#include "chn.h"


#define BUFFER_SIZE 1024

static const gchar* chn_pty_get_name ();
static void         chn_pty_finalize ();
static gboolean     chn_pty_connect ();
static void         chn_pty_disconnect ();
static gboolean     chn_pty_is_connected ();
static gsize        chn_pty_write (const void *buf, gsize len);
static gsize        chn_pty_prepend (const void *buf, gsize len);

static GIOChannel *channel;              /* pty I/O channel */
static guint       source_id = 0;        /* pty watch event source id */
static guchar      prepend[BUFFER_SIZE]; /* prepend buffer */
static gsize       prepend_len;          /* length of the prepend buffer */
static guint       child_pid = -1;       /* PID of the slave process */

extern ChannelCallbacks channel_callbacks;
extern ChannelFuncs     channel_funcs;

static struct {
  gchar *cmdline;
} options;

gint
chn_pty_init (const gchar *cmdline)
{
  g_debug ("chn_pty_init: cmdline='%s'", cmdline);

  if (cmdline == NULL)
    return -1;

  if (options.cmdline != NULL)
    g_free (options.cmdline);
  options.cmdline = g_strdup (cmdline);

  channel_funcs.connect = chn_pty_connect;
  channel_funcs.disconnect = chn_pty_disconnect;
  channel_funcs.finalize = chn_pty_finalize;
  channel_funcs.get_name = chn_pty_get_name;
  channel_funcs.is_connected = chn_pty_is_connected;
  channel_funcs.prepend = chn_pty_prepend;
  channel_funcs.write = chn_pty_write;

  return 0;
}

static const gchar*
chn_pty_get_name ()
{
  return "chn_pty";
}

static gboolean
chn_pty_is_connected ()
{
  g_assert ((channel != NULL && source_id > 0) || (channel == NULL && source_id == 0));

  if (channel != NULL)
    return TRUE;
  else
    return FALSE;
}

static void
chn_pty_finalize ()
{
  g_debug ("chn_pty_finalize");

  if (chn_pty_is_connected ())
    chn_pty_disconnect ();

  if (options.cmdline != NULL)
    {
      g_free (options.cmdline);
      options.cmdline = NULL;
    }
}

static gboolean
chn_pty_read_event (GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
  guchar buffer[BUFFER_SIZE];
  GError *err = NULL;
  GIOStatus status;
  gsize len;

  switch (condition)
    {
    case G_IO_IN:
      if (prepend_len > 0)
        {
          g_assert (prepend_len <= sizeof (buffer));
          memcpy (buffer, prepend, prepend_len);
          status = g_io_channel_read_chars (channel, (gchar *)buffer+prepend_len, sizeof (buffer)-prepend_len, &len, &err);
          prepend_len = 0;
        }
      else
        status = g_io_channel_read_chars (channel, (gchar *)buffer, sizeof (buffer), &len, &err);

      switch (status)
        {
        case G_IO_STATUS_NORMAL:
          if (channel_callbacks.input != NULL)
            (*channel_callbacks.input)(buffer, len, channel_callbacks.user_data);
          break;

        case G_IO_STATUS_EOF:
          if (channel_callbacks.disconnect != NULL)
            (*channel_callbacks.disconnect) (NULL, channel_callbacks.user_data);
          chn_pty_disconnect ();
          break;

        case G_IO_STATUS_ERROR:
          if (channel_callbacks.error != NULL)
            (*channel_callbacks.error) (err, channel_callbacks.user_data);
          chn_pty_disconnect ();
          break;

        case G_IO_STATUS_AGAIN:
          break;

        default:
          g_warn_if_reached ();
        }

      break;

    default:
      break;
    }

  return TRUE;
}

static gboolean
chn_pty_connect ()
{
  struct rlimit rlim;
  gchar *ptsfile;
  gint ptyfd, ptsfd, rc;
  pid_t pid;

  g_debug ("chn_pty_connect");

  g_assert (options.cmdline != NULL && child_pid == -1);

  if ((ptyfd = posix_openpt (O_RDWR | O_NOCTTY)) == -1)
    {
      g_debug ("chn_pty_connect: can't open /dev/ptmx: %s", strerror (errno));
      goto err_openpt;
    }

  if (grantpt (ptyfd) == -1 || unlockpt (ptyfd) == -1 || (ptsfile = ptsname (ptyfd)) == NULL)
    {
      g_debug ("chn_pty_connect: can't grant/unlock/ptsname: %s", strerror (errno));
      goto err_pty;
    }

  if ((ptsfd = open (ptsfile, O_RDWR | O_NOCTTY)) == -1)
    {
      g_debug ("chn_pty_connect: can't open pts `%s': %s", ptsfile, strerror (errno));
      goto err_ptsfd;
    }

  g_debug ("chn_pty_connect: ptsfile=`%s'", ptsfile);

  pid = fork ();

  if (pid == -1)
    goto err_fork;

  if (pid == 0)
    {
      gint fd, nofile;

      /* Attach pts to child's standard input, output and error files.
       */
      for (fd = 0; fd < 3; fd++)
        {
          if (ptsfd == fd)
            continue;
          close (fd);
          dup (ptsfd);
        }

      if (ptsfd >= 3)
        close (ptsfd);

      /* Close all the other file descriptors before exec.
       */
      rc = getrlimit (RLIMIT_NOFILE, &rlim);
      if (rc == -1)
        nofile = 1024;
      else
        nofile = rlim.rlim_max;

      for (fd = 3; fd < nofile; fd++)
        close (fd);

      execl ("/bin/sh", "sh", "-c", options.cmdline, NULL);

      _exit (1);
    }
  else
    {
      g_assert (channel == NULL);
      channel = g_io_channel_unix_new (ptyfd);
      g_assert (channel != NULL);

      g_io_channel_set_encoding (channel, NULL, NULL);
      g_io_channel_set_buffered (channel, FALSE);
      g_io_channel_set_close_on_unref (channel, TRUE);
      g_io_channel_set_flags (channel, G_IO_FLAG_NONBLOCK, NULL);

      g_assert (source_id == 0);
      source_id = g_io_add_watch (channel, G_IO_IN, chn_pty_read_event, NULL);
      g_assert (source_id > 0);

      rc = close (ptsfd);
      g_assert (rc == 0);

      child_pid = pid;
    }

  return TRUE;

err_fork:
  close (ptsfd);
err_ptsfd:
err_pty:
  close (ptyfd);
err_openpt:
  return FALSE;
}

static void
chn_pty_disconnect ()
{
  gint rc;

  g_assert ((channel != NULL && source_id > 0) || (channel == NULL && source_id == 0));

  g_debug ("chn_pty_disconnect");

  if (child_pid >= 0)
    {
      rc = kill (child_pid, SIGTERM);
      if (rc == -1)
        {
          if (errno == ESRCH)
            g_warning ("chn_pty_disconnect: child pid=%d not found", child_pid);
          else
            g_error ("chn_pty_disconnect: kill(): %s", strerror (errno));
        }
      child_pid = -1;
    }

  if (source_id > 0)
    {
      rc = g_source_remove (source_id);
      g_assert (rc == TRUE);
      source_id = 0;
    }

  if (channel != NULL)
    {
      g_io_channel_unref (channel);
      channel = NULL;
    }
}

static gsize
chn_pty_write (const void *buf, gsize len)
{
  gsize pos;

  g_assert (buf != NULL);

  pos = 0;

  if (channel == NULL)
    return 0;

  while (len > 0)
    {
      gsize n;
      GIOStatus status;
      GError *err;

      err = NULL;
      status = g_io_channel_write_chars (channel, (gchar *)buf+pos, len, &n, &err);
      g_assert ((status != G_IO_STATUS_ERROR && err == NULL) || (status == G_IO_STATUS_ERROR && err != NULL));

      if (err != NULL)
        {
          if (status != G_IO_STATUS_AGAIN)
            {
              if (channel_callbacks.error != NULL)
                channel_callbacks.error (err, channel_callbacks.user_data);
            }
          else
            pos += n;
          g_error_free (err);
          break;
        }

      pos += n;
      len -= n;
    }

  return pos;
}

static gsize
chn_pty_prepend (const void *buf, gsize len)
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

