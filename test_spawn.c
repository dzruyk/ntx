#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>

#define NARGMAX 127
#define COMMAND_WRAPPER_BIN "cmdwrapper"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

static void
child_watch (GPid pid, gint status, gpointer user_data)
{
  gboolean ok;

  /* Successful program termination is determined by the EXIT_SUCCESS code.
   * Otherwise it is considered to have terminated abnormally.
   */
  if (WIFEXITED (status) && WEXITSTATUS (status) == EXIT_SUCCESS)
    ok = TRUE;
  else
    ok = FALSE;

  g_debug ("client_os_command: child exited %s", ok ? "OK" : "FAIL");
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

  argc = 1;
  argv[0] = COMMAND_WRAPPER_BIN;

  g_strlcpy (buf, cmd, sizeof (buf));

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

  argv[argc] = NULL;

  flags = G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_STDOUT_TO_DEV_NULL;

  err = NULL;
  if (!g_spawn_async (NULL, argv, NULL, flags, NULL, NULL, &pid, &err))
    {
      g_assert (err != NULL);
      g_warning ("client_os_command: can't spawn a child: %s", err->message);
      g_error_free (err);
    }
  else
    {
      guint child_event_id = 0;
      g_debug ("client_os_command: process pid %d spawned", pid);
      g_assert (child_event_id == 0);
      child_event_id = g_child_watch_add (pid, child_watch, NULL);
      g_assert (child_event_id > 0);
    }
}

int
main (int argc, char *argv[])
{
  gtk_init (&argc, &argv);

  client_os_command ("/home/sitkarev/bookman/text2man arg1 arg-2 --args--- lastone");

  gtk_main ();

  return 0;
}
