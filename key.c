#include <gdk/gdkkeysyms.h>
#include <errno.h>
#include <string.h>
#include <iconv.h>

#include "console.h"
#include "internal.h"
#include "chn.h"

enum
{
  ESC = 0x1B
};

#define UTF8_MAX_CHAR    6

#define N_LETTERS        26
#define N_FUNKEYS        27

/* Ctrl+a, Ctrl+b, ..., Ctrl+z */
static const gchar *ctrl_letter_codes[N_LETTERS] =
  {
    "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16",
    "17", "18", "19", "20", "21", "22", "23", "24",
    "25", "26"
  };

/* Alt+a, Alt+b, ..., Alt+z */
static const gchar *alt_letter_codes[N_LETTERS] =
  {
    "37", "52", "50", "39", "28", "40", "41", "276",
    "32", "278", "279", "280", "54", "53", "33", "34",
    "27", "267", "38", "29", "31", "51", "00", "49",
    "30", "277"
  };

/* Function key sequences */
static const gchar *func_codes[N_FUNKEYS] =
  {
    /* Esc, F1, F2, F3, F4, F5, F6, F7 */
    "-1", "88", "89", "90", "91", "92", "93", "94",
    /* F8, F9, F10, F11, F12, Up, Down, Left */
    "95", "96", "97", "98", "99", "100", "101", "102",
    /* Right, Insert, Home, PgUp, PgDown, End, Delete, Tab */
    "103", "81", "82", "83", "84", "85", "76", "9",
    /* Enter, Backspace, Tilda */
    "13", "8", NULL
  };

/* Alt+Function key sequences. */
static const gchar *alt_func_codes[N_FUNKEYS] =
  {
    /* Esc, F1, F2, F3, F4, F5, F6, F7 */
    "+24", "116", "117", "118", "119", "120", "121", "122",
    /* F8, F9, F10, F11, F12, Up, Down, Left */
    "123", "124", "125", "128", "129", "132", "133", "130",
    /* Right, Insert, Home, PgUp, PgDown, End, Delete, Tab */
    "131", "134", "135", "136", "137", "138", "139", "143",
    /* Enter, Backspace, Tilda */
    "+240", "+240", "+240"
  };

/* Ctrl+Function key sequences. */
static const gchar *ctrl_func_codes[N_FUNKEYS] =
  {
    /* Esc, F1, F2, F3, F4, F5, F6, F7 */
    "-1", "58", "59", "60", "61", "62", "63", "64",
    /* F8, F9, F10, F11, F12, Up, Down, Left */
    "65", "66", "67", "68", "69", "79", "80", "78",
    /* Right, Insert, Home, PgUp, PgDown, End, Delete, Tab */
    "77", "70", "71", "72", "73", "74", "75", "141",
    /* Enter, Backspace, Tilda */
    "10", "76", "+240"
  };

/* Shift+Function key sequences. */
static const gchar *shift_func_codes[N_FUNKEYS] =
  {
    /* Esc, F1, F2, F3, F4, F5, F6, F7 */
    "-1", "104", "105", "106", "107", "108", "109", "110",
    /* F8, F9, F10, F11, F12, Up, Down, Left */
    "111", "112", "113", "114", "115", NULL, NULL, NULL,
    /* Right, Insert, Home, PgUp, PgDown, End, Delete, Tab */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    /* Enter, Backspace, Tilda */
    NULL, NULL, NULL
  };

void
key_iconv_send (const gchar *buf, gsize len)
{
  const char *from = "utf8", *to = "cp866";
  iconv_t cd;
  gchar buffer[128];
  gchar *out;
  gsize outlen, nconv;

  if (buf == NULL || len == 0)
    return;

  cd = iconv_open (to, from);
  if (cd == (iconv_t) -1)
    {
      if (errno == EINVAL)
        g_error ("key_send: iconv_open can't convert %s to %s", from, to);
      else
        g_error ("key_send: iconv_open %s", strerror (errno));
      return;
    }

  out = buffer;
  /* Preserve one byte for the terminating ESC. */
  outlen = sizeof (buffer)-1;

  nconv = iconv (cd, (char **)&buf, &len, (char **)&out, &outlen);

  if (nconv == (size_t) -1)
    {
      if (errno == EILSEQ)
        g_error ("key_send: iconv invalid byte sequence");
      else
        g_error ("key_send: iconv %s", strerror (errno));
    }
  else
    {
      *out++ = ESC;
      outlen = out - buffer;

      chn_write (buffer, outlen);
    }

  iconv_close (cd);
}

/* This helper translates the function key + modifier to the character
 * sequence. The pointer returned in res_buf points to a statically
 * allocated memory. You need not to g_free() it.
 */
