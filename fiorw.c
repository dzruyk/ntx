#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#ifndef __unix__
#include <windows.h>
#endif

#include <glib.h>
#include <gio/gio.h>

#include "fiorw.h"

/** 3
 *   fio - API functions for coprocess file I/O
 * SYNOPSIS
 *   #include <fiorw.h>
 * DESCRIPTION
 *   These API functions are used to start, terminate and communicate with the
 *   fio(1) running as a coprocess. The programming model is not very
 *   complicated: user sets callback functions in \fBFIOCallbacks\fP structure,
 *   --- through which he will be notified on response from fio(1), write buffer
 *   space availability and coprocess termination --- opens an arbitrary file
 *   in read-only, write-only or write-append mode and starts sending commands
 *   to fio(1) coprocess through fio_write(3).
 * EXAMPLES
 *   The following example demonstrates a typical usage of this API:
 *
 *     FIOCallbacks callbacks;
 *     ...
 *     memset (&callbacks, 0, sizeof (callbacks));
 *     callbacks.user_data = NULL;
 *     callbacks.read_data = read_data_cb;
 *     callbacks.kick_writer = kick_writer_cb;
 *     callbacks.coproc_exited = coproc_exited_cb;
 *     callbacks.io_error = io_error_cb;
 *     fio_set_callbacks (&callbacks, NULL);
 *
 *     fio_open_readonly ("/etc/passwd");
 *     fio_write ("R64\n", 4);
 *
 *     g_main_loop_run ();
 *
 *     fio_close ();
 *     return 0;
 *
 *   For the more elaborate example see \fItest_fio.c\fP file.
 * SEE ALSO
 *   fio(1), fio_set_callbacks(3), fio_open_readonly(3), fio_open_writeonly(3),
 *   fio_open_append(3), fio_write(3), fio_close(3), fio_write_buffer_space(3).
 */

#ifdef __unix__
#  define FIONAME    "fio"
#  define FIOPROG    "./fio"
#else
#  define FIONAME    "fio.exe"
#  define FIOPROG    "fio.exe"
#endif

#define WBUFSZ     4096

static GIOChannel  *rchannel          = NULL;      /* read from child I/O channel */
static GIOChannel  *wchannel          = NULL;      /* write to child I/O channel */
static guint        rsource_id        = 0;         /* read channel event source id */
static guint        wsource_id        = 0;         /* write channel event source id */
static guint        child_watch_id    = 0;         /* watch on child process */
static GPid        child_pid         = -1;        /* PID of a child process */
static guchar       writebuf[WBUFSZ];              /* write buffer */
static guint        writebuf_tail     = 0;         /* write buffer tail */
static guint        writebuf_head     = 0;         /* write buffer head */
static FIOCallbacks fiocb;

static gboolean fio_read_event  (GIOChannel *channel, GIOCondition condition, gpointer user_data);
static gboolean fio_write_event (GIOChannel *channel, GIOCondition condition, gpointer user_data);
static gboolean fio_open        (const gchar *filename, const gchar *mode);
static void     fio_end         ();

/* This helper aligns data residing in the writebuf.
 * It is used to make space available there for writing.
 */
static void
writebuf_align ()
{
  if (writebuf_tail != writebuf_head)
    {
      memmove (writebuf, writebuf+writebuf_tail, writebuf_head-writebuf_tail);
      writebuf_head -= writebuf_tail;
      writebuf_tail = 0;
    }
}

static void
fio_end ()
{
  gint rc;

  if (rsource_id > 0)
    {
      g_debug ("removing r source_id=%d", rsource_id);
      rc = g_source_remove (rsource_id);
      g_assert (rc == TRUE);
      rsource_id = 0;
    }

  if (wsource_id > 0)
    {
      g_debug ("removing w source_id=%d", wsource_id);
      rc = g_source_remove (wsource_id);
      g_assert (rc == TRUE);
      wsource_id = 0;
    }

  if (rchannel != NULL)
    {
      g_debug ("closing r channel");
      g_io_channel_unref (rchannel);
      rchannel = NULL;
    }

  if (wchannel != NULL)
    {
      g_debug ("closing w channel");
      g_io_channel_unref (wchannel);
      wchannel = NULL;
    }

  /* Pipe to child is not valid now. Indicate this by setting child_pid and
   * watch_id to initial values. The cooperating process must terminate when it
   * sees EOF on his pipe end.
   */
  if (child_pid >= 0)
    child_pid = -1;

  if (child_watch_id > 0)
    child_watch_id = 0;
}

