#include <sys/stat.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "chn.h"
#include "console.h"
#include "colors.h"
#include "internal.h"
#include "fiorw.h"
#include "os.h"

enum
{
  NUL = 0x00,
  SOH = 0x01,
  LF  = 0x0a,
  DEL = 0x7f,
  BS  = 0x08,
  BEL = 0x07,
  ESC = 0x1b
};

enum
{
  S_0,
  S_CMD,
  S_PARAM
};

enum
{
  C_START_IOS = 1,
  C_CLEAR_SCREEN = 2,
  C_CLEAR_EOL = 3,
  C_SET_COLOR = 4,
  C_MOVE_CURSOR = 7,
  C_CURSOR_OFF = 8,
  C_SET_CURSOR_UNDERSCORE = 9,
  C_SET_CURSOR_FULLBLOCK = 10,
  C_SET_CURSOR_HALFBLOCK = 11,
  C_OUTPUT_STRING = 14,
  C_SCROLL_BOX_UP = 28,
  C_SCROLL_BOX_DOWN = 29,
  C_STOP_IOS = 34,
  C_BELL = 36,
  C_FILE_EXISTS = 40,
  C_FILE_OPEN = 41,
  C_FILE_NEWLINE = 42,
  C_FILE_WRITE_STRING = 43,
  C_FILE_CLOSE = 44,
  C_FILE_READ_STRING = 45,
  C_OS_COMMAND = 48,
  C_KEYBOARD_LOCK = 50,
  C_KEYBOARD_UNLOCK = 51,
  C_ARE_YOU_ALIVE = 56,
  C_LOCAL_ACTION = 57,
  C_FILE_BINARY_WRITE = 59,
  C_FILE_BINARY_READ = 60,
  C_GET_CONSOLE_SIZE = 61,
  C_GET_VERSION = 62,
  C_MOUSE_ENABLE = 63,
  C_MOUSE_DISABLE = 64,
  C_GET_CWD = 70,
  C_READ_INI = 71,
  C_GET_TEMPORARY_DIRECTORY = 72
};