static gboolean
key_to_sequence (guint modifier_key, guint keyval, const gchar **res_buf, gsize *res_len)
{
  const gchar *str;
  guint len;

  static const gint mod_to_index[] =
    {
      0, GDK_CONTROL_MASK, GDK_MOD1_MASK, GDK_SHIFT_MASK
    };
  static const gchar **codetabs[] =
    {
      func_codes, ctrl_func_codes, alt_func_codes, shift_func_codes
    };
  static const gint keyvaltab[] =
    {
      GDK_Escape, GDK_F1, GDK_F2, GDK_F3, GDK_F4, GDK_F5,
      GDK_F6, GDK_F7, GDK_F8, GDK_F9, GDK_F10, GDK_F11,
      GDK_F12, GDK_Up, GDK_KP_Up, GDK_Down, GDK_KP_Down, GDK_Left,
      GDK_KP_Left, GDK_Right, GDK_KP_Right, GDK_Insert, GDK_KP_Insert, GDK_Home,
      GDK_KP_Home, GDK_Page_Up, GDK_KP_Page_Up, GDK_Page_Down, GDK_KP_Page_Down, GDK_End,
      GDK_KP_End, GDK_Delete, GDK_KP_Delete, GDK_Tab, GDK_Return, GDK_KP_Enter,
      GDK_BackSpace
    };
  static const gint index_to_codes[] =
    {
      0,  1,  2,  3,  4,  5,
      6,  7,  8,  9,  10, 11,
      12, 13, 13, 14, 14, 15,
      15, 16, 16, 17, 17, 18,
      18, 19, 19, 20, 20, 21,
      21, 22, 22, 23, 24, 24,
      25
    };
  gint i;

  str = NULL;
  len = 0;

  for (i = 0; i < G_N_ELEMENTS (mod_to_index); i++)
    {
      if (modifier_key == mod_to_index[i])
        {
          const gchar **table = codetabs[i];
          g_assert (table != NULL);
          for (i = 0; i < G_N_ELEMENTS (keyvaltab); i++)
            {
              if (keyvaltab[i] == keyval)
                {
                  gint idx = index_to_codes[i];
                  g_assert (idx >= 0 && idx < N_FUNKEYS);
                  if (table[idx] != NULL)
                    {
                      len = strlen (table[idx]);
                      str = table[idx];
                    }
                  break;
                }
            }
          break;
        }
    }

  if (len > 0)
    {
      if (res_len != NULL)
        *res_len = len;
      if (res_buf != NULL)
        *res_buf = str;
      return TRUE;
    }
  else
    return FALSE;
}

void
key_send_code (gint keycode)
{
  const gchar *buf;
  gsize len;

  if (key_to_sequence (0, keycode, &buf, &len))
    {
      g_assert (len > 0 && buf != NULL);
      key_iconv_send (buf, len);
    }
  else
    g_warn_if_reached ();
}

void
key_send (const GdkEventKey *event)
{
  gchar tmp[64];
  const gchar *str;
  gunichar uc;
  guint modifiers;
  gsize len;

  g_assert (event != NULL);

  str = NULL;
  len = 0;

  modifiers = gtk_accelerator_get_default_mod_mask ();

  uc = gdk_keyval_to_unicode (event->keyval);

  if (uc > 0
      && ((event->state & modifiers) == 0
          || (event->state & modifiers) == GDK_SHIFT_MASK))
    {
      tmp[0] = '+';
      len = g_unichar_to_utf8 (uc, tmp+1) + 1;
      g_assert (len > 0 && len < sizeof (tmp));
      tmp[len] = '\0';
      str = tmp;
    }
  else
    {
      if (uc > 0)
        {
          const gchar **table;
          guint keyval;
          gint idx;

          if ((event->state & modifiers) == GDK_CONTROL_MASK)
            table = ctrl_letter_codes;
          else if ((event->state & modifiers) == GDK_MOD1_MASK)
            table = alt_letter_codes;
          else
            table = NULL;

          if (table != NULL)
            {

              gdk_keymap_translate_keyboard_state (NULL, event->hardware_keycode,
                                                   event->state, 0,
                                                   &keyval, NULL, NULL, NULL);

              idx = gdk_keyval_to_unicode (keyval) - 'a';

              if (idx >= 0 && idx < N_LETTERS)
                {
                  if (table[idx] != NULL)
                    {
                      len = strlen (table[idx]);
                      str = table[idx];
                    }
                }
            }
        }
      else
        key_to_sequence (event->state & modifiers, event->keyval, &str, &len);
    }

  if (len > 0)
    key_iconv_send (str, len);
}

