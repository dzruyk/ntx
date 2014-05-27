#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>

#include <windows.h>

#include "os.h"

gchar*
os_get_temporary_directory (gchar *buf, gsize bufsz)
{
  gchar dirname[PATH_MAX];
  gchar *tmp;

  if ((tmp = getenv ("TMP")) == NULL && (tmp = getenv ("TEMP")) == NULL)
    {
      int ret;

      ret = GetTempPath (bufsz, buf);
      if (ret != 0)
        return buf;

      if (tmp == NULL)
        {
          g_strlcpy (dirname, OS_DEFAULT_TMP_DIR, sizeof (dirname));
          tmp = dirname;
        }
    }

  g_strlcpy (buf, tmp, bufsz);
  return buf;
}

gboolean
os_process_spawn_with_pipes(gchar **argv,
                         gint *input,
                         gint *output,
                         GPid *child_pid)
{
  GError *err = NULL;
  gboolean ret;
  
  ret = g_spawn_async_with_pipes (NULL, argv, NULL,
        G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, child_pid, input, output, NULL, &err);

  if (ret != TRUE)
    g_warning ("os_process_spawn_with_pipes error %s", err->message);

  return ret;
}

gboolean
os_process_is_exited (GPid pid, gint status)
{
  if (GetExitCodeProcess (pid, &status) && status != STILL_ACTIVE)
    {
      g_warn_if_reached ();
      return TRUE;
    }
    
  return FALSE;
}

gint
os_process_get_exit_status (GPid pid, gint status)
{
  if (GetExitCodeProcess (pid, &status) != TRUE)
    {
      g_warn_if_reached ();
      return EXIT_FAILURE;
    }

  return status;
}

gboolean
os_process_is_signaled (GPid pid, gint status)
{
  return FALSE;
}

gint
os_process_get_signal (GPid pid, gint status)
{
  g_warn_if_reached ();
  return -1;
}