static void
fio_child_exited (GPid pid, gint status, gpointer user_data)
{
  gint code;

  g_assert (pid >= 0);

#ifdef __unix__

  if (WIFEXITED (status))
    {
      code = WEXITSTATUS (status);
      g_assert (code >= 0);
    }
  else if (WIFSIGNALED (status))
    {
      code = WTERMSIG (status) + 255;
      g_assert (code >= 256);
    }
  else
    {
      g_warn_if_reached ();
      code = -1;
    }

#else

  if (GetExitCodeProcess (pid, &code) != TRUE)
    {
      g_warn_if_reached ();
      code = -1;
    }

#endif

  /* Notify user that the coprocess has exited.
   */
  if (fiocb.coproc_exited != NULL)
    (*fiocb.coproc_exited) (pid, code, fiocb.user_data);
}

/* Helper to set up unbuffered non-blocking channel.
 */
static void
setup_nonblock_channel (GIOChannel *channel, gboolean close_on_unref)
{
  g_assert (channel != NULL);

  g_io_channel_set_encoding (channel, NULL, NULL);
  g_io_channel_set_buffered (channel, FALSE);
  g_io_channel_set_flags (channel, G_IO_FLAG_NONBLOCK, NULL);
  g_io_channel_set_close_on_unref (channel, close_on_unref);
}

static gboolean
fio_open (const gchar *filename, const gchar *mode)
{
  GPid pid;
  gint fdwrite, fdread;
  gchar *argv[] = {FIONAME, FIOPROG, mode, filename, NULL};

  if (g_spawn_async_with_pipes (NULL, argv, NULL,
        G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, &pid, &fdwrite, &fdread, NULL, NULL) != TRUE)
    return FALSE;

  /* Set write buffer to initial state.
   */
  writebuf_head = writebuf_tail = 0;

  rchannel = g_io_channel_unix_new (fdread);
  g_assert (rchannel != NULL);
  setup_nonblock_channel (rchannel, TRUE);
  g_assert (rsource_id == 0);
  rsource_id = g_io_add_watch (rchannel, G_IO_IN | G_IO_ERR | G_IO_HUP, fio_read_event, NULL);
  g_assert (rsource_id > 0);

  wchannel = g_io_channel_unix_new (fdwrite);
  g_assert (wchannel != NULL);
  setup_nonblock_channel (wchannel, TRUE);

  g_assert (child_watch_id == 0);
  child_watch_id = g_child_watch_add (pid, fio_child_exited, NULL);
  g_assert (child_watch_id > 0);

  g_assert (child_pid < 0);
  child_pid = pid;

  return TRUE;
}

static gboolean
fio_write_event (GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
  GError *err;
  GIOStatus status;
  gsize len, written;

  g_assert (channel != NULL && channel == wchannel);
  g_assert (condition == G_IO_OUT);

  g_assert (wsource_id > 0);

  written = 0;
  len = writebuf_head - writebuf_tail;

  if (len > 0)
    {
      err = NULL;
      status = g_io_channel_write_chars (wchannel, (const gchar *)writebuf+writebuf_tail, len, &written, &err);
      g_assert ((err == NULL && status == G_IO_STATUS_NORMAL) || (err != NULL && status != G_IO_STATUS_NORMAL));

      g_debug ("fio_write_event: %lu bytes in buffer, %lu written", len, written);

      if (written > 0)
        {
          g_assert (written <= len);
          writebuf_tail += written;
        }

      if (status != G_IO_STATUS_NORMAL)
        {
          if (status == G_IO_STATUS_AGAIN)
            {
              g_assert (err != NULL);
              g_error_free (err);
            }
          else
            {
              g_assert (err != NULL);
              g_error ("fio_write_event: error writing fd=%d: %s", g_io_channel_unix_get_fd (wchannel), err->message);
              g_error_free (err);
            }
        }
    }

  /* Move tail and head to initial postion, so that the free space is seen. */
  if (writebuf_tail == writebuf_head)
    writebuf_tail = writebuf_head = 0;

  /* Kick writer function of the user side, which is allowed to call fio_write() from inside.
   */
  if (written > 0)
    {
      if (fiocb.kick_writer != NULL)
        (*fiocb.kick_writer) (fiocb.user_data);
    }

  /* disable write event if buffer is empty */
  if (writebuf_tail == writebuf_head)
    {
      g_debug ("fio_write_event: disabling G_IO_OUT");
      /* write event source will be removed */
      wsource_id = 0;
      return FALSE;
    }

  return TRUE;
}

