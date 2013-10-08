#include <glib.h>
#include <gio/gio.h>

#include "fiorw.h"

static volatile sig_atomic_t quit = 0;

void
kick_writer_cb (gpointer user_data)
{
  g_debug ("kick_writer_cb");
}

void
read_data_cb (guchar *buf, gsize len, gpointer user_data)
{
  g_debug ("read_data_cb: %lu bytes", len);

  if (len > 2 && buf[len-1] == '\n')
    {
      if (buf[len-2] == 0x1b)
	{
	  if (buf[0] == '1' && buf[1] != 0x1b)
	    fio_write ("r64\n", 4);
	  else
	    {
	      g_debug ("read_data_cb: done reading! buf[0]=%d", buf[0]);
	      fio_close ();
	      quit = 1;
	    }
	}
      else
	g_warning ("read_data_cb: wrong data recieved %02x %02x len=%d", buf[len-1], buf[len-2], (int)len);
    }
}

void
io_error_cb (gboolean hangup, gpointer user_data)
{
  g_debug ("io_error_cb: hangup=%s", hangup ? "yes" : "no");

  fio_close ();
}

void
coproc_exited_cb (gint pid, gint code, gpointer user_data)
{
  g_debug ("coproc_exited: pid=%d code=%d\n", pid, code);
}

void
sigint (int signo)
{
  quit = 1;
}

int
main (int argc, char *argv[])
{
  GMainContext *main_context;
  FIOCallbacks callbacks;

  g_type_init ();

  signal (SIGINT, sigint);

  main_context = g_main_context_default ();
  g_assert (main_context != NULL);

  callbacks.user_data = NULL;
  callbacks.read_data = read_data_cb;
  callbacks.kick_writer = kick_writer_cb;
  callbacks.coproc_exited = coproc_exited_cb;
  callbacks.io_error = io_error_cb;

  fio_set_callbacks (&callbacks, NULL);

  fio_open_readonly ("/etc/passwd");

  fio_write ("R64\n", 4);

  while (!quit)
    {
      g_main_context_iteration (main_context, TRUE);
    }

  quit = 0;

  fio_open_readonly ("/dev/urandom");

  fio_write ("R64\n", 4);

  while (!quit)
    {
      g_main_context_iteration (main_context, TRUE);
    }

  fio_close ();

  return 0;
}