#ifdef _CLIENT_DEBUG
#define DEBUG(fmt, arg...) \
  do { \
    fprintf (stderr, fmt "\r\n", ##arg); \
  } while (0)
#else
#define DEBUG(fmt, arg...) do { } while (0)
#endif

#define NARGMAX                 128           /* maximum number of arguments passed to child process */
#define MAXPARAM                8192          /* parameters buffer size */

#define VERSION "3.13"

#define COMMAND_WRAPPER_BIN     "cmdwrapper"  /* name of a binary to wrap C_OS_COMMAND */

#define FG_COLOR(c)    (0x0f & (c))
#define BG_COLOR(c)    ((0xf0 & (c)) >> 4)

static GIConv  cd;
static guchar   param[MAXPARAM];
static guint    paramlen = 0;
static gint     state = S_0;
static gint     cmd = -1;                     /* command number */
static gboolean ios_started = 0;              /* TRUE if C_START_IOS was received */
static gboolean file_opened = FALSE;          /* TRUE indicates C_FILE_OPEN opened a file with fio_open_xxx() */
static guint    child_event_id = 0;           /* event source id of external program started by C_OS_COMMAND */

static void   send_response           (gchar c);
static void   client_read_data_cb     (guchar *buffer, gsize len, gpointer user_data);
static void   client_kick_writer_cb   (gpointer user_data);
static void   client_coproc_exited_cb (gint pid, gint code, gpointer user_data);
static void   client_io_error_cb      (gboolean hangup, gpointer user_data);


extern GtkWidget *console;


gboolean
client_in_telnet_mode ()
{
  return ios_started ? FALSE : TRUE;
}

void
client_init ()
{
  FIOCallbacks callbacks;
  const gchar *to = "utf8";
  const gchar *from = "cp866";

  cd = g_iconv_open (to, from);
  if (cd == (GIConv) -1)
    {
      if (errno == EINVAL)
        g_error ("g_iconv_open can't convert %s to %s", from, to);
      else
        g_error ("g_iconv_open failed: %s", strerror (errno));
    }

  memset (&callbacks, 0, sizeof (callbacks));
  callbacks.user_data = NULL;
  callbacks.read_data = client_read_data_cb;
  callbacks.kick_writer = client_kick_writer_cb;
  callbacks.coproc_exited = client_coproc_exited_cb;
  callbacks.io_error = client_io_error_cb;
  fio_set_callbacks (&callbacks, NULL);

  state = S_0;
  cmd = -1;
}

void
client_deinit ()
{

  if (file_opened)
    fio_close ();

  g_iconv_close (cd);
}

/* This helper does actual console output. */
static void
client_write_console (guchar *buf, guint len)
{
  gchar *p, *end;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  p = (gchar *)buf;
  end = p + len;

  while (*p != '\0' && p < end)
  {
    gunichar uc;
    uc = g_utf8_get_char (p);
    if (uc > 0
        && (g_unichar_isprint (uc) || g_unichar_isspace (uc)
            || uc == DEL || uc == BS || uc == BEL))
      {
        console_put_char (CONSOLE (console), uc);
      }
    p = g_utf8_next_char (p);
  }
}

static void
client_change_color (gint foreground, gint background)
{
  static gchar *color_list[] =
    {
      COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN,
      COLOR_RED, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE,
      COLOR_BRBLACK, COLOR_BRBLUE, COLOR_BRGREEN, COLOR_BRCYAN,
      COLOR_BRRED, COLOR_BRMAGENTA, COLOR_BRYELLOW, COLOR_BRWHITE
    };

  GdkColor fg_color, bg_color;

  g_return_if_fail (foreground >= 0 && foreground < G_N_ELEMENTS (color_list));
  g_return_if_fail (background >= 0 && background < G_N_ELEMENTS (color_list));
  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  gdk_color_parse (color_list[foreground], &fg_color);
  gdk_color_parse (color_list[background], &bg_color);

  console_set_foreground_color (CONSOLE (console), &fg_color);
  console_set_background_color (CONSOLE (console), &bg_color);
}

static void
client_scroll_box_up (guint x1, guint y1, guint x2, guint y2, guint color, guint nr)
{
  GdkColor fg_color, bg_color;
  guint box_width, box_height;

  g_return_if_fail (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_SCROLL_BOX_UP x1=%d y1=%d x2=%d y2=%d color=0x%02x nr=%d", x1, y1, x2, y2, color, nr);

  console_get_foreground_color (CONSOLE (console), &fg_color);
  console_get_background_color (CONSOLE (console), &bg_color);

  client_change_color (FG_COLOR (color), BG_COLOR (color));

  box_width = x2 - x1 + 1;
  box_height = y2 - y1 + 1;

  console_scroll_box_up (CONSOLE (console), x1, y1, box_width, box_height, nr);

  console_set_foreground_color (CONSOLE (console), &fg_color);
  console_set_background_color (CONSOLE (console), &bg_color);
}

static void
client_scroll_box_down (guint x1, guint y1, guint x2, guint y2, guint color, guint nr)
{
  GdkColor fg_color, bg_color;
  guint box_width, box_height;

  g_return_if_fail (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_SCROLL_BOX_DOWN x1=%d y1=%d x2=%d y2=%d color=0x%02x nr=%d", x1, y1, x2, y2, color, nr);

  console_get_foreground_color (CONSOLE (console), &fg_color);
  console_get_background_color (CONSOLE (console), &bg_color);

  client_change_color (FG_COLOR (color), BG_COLOR (color));

  box_width = x2 - x1 + 1;
  box_height = y2 - y1 + 1;

  console_scroll_box_down (CONSOLE (console), x1, y1, box_width, box_height, nr);

  console_set_foreground_color (CONSOLE (console), &fg_color);
  console_set_background_color (CONSOLE (console), &bg_color);
}

static void
client_get_version ()
{
  gchar buf[64];
  gint len;

  DEBUG (">> C_GET_VERSION: <- %s,ESC", VERSION);

  strncpy (buf, VERSION, sizeof (buf));
  len = strlen (VERSION);
  buf[len] = ESC;
  chn_write (buf, len+1);
}

static void
client_keyboard_lock ()
{
  gchar buf[64];

  DEBUG (">> C_KEYBOARD_LOCK" );

  gui_keyboard_disable ();
  gui_mouse_disable ();

  buf[0] = '9';
  buf[1] = '9';
  buf[2] = '9';
  buf[3] = ESC;
  chn_write (buf, 4);
}

static void
client_keyboard_unlock ()
{
  DEBUG (">> C_KEYBOARD_UNLOCK");

  gui_keyboard_enable ();
  gui_mouse_enable ();
}

static void
client_clear_screen ()
{
  g_assert (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_CLEAR_SCREEN");

  console_erase_display (CONSOLE (console), CONSOLE_ERASE_WHOLE);
}

static void
client_get_console_size ()
{
  gchar buf[64];
  gint width, height, len;

  g_assert (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_GET_CONSOLE_SIZE");

  width = console_get_width (CONSOLE (console));
  height = console_get_height (CONSOLE (console));

  snprintf (buf, sizeof (buf), "%d,%d", width, height);
  len = strlen (buf);
  buf[len] = ESC;
  chn_write (buf, len+1);
}

static void
client_cursor_off ()
{
  g_assert (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_CURSOR_OFF");

  console_set_cursor_shape (CONSOLE (console), CONSOLE_CURSOR_INVISIBLE);
}

static void
client_clear_eol ()
{
  g_assert (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_CLEAR_EOL");

  console_erase_line (CONSOLE (console), CONSOLE_ERASE_TO_END);
}

static void
client_set_cursor_fullblock ()
{
  g_assert (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_SET_CURSOR_FULLBLOCK");

  console_set_cursor_shape (CONSOLE (console), CONSOLE_CURSOR_FULL_BLOCK);
}

static void
client_set_cursor_halfblock ()
{
  g_assert (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_SET_CURSOR_HALFBLOCK");

  console_set_cursor_shape (CONSOLE (console), CONSOLE_CURSOR_LOWER_HALF);
}

static void
client_set_cursor_underscore ()
{
  g_assert (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_SET_CURSOR_UNDERSCORE");

  console_set_cursor_shape (CONSOLE (console), CONSOLE_CURSOR_UNDERSCORE);
}

static void
client_move_cursor (gint x, gint y)
{
  g_assert (console != NULL && IS_CONSOLE (console));

  DEBUG (">> C_MOVE_CURSOR %d %d", x, y);

  console_move_cursor_to (CONSOLE (console), x, y);
}

static void
client_are_you_alive ()
{
  guchar buf[64];

  DEBUG (">> C_ARE_YOU_ALIVE");

  buf[0] = '9';
  buf[1] = '9';
  buf[2] = '8';
  buf[3] = ESC;
  chn_write (buf, 4);
}

static void
client_read_ini (const gchar *section, const gchar *parameter)
{
  guchar buf[64];

  g_assert (section != NULL && parameter != NULL);

  DEBUG (">> C_READ_INI: [%s] %s", section, parameter);

  buf[0] = ESC;
  chn_write (buf, 1);
}

static void
client_mouse_enable ()
{
  DEBUG (">> C_MOUSE_ENABLE");

  gui_mouse_enable ();
}

static void
client_mouse_disable ()
{
  DEBUG (">> C_MOUSE_DISABLE");

  gui_mouse_disable ();
}

static void
client_get_temporary_directory ()
{
  gchar pname[PATH_MAX+1]; /* including terminating NUL and `/' */
  gsize len;

  os_get_temporary_directory (pname, PATH_MAX);

  DEBUG (">> C_GET_TEMPORARY_DIRECTORY -> %s", pname);

  len = strlen (pname);
  g_assert (len < PATH_MAX && pname[len] == '\0');
  pname[len] = '/';
  pname[len+1] = ESC;
  chn_write (pname, len+2);
}

static void
client_file_open (const gchar *filename, gchar how)
{
  gchar nm[PATH_MAX];
  gchar tmp[PATH_MAX];
  gboolean ok;

  if (file_opened)
    {
      g_warning ("client_file_open: attempt to open second file?");
      fio_close ();
    }

  /* This is weird, but this is how we handle file names.
   * If no directory path was specified, prefix file name with
   * temporary directory.
   */
  if (filename[0] != '/' || strchr (filename, '/') == NULL)
    {
      g_snprintf (nm, sizeof (nm), "%s/%s", os_get_temporary_directory (tmp, sizeof (tmp)), filename);
      filename = nm;
    }

  DEBUG (">> C_FILE_OPEN <- %s", filename);

  if (how == 'r')
    ok = fio_open_readonly (filename);
  else if (how == 'w')
    ok = fio_open_writeonly (filename);
  else if (how == 'a')
    ok = fio_open_append (filename);
  else
    {
      g_warn_if_reached ();
      ok = FALSE;
    }

  if (ok)
    {
      file_opened = TRUE;
      send_response ('1');
    }
  else
    send_response ('2');
}

static void
client_file_close ()
{
  DEBUG (">> C_FILE_CLOSE");

  if (file_opened)
    fio_close ();

  file_opened = FALSE;
}

static void
client_get_cwd ()
{
  gchar *s, pname[PATH_MAX];
  guint len;

  if ((s = getcwd (pname, sizeof (pname))) != NULL)
    {
      DEBUG (">> C_GET_CWD -> %s", s);
      len = strlen (s);
      s[len] = ESC;
      chn_write (s, len+1);
    }
}

static void
client_file_exists (const gchar *filename)
{
  gchar nm[PATH_MAX];
  gchar tmp[PATH_MAX];
  struct stat st;
  gchar c;

  /* Prefix file name with temporary directory, if it doesn't looks like an
   * absolute path and has no directories as its components.
   */
  if (filename[0] != '/' || strchr (filename, '/') == NULL)
    {
      g_snprintf (nm, sizeof (nm), "%s/%s", os_get_temporary_directory (tmp, sizeof (tmp)), filename);
      filename = nm;
    }

  if (stat (filename, &st) == 0 && S_ISREG (st.st_mode))
    c = '2';
  else
    {
      if (errno == ENOTDIR || errno == ENOENT || errno == ENAMETOOLONG || errno == ELOOP)
        c = '1';
      else
        c = '0';
    }

  send_response (c);
}

/* This helper sends a single-character response terminated by ESC.
 */
static void
send_response (gchar c)
{
  gchar buf[2];

  buf[0] = c;
  buf[1] = ESC;
  chn_write (buf, 2);
}

/* This small helper is run when the program stared by C_OS_COMMAND terminated.
 */
static void
child_watch (GPid pid, gint status, gpointer user_data)
{
  gboolean ok;

  g_assert (child_event_id > 0);

  /* Successful program termination is determined by the EXIT_SUCCESS code.
   * Otherwise it is considered to have terminated abnormally.
   */
  if (os_process_is_exited (pid, status) && os_process_get_exit_status (pid, status) == EXIT_SUCCESS)
    ok = TRUE;
  else
    ok = FALSE;

  g_debug ("client_os_command: child pid %d exited %s", (int) pid, ok ? "OK" : "FAIL");

  child_event_id = 0;
}

static void
client_os_command (const gchar *cmd)
{
  gchar buf[1024];
  gchar *argv[NARGMAX+1] = { NULL };
  gchar *c, *end;
  gint argc;
  GError *err;
  GPid pid;
  GSpawnFlags flags;

  DEBUG (">> C_OS_COMMAND: '%s'", cmd);

  if (child_event_id > 0)
    {
      g_warning ("client_os_command: attempt to run two commands?");
      send_response ('0');
    }
  else
    {
      argc = 1;
      argv[0] = COMMAND_WRAPPER_BIN;

      g_strlcpy (buf, cmd, sizeof (buf));

      /* Split command name and arguments to argv[] vector. Glib has the g_strsplit()
       * function but it complicates memory management a little bit. We use a
       * custom-tailored analogue of it here. If someone identifies it as a sin --
       * replace it with Glib's one for your taste.
       */
      c = buf;
      end = buf + strlen (buf);

      while (c < end)
        {
          while (g_ascii_isspace (*c))
            c++;

          if (*c != '\0' && argc < NARGMAX)
            argv[argc++] = c;

          while (*c != '\0' && !g_ascii_isspace (*c))
            c++;

          *c++ = '\0';
        }

      g_assert (argc < NARGMAX);

      argv[argc] = NULL;

      /* Shut up spawned child's stdout and stderr. */
      flags = G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_STDOUT_TO_DEV_NULL;

      err = NULL;
      if (!g_spawn_async (NULL, argv, NULL, flags, NULL, NULL, &pid, &err))
        {
          g_assert (err != NULL);
          g_warning ("client_os_command: can't spawn a child: %s", err->message);
          g_error_free (err);
          send_response ('0');
        }
      else
        {
          g_assert (err == NULL);
          g_debug ("client_os_command: process pid %d spawned", pid);
          g_assert (child_event_id == 0);
          child_event_id = g_child_watch_add (pid, child_watch, NULL);
          g_assert (child_event_id > 0);
          send_response ('1');
        }
    }
}

static void
client_io_error_cb (gboolean hangup, gpointer user_data)
{
  g_assert (file_opened);

  g_debug ("client_io_error_cb: %s on pipe to coprocess", hangup ? "hangup" : "error");

  fio_close ();

  file_opened = FALSE;
}

static void
client_read_data_cb (guchar *buffer, gsize len, gpointer user_data)
{

  if (file_opened)
    {
      if (len > 2 && buffer[len-1] == LF && buffer[len-2] == ESC)
        {
          /* send response with LF byte at the end stripped */
          chn_write (buffer, len-1);
        }
      else
        g_warning ("client_read_data_cb: no <LF><ESC> terminator");
    }
  else
    g_warning ("client_read_data_cb: no file opened");
}

static void
client_kick_writer_cb (gpointer user_data)
{
  /* This function does nothing. Its purpose is to enable read events on
   * a channel, because there is write buffer space available in fio.
   */
}

static void
client_coproc_exited_cb (gint pid, gint code, gpointer user_data)
{
  DEBUG ("client_coproc_exited_cb: pid=%d code=%d", pid, code);

  /* This function does nothing. Its purpose is to notify user that the
   * coprocess has exited for debugging and informational purposes.
   */
}

void
client_do_input (guchar *buf, gsize len)
{
  guchar buffer[1024*6];
  gchar *inptr, *outptr;
  size_t inleft, outleft;
  gint i, n, rc;

  n = 0;
  inleft = 0;
  inptr = (gchar *)buf;

  for (i = 0; i < len; i++)
    {
      guchar c;

      c = buf[i];

      switch (state)
        {
        case S_0:
          if (c == 0)
            {
              /* Next byte will be a command number. */
              state = S_CMD;

              /* Flush non-command buffer contents to console. */
              while (inleft > 0)
                {
                  outleft = sizeof (buffer);
                  outptr = (gchar *)buffer;

                  rc = g_iconv (cd, &inptr, &inleft, &outptr, &outleft);

                  if (rc == (size_t) -1)
                    {
                      if (errno == EINVAL)
                        break;
                      else
                        {
                          g_warning ("g_iconv: can't convert sequence: %s", strerror (errno));
                          break;
                        }
                    }
                  else
                    client_write_console (buffer, sizeof (buffer)-outleft);
                }
            }
          else
            {
              buf[n++] = c;
              inleft++;
            }
          break;

        case S_CMD:
          cmd = c;
          switch (cmd)
            {
            case C_START_IOS:
              DEBUG ("starting IOS...");
              ios_started = TRUE;
              state = S_0;
              break;

            case C_STOP_IOS:
              DEBUG ("stopping IOS...");
              ios_started = FALSE;
              state = S_0;
              break;

            case C_GET_VERSION:
              client_get_version ();
              state = S_0;
              break;

            case C_KEYBOARD_LOCK:
              client_keyboard_lock ();
              state = S_0;
              break;

            case C_KEYBOARD_UNLOCK:
              client_keyboard_unlock ();
              state = S_0;
              break;

            case C_CLEAR_SCREEN:
              client_clear_screen ();
              state = S_0;
              break;

            case C_GET_CONSOLE_SIZE:
              client_get_console_size ();
              state = S_0;
              break;

            case C_CURSOR_OFF:
              client_cursor_off ();
              state = S_0;
              break;

            case C_CLEAR_EOL:
              client_clear_eol ();
              state = S_0;
              break;

            case C_SET_CURSOR_FULLBLOCK:
              client_set_cursor_fullblock ();
              state = S_0;
              break;

            case C_SET_CURSOR_HALFBLOCK:
              client_set_cursor_halfblock ();
              state = S_0;
              break;

            case C_SET_CURSOR_UNDERSCORE:
              client_set_cursor_underscore ();
              state = S_0;
              break;

            case C_MOUSE_DISABLE:
              client_mouse_disable ();
              state = S_0;
              break;

            case C_GET_CWD:
              client_get_cwd ();
              state = S_0;
              break;

            case C_FILE_CLOSE:
              client_file_close ();
              state = S_0;
              break;

            case C_GET_TEMPORARY_DIRECTORY:
              client_get_temporary_directory ();
              state = S_0;
              break;

            case C_FILE_READ_STRING:
              param[0] = 'R';
              paramlen = 1;
              state = S_PARAM;
              break;

            case C_FILE_WRITE_STRING:
              param[0] = 'W';
              paramlen = 1;
              state = S_PARAM;
              break;

            case C_FILE_BINARY_READ:
              param[0] = 'r';
              paramlen = 1;
              state = S_PARAM;
              break;

            case C_FILE_BINARY_WRITE:
              param[0] = 'w';
              paramlen = 1;
              state = S_PARAM;
              break;

            default:
              state = S_PARAM;
              paramlen = 0;
              break;
            }
          break;

        case S_PARAM:
          switch (cmd)
            {
            case C_SET_COLOR:
              client_change_color (FG_COLOR (c), BG_COLOR (c));
              state = S_0;
              break;

            case C_MOVE_CURSOR:
              param[paramlen++] = c;
              if (paramlen >= 2)
                {
                  guint x, y;

                  x = param[0]-1;
                  y = param[1]-1;
                  client_move_cursor (x, y);
                  paramlen = 0;
                  state = S_0;
                }
              break;

            case C_OUTPUT_STRING:
              DEBUG (">> C_OUTPUT_STRING");
              break;

            case C_SCROLL_BOX_DOWN:
            case C_SCROLL_BOX_UP:
              param[paramlen++] = c;
              if (paramlen >= 6)
                {
                  guint x1, y1, x2, y2;
                  guint color, nr;

                  x1 = param[0]-1;
                  y1 = param[1]-1;
                  x2 = param[2]-1;
                  y2 = param[3]-1;
                  color = param[4];
                  nr = param[5];

                  switch (cmd)
                    {
                    case C_SCROLL_BOX_DOWN:
                      client_scroll_box_down (x1, y1, x2, y2, color, nr);
                      break;
                    case C_SCROLL_BOX_UP:
                      client_scroll_box_up (x1, y1, x2, y2, color, nr);
                      break;
                    default:
                      g_warn_if_reached ();
                    }
                  paramlen = 0;
                  state = S_0;
                }
              break;

            case C_BELL:
              param[paramlen++] = c;
              if (paramlen >= 1)
                {
                  DEBUG (">> C_BELL");
                  paramlen = 0;
                  state = S_0;
                }
              break;

            case C_FILE_EXISTS:
              if (c != NUL)
                {
                  if (paramlen < MAXPARAM-1)
                    param[paramlen++] = c;
                }
              else
                {
                  param[paramlen] = '\0';
                  client_file_exists ((const gchar *)param);
                  paramlen = 0;
                  state = S_0;
                }
              break;

            case C_FILE_OPEN:
              if (c != NUL)
                {
                  if (paramlen < MAXPARAM-1)
                    param[paramlen++] = c;
                }
              else
                {
                  const gchar *filename;
                  char how;

                  param[paramlen] = '\0';

                  how = g_ascii_tolower (param[0]);
                  filename = (const gchar *)param + 1;

                  client_file_open (filename, how);

                  paramlen = 0;
                  state = S_0;
                }
              break;

            case C_FILE_NEWLINE:
              DEBUG (">> C_FILE_NEWLINE");
              break;

            case C_FILE_WRITE_STRING:
            case C_FILE_BINARY_WRITE:
              /* There is no much difference between binary and string writes from
               * the client's point of view. Server sends the data to be written
               * and fio writes them to the file. We only need to detect NUL
               * byte, which marks end of data.
               */
              if (c == NUL)
                {
                  /* NUL marks end of parameters */
                  param[paramlen++] = '\n';

                  DEBUG (">> %s %u bytes", cmd == C_FILE_WRITE_STRING ? "C_FILE_WRITE_STRING" : "C_FILE_BINARY_WRITE", paramlen-1);

                  if (file_opened)
                    fio_write (param, paramlen);

                  paramlen = 0;
                  state = S_0;
                }
              else
                {
                  param[paramlen++] = c;

                  /* write partially completed buffer */
                  if (paramlen == MAXPARAM-1)
                    {
                      if (file_opened)
                        fio_write (param, paramlen);
                      paramlen = 0;
                    }
                }

              break;

            case C_FILE_READ_STRING:
            case C_FILE_BINARY_READ:
              /* Reading file is similar to both binary and string conventions.
               * Parameters received from a server are passed directly to fio
               * coprocess. We are responsible to detect an end of parameters
               * marked by NUL byte.
               */
              if (c != NUL)
                {
                  if (paramlen < MAXPARAM-1)
                    param[paramlen++] = c;
                }
              else
                {
                  param[paramlen++] = '\n';

                  DEBUG (">> %s %.*s bytes", cmd == C_FILE_READ_STRING ? "C_FILE_READ_STRING" : "C_FILE_BINARY_READ", paramlen-2, param+1);

                  if (file_opened)
                    fio_write (param, paramlen);

                  state = S_0;
                  paramlen = 0;
                }
              break;

            case C_OS_COMMAND:
              if (c != NUL)
                {
                  if (paramlen < MAXPARAM-1)
                    param[paramlen++] = c;
                }
              else
                {
                  param[paramlen] = '\0';

                  client_os_command ((const gchar *)param);

                  state = S_0;
                  paramlen = 0;
                }
              break;

            case C_LOCAL_ACTION:
              DEBUG (">> C_LOCAL_ACTION");
              break;

            case C_ARE_YOU_ALIVE:
              client_are_you_alive ();
              paramlen = 0;
              state = S_0;
              break;

            case C_READ_INI:
              if (c != NUL)
                {
                  if (paramlen < MAXPARAM)
                    param[paramlen++] = c;
                }
              else
                {
                  char *sec, *par;
                  sec = (char *)param;
                  par = memchr (param, SOH, paramlen);
                  if (par != NULL)
                    {
                      *par = '\0';
                      par++;
                    }
                  if (par != NULL)
                    client_read_ini (sec, par);
                  else
                    DEBUG (">> C_READ_INI: %.*s", paramlen, param);
                  paramlen = 0;
                  state = S_0;
                }
              break;

            case C_MOUSE_ENABLE:
              client_mouse_enable ();
              paramlen = 0;
              state = S_0;
              break;

            case 99:
              DEBUG (" unhandled command 99 ????");
              if (paramlen < 8)
                param[paramlen++] = c;
              else
                {
                  paramlen = 0;
                  state = S_0;
                }
              break;

            default:
              DEBUG (" unknown command %d ??", cmd);
              g_warn_if_reached();
             // exit(4);
              state = S_0;
            }
          break;

        default:
          g_warn_if_reached();
        }
    }

  while (inleft > 0)
    {
      outleft = sizeof (buffer);
      outptr = (gchar *)buffer;

      rc = g_iconv (cd, &inptr, &inleft, &outptr, &outleft);

      if (rc == (size_t) -1)
        {
          if (errno == EINVAL)
            {
              chn_prepend (inptr, inleft);
              break;
            }
          else
            {
              g_warning ("g_iconv: can't convert: %s", strerror (errno));
              break;
            }
        }
      else
        client_write_console (buffer, sizeof (buffer)-outleft);
    }
}