/** 3
 *   fio_write - write data to fio coprocess
 * DESCRIPTION
 *   This function writes arbitrary \fIlen\fP bytes of data pointed by \fIbuf\fP to
 *   fio coprocess.
 *   If only partial write succeeded, remaining unwritten data is buffered and
 *   write operation is delayed until underlaying file descriptor signals that it is
 *   able to accept more data. Function guarantees to be able to buffer unwritten bytes
 *   not less then fio_write_buffer_space() returned. It means that the user is
 *   safe to pass fio_write_buffer_space() bytes of data.
 *
 * RETURN VALUE
 *   On success, the number of bytes written is returned; zero means no bytes
 *   were written. On error -1 is returned.
 *
 * SEE ALSO
 *   fio(1), fio_open(3), fio_write_buffer_space(3), fio_close(3), FIOCallbacks(3)
 */
gssize
fio_write (const void *buf, gsize len)
{
  GError *err;
  GIOStatus status;
  gssize written;

  g_assert (buf != NULL);

  if (wchannel == NULL)
    return -1;

  if (len == 0)
    return 0;

  if (writebuf_head == writebuf_tail)
    {

      err = NULL;
      status = g_io_channel_write_chars (wchannel, buf, len, (gsize *)&written, &err);
      g_assert ((err != NULL && status != G_IO_STATUS_NORMAL) || (err == NULL && status == G_IO_STATUS_NORMAL));

      /* Bytes written can be nonzero, even if the return value is not
       * G_IO_STATUS_NORMAL. Handle this case the best we can.
       */

      if (written < len)
        {
          gsize n, left;

          writebuf_tail = writebuf_head = 0;

          /* calculate data left unwritten */
          left = len - written;

          /* calculate minimum between available buffer space and unwritten data */
          n = MIN (sizeof (writebuf), left);

          if (n > 0)
            {
              if (n < left)
                g_warning ("fio_write: buffer truncated");
              memcpy (writebuf, buf+written, n);
              writebuf_head += n;
            }
          else
            {
              g_warning ("fio_write: no buffer space");
            }

          if (wsource_id == 0)
            {
              wsource_id = g_io_add_watch (wchannel, G_IO_OUT, fio_write_event, NULL);
              g_assert (wsource_id > 0);
            }
        }

      if (status != G_IO_STATUS_NORMAL)
        {
          if (status == G_IO_STATUS_AGAIN)
            {
              g_assert (err != NULL);
              g_error_free (err);
            }
          else
            {
              g_error ("fio_write: error writing fd=%d: %s", g_io_channel_unix_get_fd (wchannel), err->message);
              g_error_free (err);
              written = -1;
            }
        }
    }
  else
    {
      gsize n;

      g_assert (writebuf_head > writebuf_tail);

      /* try to align write buffer and free some space */
      if (sizeof (writebuf) - writebuf_head < len)
        writebuf_align ();

      n = MIN (sizeof (writebuf)-writebuf_head, len);

      if (n > 0)
        {
          if (n < len)
            g_warning ("fio_write: buffer truncated");
          memcpy (writebuf+writebuf_head, buf, n);
          writebuf_head += n;
        }
      else
        {
          g_warning ("fio_write: no buffer space");
        }

      g_assert (wsource_id > 0);

      written = 0;
    }

  return written;
}

static gboolean
fio_read_event (GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
  gchar buffer[1024];
  GError *err;
  GIOStatus status;
  gsize len;

  g_assert (channel != NULL && channel == rchannel);

  /* g_debug ("fio_read_event: fd=%d condition=0x%04x", g_io_channel_unix_get_fd (channel), condition); */

  if (condition & G_IO_IN)
    {
      err = NULL;
      status = g_io_channel_read_chars (channel, buffer, sizeof (buffer), &len, &err);
      g_assert ((err == NULL && status == G_IO_STATUS_NORMAL) || (err != NULL && status != G_IO_STATUS_NORMAL));

      if (len > 0)
        {
          if (fiocb.read_data != NULL)
            (*fiocb.read_data) ((guchar *)buffer, len, fiocb.user_data);
        }

      if (status != G_IO_STATUS_NORMAL)
        {
          if (status == G_IO_STATUS_AGAIN)
            {
              g_assert (err != NULL);
              g_error_free (err);
            }
          else
            {
              g_assert (err != NULL);
              g_warning ("fio_read_event: error reading fd=%d: %s", g_io_channel_unix_get_fd (channel), err->message);
              g_error_free (err);
            }
        }
    }

  if (condition & (G_IO_ERR | G_IO_HUP))
    {
      /* Do not call io_error callback twice, if G_IO_ERR signaled.
       */
      if (condition & G_IO_ERR)
        {
          g_debug ("fio_read_event: channel error fd=%d", g_io_channel_unix_get_fd (channel));
          if (fiocb.io_error != NULL)
            (*fiocb.io_error) (FALSE, fiocb.user_data);
        }
      else
        {
          g_debug ("fio_read_event: channel hangup fd=%d", g_io_channel_unix_get_fd (channel));
          if (fiocb.io_error != NULL)
            (*fiocb.io_error) (TRUE, fiocb.user_data);
        }
    }

  return TRUE;
}

/** 3
 *   fio_close - terminate fio coprocess
 * DESCRIPTION
 *   This function releases resources associated with previous fio_open() call.
 *   Call this function when you are done with fio session, or want to start a
 *   new one.
 * RETURN VALUE
 *   This function returns nothing.
 */
void
fio_close ()
{
  fio_end ();
}

/** 3
 *   fio_open_readonly - open file for fio in read-only mode
 * DESCRIPTION
 *   This function opens a file \fIfilename\fP for fio in read-only mode.
 *   After successful call to this function, user can call fio_write() to send
 *   commands to fio coprocess.
 * RETURN VALUE
 *   On success, this function returns TRUE; on failure it returns FALSE.
 */
gboolean
fio_open_readonly (const gchar *filename)
{
  g_assert (filename != NULL);

  return fio_open (filename, "-r");
}

/** 3
 *   fio_open_writeonly - open file for fio in write-only mode
 * DESCRIPTION
 *   This function opens a file \fIfilename\fP for fio in write-only mode.
 *   After successful call to this function, user can call fio_write() to send
 *   commands to fio coprocess.
 * RETURN VALUE
 *   On success, this function returns TRUE; on failure it returns FALSE.
 */
gboolean
fio_open_writeonly (const gchar *filename)
{
  g_assert (filename != NULL);

  return fio_open (filename, "-w");
}

/** 3
 *   fio_open_append - open file for fio in write-append mode
 * DESCRIPTION
 *   This function opens a file \fIfilename\fP for fio in write-append mode.
 *   After successful call to this function, user can call fio_write() to send
 *   commands to fio coprocess.
 * RETURN VALUE
 *   On success, this function returns TRUE; on failure it returns FALSE.
 */
gboolean
fio_open_append (const gchar *filename)
{
  g_assert (filename != NULL);

  return fio_open (filename, "-a");
}

/** 3
 *   fio_write_buffer_space - get write buffer space
 * DESCRIPTION
 *   This function returns available free space (in bytes) in the write buffer.
 *   A consequent call to the fio_write() function guarantees that at least the
 *   returned number of bytes can be delayed for later write if underlaying file
 *   descriptor is not available for writing.
 * RETURN VALUE
 *   Available write buffer space, in bytes. 0 means no free space available.
 */
gsize
fio_write_buffer_space ()
{
  /* Some heuristic rules apply to determine when to align data in the buffer --
   * when tail poins over the half of the buffer.
   */
  if (writebuf_tail >= sizeof (writebuf)/2)
    writebuf_align ();

  return sizeof (writebuf) - writebuf_head;
}

/** 3
 *   fio_set_callbacks - set callbacks for fio events
 * DESCRIPTION
 *   This function sets callbacks pointed by \fIcb\fP, making a full copy of
 *   this structure. If \fIold\fP is not a null pointer, then the old callback
 *   structure is copied there. If \fIcb\fP is a null pointer, callbacks are not
 *   modified.
 * RETURN VALUE
 *   This function returns nothing.
 */
void
fio_set_callbacks (const FIOCallbacks *cb, FIOCallbacks *old)
{
  if (old != NULL)
    *old = fiocb;

  if (cb != NULL)
    fiocb = *cb;
}

