#include <gdk/gdkkeysyms-compat.h>
#include <glib.h>
#include <cairo/cairo.h>
#include <ft2build.h>
#include <math.h>
#include <string.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "console.h"
#include "console_marshal.h"
#include "colors.h"
#include "fc.h"

/* ASCII control characters treated specially by console window.
 */
#define ASCII_NUL        0x00
#define ASCII_LF         0x0A
#define ASCII_CR         0x0D
#define ASCII_BEL        0x07
#define ASCII_BS         0x08
#define ASCII_ESC        0x1B
#define ASCII_DEL        0x7F
#define ASCII_HT         0x09
#define ASCII_VT         0x0B
#define ASCII_FF         0x0C

#define LEFT_MOUSE_BUTTON 1
#define MIDDLE_MOUSE_BUTTON 2
#define RIGHT_MOUSE_BUTTON 3

/* Enumeration of the console widget property ids.
 */
typedef enum
{
  PROP_0,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_FONT_FAMILY,
  PROP_FONT_STYLE,
  PROP_FONT_SIZE,
  PROP_CURSOR_SHAPE,
  PROP_CURSOR_TIMER
} ConsolePropertyId;

/* Enumeration of the console property change mask.
 */
typedef enum
{
  PROP_WIDTH_MASK       = 1 << 0,
  PROP_HEIGHT_MASK      = 1 << 1,
  PROP_FONT_FAMILY_MASK = 1 << 2,
  PROP_FONT_STYLE_MASK  = 1 << 3,
  PROP_FONT_SIZE_MASK   = 1 << 4
} ConsolePropertyMask;

enum {
  PRIMARY_TEXT_SELECTED,
  PRIMARY_TEXT_PASTED,
  CLIPBOARD_TEXT_SELECTED,
  CLIPBOARD_TEXT_PASTED,
  LAST_SIGNAL,
};

static guint        console_signals[LAST_SIGNAL] = { 0 };

/* This macro rounds up x to multiples of a.
 */
#define ROUND_UP(x, a) ((((x) + 1) / (a)) * (a))

/* Console widget property constants.
 */
#define CONSOLE_WIDTH_MIN       1
#define CONSOLE_WIDTH_MAX       1024
#define CONSOLE_WIDTH_DEFAULT   80

#define CONSOLE_HEIGHT_MIN      1
#define CONSOLE_HEIGHT_MAX      1024
#define CONSOLE_HEIGHT_DEFAULT  24

#define FONT_FAMILY_DEFAULT     "Andale Mono"
#define FONT_STYLE_DEFAULT      "normal"
#define FONT_SIZE_DEFAULT       12

/* GDK color scale to convert to cairo colorspace.
 */
#define GDK_COLOR_SCALE         65535.0

/* Maximum length of a UTF-8 encoded character.
 */
#define UTF8_CHAR_LEN_MAX       6

/* Size of console tab bitmap table.
 */
#define TABMAP_SIZE             8

/* Default cursor blinking timer.
 */
#define CURSOR_BLINKING_TIMER   250

typedef struct _ConsoleColor
{
  double red;
  double green;
  double blue;
} ConsoleColor;

typedef enum
{
  CONSOLE_CHAR_ATTR_DEFAULT,
  CONSOLE_CHAR_ATTR_UNDERSCORE,
  CONSOLE_CHAR_ATTR_BLINK,
  CONSOLE_CHAR_ATTR_REVERSE
} ConsoleCharAttr;

typedef struct {
  gdouble x1;                   /* start x coordinate in pixels */
  gdouble y1;                   /* start y coordinate in pixels */
  gdouble x2;                   /* stop x coordinate in pixels */
  gdouble y2;                   /* stop y coordinate in pixels */
} ConsoleTextSelection;

typedef struct _ConsoleChar
{
  gunichar chr;                 /* unicode symbol */
  ConsoleCharAttr attr;         /* character attributes */
  ConsoleColor color;           /* foreground character color */
  ConsoleColor bg_color;        /* background character color */
} ConsoleChar;

/* Private structure for a console widget instance.
 */
struct _ConsolePrivate {

  /* properties
   */
  gint width;                   /* screen width in characters */
  gint height;                  /* screen height in characters */

  ConsoleChar *scr;             /* console screen buffer */

  gint char_width;              /* character width in pixels */
  gint char_height;             /* character height in pixels */
  gint baseline;                /* baseline position in pixels */

  /* font rendering properties and structures
   */
  gchar *font_file;             /* path to font file */
  gchar *font_family;           /* name of the font family (`Courer New', `Sans Mono', etc) */
  gchar *font_style;            /* name of the font family (`italic', 'roman', etc) */
  gint font_size;               /* font size in points (1/72 inches) */
  gint face_index;              /* font face index used by FT_Face_New */

  FT_Library ftlib;             /* FreeType library instance */
  FTC_Manager manager;          /* FreeType cache manager */
  FTC_CMapCache cmapcache;      /* character map cache */
  FTC_SBitCache sbitcache;      /* small bitmap cache */

  /* state variables
   */
  gint cursor_x;                /* cursor x coordinate */
  gint cursor_y;                /* cursor y coordinate */
  gint cursor_shape;            /* cursor appearance */
  gboolean cursor_toggle;       /* cursor on/off blinker  */
  guint cursor_timer_id;        /* cursor blink timer */
  ConsoleColor color;           /* character foreground color */
  ConsoleColor bg_color;        /* character background color */
  ConsoleCharAttr attr;         /* character attributes */

  ConsoleTextSelection text_selection; /* text selection area structure */

  /* horizontal TAB position bitmap
   */
  guint32 tabs[TABMAP_SIZE];

};

#define CONSOLE_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), CONSOLE_TYPE, ConsolePrivate))

static void     console_class_init              (ConsoleClass   *klass);
static void     console_init                    (Console        *console);
static void     console_size_request            ();
static void     console_size_allocate           ();
static void     console_realize                 ();
static gboolean console_expose                  ();
static void     console_draw                    (GtkWidget      *widget,
                                                 GdkEventExpose *event);
static void     console_set_property            (GObject        *object,
                                                 guint           prop_id,
                                                 const GValue   *value,
                                                 GParamSpec     *pspec);
static void     console_get_property            (GObject        *object,
                                                 guint           prop_id,
                                                 GValue         *value,
                                                 GParamSpec     *pspec);
static void     console_finalize                (GObject        *object);

static void     invalidate_char_rect            (Console        *console,
                                                 gint            x,
                                                 gint            y);
static void     invalidate_cursor_rect          (Console        *console);
static gboolean console_cursor_timer            (gpointer        user_data);
static gboolean console_primary_text_selected   (Console        *console,
                                                 const gchar    *str);
static gboolean console_clipboard_text_selected (Console        *console,
                                                 const gchar    *str);
static GString* get_selected_text               (Console        *console);

static gboolean
primary_try_get_pasted_text (Console *console)
{
  GtkClipboard *clipboard;
  gchar *s;
  gboolean ret;

  clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);

  s = gtk_clipboard_wait_for_text (clipboard);
  if (s == NULL)
    return FALSE;

  g_debug ("get \"%s\" from primary", s);

  g_signal_emit (console, console_signals[PRIMARY_TEXT_PASTED], 0, s, &ret);

  g_free (s);

  return TRUE;
}

static gboolean
clipboard_try_get_pasted_text (Console *console)
{
  GtkClipboard *clipboard;
  gchar *s;
  gboolean ret;

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  s = gtk_clipboard_wait_for_text (clipboard);
  if (s == NULL)
    return FALSE;

  g_debug ("get \"%s\" from clipboard", s);

  g_signal_emit (console, console_signals[CLIPBOARD_TEXT_PASTED], 0, s, &ret);

  g_free (s);

  return TRUE;
}

static gboolean
console_button_press_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  Console *console = (Console *) widget;
  ConsoleTextSelection *cs = &console->priv->text_selection;
  guint modifiers;

  g_assert (widget == user_data);

  modifiers = gtk_accelerator_get_default_mod_mask ();

  if (event->button == LEFT_MOUSE_BUTTON && (event->state & modifiers) == GDK_CONTROL_MASK)
    {
      if (cs->x1 != -1 || cs->y1 != -1
          || cs->x2 != -1 || cs->y2 != -1)
        return FALSE;

      g_debug ("start selection at (x1, y1) (%f, %f)", event->x, event->y);

      cs->x1 = cs->x2 = event->x;
      cs->y1 = cs->y2 = event->y;
      gtk_widget_queue_draw (widget);
    }
  if (event->button == MIDDLE_MOUSE_BUTTON)
    {
      g_debug (" paste action");

      return primary_try_get_pasted_text (console);
    }

  return FALSE;
}

static gboolean
console_key_press_event_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  Console *console = (Console *) widget;
  GdkKeymap *keymap;
  gint lvl;
  guint kv;

  g_assert (widget == user_data);

  keymap = gdk_keymap_get_default();

  gdk_keymap_translate_keyboard_state(keymap, event->hardware_keycode, event->state, 0, &kv, NULL, &lvl, NULL);

  if (kv == GDK_V &&
      (event->state & GDK_CONTROL_MASK) != 0)
    {
      g_debug("ctrl+shift+v pressed");
      return clipboard_try_get_pasted_text (console);
    }
  else if (kv == GDK_C &&
           (event->state & GDK_CONTROL_MASK) != 0)
    {
      gboolean ret;
      GString *s;

      g_debug("ctrl+shift+c pressed");
      s = get_selected_text (console);

      g_signal_emit (console, console_signals[CLIPBOARD_TEXT_SELECTED], 0, s->str, &ret);
      gtk_widget_queue_draw (widget);

      g_string_free (s, TRUE);
      return FALSE;
    }
  
  return FALSE;
}

#define SWAP_IF(cond, a, b) G_STMT_START { \
  if (cond)         \
    {               \
      int tmp;      \
      tmp = a;      \
      a = b;        \
      b = tmp;      \
    }               \
  } G_STMT_END

static GString*
get_selected_text(Console *console)
{
  ConsoleTextSelection *cs = &console->priv->text_selection;
  GString *res;
  gint x1, y1;
  gint x2, y2;
  gint char_width, char_height;
  gint i, j;

  g_assert (cs->x1 != -1 && cs->y1 != -1 &&
            cs->x2 != -1 && cs->y2 != -1);

  res = g_string_new ("");

  char_width = console->priv->char_width;
  char_height = console->priv->char_height;

  x1 = cs->x1 / char_width;
  y1 = cs->y1 / char_height;
  x2 = cs->x2 / char_width;
  y2 = cs->y2 / char_height;

  SWAP_IF (x1 > x2, x1, x2);
  SWAP_IF (y1 > y2, y1, y2);
  x2 = MIN (x2, console->priv->width);
  y2 = MIN (y2, console->priv->height);

  g_debug ("selected text coords (%d, %d) (%d, %d)", x1, y1, x2, y2);

  for (i = y1; i <= y2; i++)
    {
      for (j = x1; j <= x2; j++)
        {
          int coord;

          coord = i * console->priv->width + j;
          g_string_append_unichar (res, console->priv->scr[coord].chr);

        }
      if (i < y2)
        {
          /* if it's not last string */
          g_string_append (res, "\n");
        }
    }

  return res;
}

static gboolean
console_button_release_event_cb (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  Console *console = (Console *) widget;
  ConsoleTextSelection *cs = &console->priv->text_selection;

  g_assert (widget == user_data);

  if (event->button == LEFT_MOUSE_BUTTON &&
      cs->x1 != -1 && cs->y1 != -1)
    {
      gboolean ret;
      GString *s;
      /* end of selection */

      g_debug ("button_release_event_cb (x2, y2) (%f, %f)", event->x, event->y);

      s = get_selected_text (console);

      g_signal_emit (console, console_signals[PRIMARY_TEXT_SELECTED], 0, s->str, &ret);
      gtk_widget_queue_draw (widget);

      cs->x1 = cs->x2 = -1;
      cs->y1 = cs->y2 = -1;

      g_string_free (s, TRUE);
    }

  return TRUE;
}

static gboolean
console_motion_notify_event_cb (GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  Console *console = (Console *) widget;
  ConsoleTextSelection *cs = &console->priv->text_selection;

  g_assert (widget == user_data);

  if (cs->x1 != -1 && cs->y1 != -1)
    {
      g_debug ("motion_notify_event_cb (x2, y2) (%f, %f)", event->x, event->y);

      if (event->x >= 0)
        cs->x2 = event->x;
      if (event->y >= 0)
        cs->y2 = event->y;

      gtk_widget_queue_draw (widget);
    }

  return TRUE;
}

/* This helper allocates a font face and is called only by
 * the cache manager when needed. It's purpose is to let
 * cache manager to be system dependencies aware.
 */
static FT_Error
console_face_requester (FTC_FaceID id, FT_Library lib, FT_Pointer data, FT_Face *aface)
{
  ConsolePrivate *priv;
  FT_Face face;
  FT_Error error;

  g_assert (lib != NULL && id != NULL);
  g_assert (aface != NULL);

  *aface = NULL;

  /* Face ID is a ConsolePrivate structure passed to cache lookup routines as
   * a parameter.
   */
  priv = (ConsolePrivate *) id;

  g_assert (priv->face_index >= 0);
  g_assert (priv->font_file != NULL);
  g_assert (priv->ftlib == lib);

  g_debug ("requesting font file '%s' face index %d", priv->font_file, priv->face_index);

  error = FT_New_Face (lib, priv->font_file, priv->face_index, &face);

  if (error == 0)
    *aface = face;

  return error;
}

/* This helper initializes FreeType cache and library.
 */
static void
console_font_cache_init (ConsolePrivate *priv)
{
  FT_Library lib;
  FTC_Manager manager;
  FTC_SBitCache sbitcache;
  FTC_CMapCache cmapcache;
  gint error;

  g_assert (priv != NULL);

  error = FT_Init_FreeType (&lib);
  if (error)
    g_error ("can't init freetype library");

  error = FTC_Manager_New (lib, 0, 0, 0, console_face_requester, NULL, &manager);
  if (error)
    g_error ("can't create cache manager");

  error = FTC_CMapCache_New (manager, &cmapcache);
  if (error)
    g_error ("can't create cmap cache");

  error = FTC_SBitCache_New (manager, &sbitcache);
  if (error)
    g_error ("can't create sbit cache");

  priv->ftlib = lib;
  priv->manager = manager;
  priv->sbitcache = sbitcache;
  priv->cmapcache = cmapcache;
}

/* This helper resets the cache and related data. It is invoked
 * each time the console font is changed.
 */
static void
console_font_cache_reset (ConsolePrivate *priv)
{
  g_assert (priv != NULL);
  g_assert (priv->manager != NULL);

  g_debug ("reseting font cache...");

  FTC_Manager_RemoveFaceID (priv->manager, priv);
  FTC_Manager_Reset (priv->manager);
}

/* This helper deinitializes FreeType library and deallocates the cache.
 */
static void
console_font_cache_deinit (ConsolePrivate *priv)
{
  g_assert (priv != NULL);

  /* Cache manager owns all other caches. Destroying the manager
   * destroys other caches as well.
   */
  if (priv->manager != NULL)
    FTC_Manager_Done (priv->manager);

  if (priv->ftlib != NULL)
    FT_Done_FreeType (priv->ftlib);

  priv->manager = NULL;
  priv->ftlib = NULL;

  /* We are safe to NULLify pointers here because cache manager
   * destruction was responsible to free them for us.
   */
  priv->cmapcache = NULL;
  priv->sbitcache = NULL;
}

GType
console_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    {
      static const GTypeInfo info =
        {
          sizeof (ConsoleClass),
          NULL,                        /* base_init */
          NULL,                        /* base_finalize */
          (GClassInitFunc) console_class_init,        /* class_init */
          NULL,                        /* class_finalize */
          NULL,                        /* class_data */
          sizeof(Console),
          0,                        /* n_preallocs */
          (GInstanceInitFunc) console_init        /* instance_init */
        };
      type = g_type_register_static (GTK_TYPE_WIDGET,
                                     "Console",
                                     &info, 0);
    }

  return type;
}

/* Public functions available to user.
 */

GtkWidget*
console_new()
{
  return GTK_WIDGET (g_object_new (console_get_type (), NULL));
}

GtkWidget*
console_new_with_size (gint width, gint height)
{
  return GTK_WIDGET (g_object_new (console_get_type (), "width", width, "height", height, NULL));
}

/* This function sets up class structure, properties and registers signals. */
static void
console_class_init (ConsoleClass *klass)
{
  GtkWidgetClass *widget_class;
  GObjectClass *object_class;

  g_debug ("console: class init");

  widget_class = (GtkWidgetClass *) klass;
  object_class = (GObjectClass *) klass;

  widget_class->realize = console_realize;
  widget_class->size_request = console_size_request;
  widget_class->size_allocate = console_size_allocate;
  widget_class->expose_event = console_expose;

  object_class->set_property = console_set_property;
  object_class->get_property = console_get_property;

  g_type_class_add_private (object_class, sizeof (ConsolePrivate));

  g_object_class_install_property (object_class, PROP_WIDTH,
                                   g_param_spec_int ("width",
                                                     "Console Window Width",
                                                     "The width of the console in characters",
                                                     CONSOLE_WIDTH_MIN, CONSOLE_WIDTH_MAX,
                                                     CONSOLE_WIDTH_DEFAULT,
                                                     G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_HEIGHT,
                                   g_param_spec_int ("height",
                                                     "Console Window Height",
                                                     "The height of the console in characters",
                                                     CONSOLE_HEIGHT_MIN, CONSOLE_HEIGHT_MAX,
                                                     CONSOLE_HEIGHT_DEFAULT,
                                                     G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_FONT_SIZE,
                                   g_param_spec_int ("font-size",
                                                     "Console Window Font Size",
                                                     "The size of the console font in points",
                                                     6, 72, FONT_SIZE_DEFAULT,
                                                     G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_FONT_FAMILY,
                                   g_param_spec_string ("font-family",
                                                        "Console Font Family",
                                                        "The font family name",
                                                        FONT_FAMILY_DEFAULT,
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_FONT_STYLE,
                                   g_param_spec_string ("font-style",
                                                        "Console Font Style",
                                                        "The font style name",
                                                        FONT_STYLE_DEFAULT,
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_CURSOR_SHAPE,
                                   g_param_spec_int ("cursor-shape",
                                                     "Console Cursor Shape",
                                                     "The shape of the console cursor",
                                                     CONSOLE_CURSOR_DEFAULT, CONSOLE_CURSOR_VERT_HALF,
                                                     CONSOLE_CURSOR_DEFAULT,
                                                     G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_CURSOR_TIMER,
                                   g_param_spec_int ("cursor-timer",
                                                     "Console Cursor Timer",
                                                     "This sets the blinking timer for the cursor",
                                                     CONSOLE_BLINK_STEADY, CONSOLE_BLINK_FAST,
                                                     CONSOLE_BLINK_MEDIUM,
                                                     G_PARAM_READWRITE));
  klass->primary_text_pasted = NULL;
  klass->primary_text_selected = console_primary_text_selected;
  klass->clipboard_text_pasted = NULL;
  klass->clipboard_text_selected = console_clipboard_text_selected;
  console_signals[PRIMARY_TEXT_SELECTED] =
    g_signal_new ("primary-text-selected",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ConsoleClass, primary_text_selected),
                  g_signal_accumulator_true_handled,
                  NULL,
                  g_cclosure_console_BOOLEAN__STRING,
                  G_TYPE_BOOLEAN, 1,
                  G_TYPE_STRING);
  console_signals[PRIMARY_TEXT_PASTED] =
    g_signal_new ("primary-text-pasted",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ConsoleClass, primary_text_pasted),
                  g_signal_accumulator_true_handled,
                  NULL,
                  g_cclosure_console_BOOLEAN__STRING,
                  G_TYPE_BOOLEAN, 1,
                  G_TYPE_STRING);

  console_signals[CLIPBOARD_TEXT_SELECTED] =
    g_signal_new ("clipboard-text-selected",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ConsoleClass, clipboard_text_selected),
                  g_signal_accumulator_true_handled,
                  NULL,
                  g_cclosure_console_BOOLEAN__STRING,
                  G_TYPE_BOOLEAN, 1,
                  G_TYPE_STRING);
  console_signals[CLIPBOARD_TEXT_PASTED] =
    g_signal_new ("clipboard-text-pasted",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ConsoleClass, clipboard_text_pasted),
                  g_signal_accumulator_true_handled,
                  NULL,
                  g_cclosure_console_BOOLEAN__STRING,
                  G_TYPE_BOOLEAN, 1,
                  G_TYPE_STRING);

  object_class->finalize = console_finalize;
}

static void
color_parse (ConsoleColor *color, const char *spec)
{
  GdkColor gdkcolor;

  g_assert (spec != NULL);

  gdk_color_parse (spec, &gdkcolor);

  color->red = gdkcolor.red / GDK_COLOR_SCALE;
  color->green = gdkcolor.green / GDK_COLOR_SCALE;
  color->blue = gdkcolor.blue / GDK_COLOR_SCALE;
}

/* This helper resizes the screen to a new width and height.
 */
static void
resize_screen (Console *console, gint width, gint height)
{
  ConsolePrivate *priv;
  ConsoleChar *old_scr;
  ConsoleChar *scr;
  guint alloc_size;
  gint old_width, old_height;
  gint i, j;

  g_assert (console != NULL);
  g_assert (width > 0 && height > 0);

  priv = console->priv;

  old_scr = priv->scr;
  old_width = priv->width;
  old_height = priv->height;

  alloc_size = width * height;
  scr = g_malloc0 (alloc_size * sizeof (ConsoleChar));

  for (i = 0; i < height; i++)
    {
      for (j = 0; j < width; j++)
        {
          /* Try to relocate contents of the old screen to the new one.
           */
          if (old_scr != NULL &&
              (i < priv->height && j < priv->width))
            {
              ConsoleChar *dst, *src;

              dst = scr + i*width + j;
              src = old_scr + i*old_width + j;

              *dst = *src;
            }
          else
            {
              ConsoleChar *chr;

              chr = scr + i*width + j;
              chr->chr = ' ';
              chr->color = priv->color;
              chr->bg_color = priv->bg_color;
              chr->attr = CONSOLE_CHAR_ATTR_DEFAULT;
            }
        }
    }

  priv->scr = scr;
  priv->width = width;
  priv->height = height;

  /* correct cursor position */
  if (priv->cursor_x >= width)
    priv->cursor_x = width - 1;
  if (priv->cursor_y >= height)
    priv->cursor_y = height - 1;

  if (old_scr != NULL)
    g_free (old_scr);
}

static void
console_init (Console *console)
{
  ConsolePrivate *priv;
  gint i, tid;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE(console));

  g_debug ("console: init");

  priv = CONSOLE_GET_PRIVATE (console);
  console->priv = priv;

  /* set initial cursor position to the top left corner */
  priv->cursor_x = 0;
  priv->cursor_y = 0;
  priv->cursor_shape = CONSOLE_CURSOR_DEFAULT;
  priv->cursor_toggle = FALSE;
  /* start cursor blinking timer */
  tid = g_timeout_add (CURSOR_BLINKING_TIMER, (GSourceFunc) console_cursor_timer, console);
  priv->cursor_timer_id = tid;

  /* initialize default colors for the console characters */
  color_parse (&priv->color, COLOR_BASE0);
  color_parse (&priv->bg_color, COLOR_BASE03);

  /* initialize default font */
  priv->font_family = g_strdup (FONT_FAMILY_DEFAULT);
  priv->font_style = g_strdup (FONT_STYLE_DEFAULT);
  priv->face_index = 0;
  priv->font_size = FONT_SIZE_DEFAULT;
  priv->font_file = NULL;

  /* initialize FreeType backend */
  console_font_cache_init (priv);

  priv->char_width = -1;
  priv->char_height = -1;
  priv->baseline = -1;

  /* initial selection
   */
  priv->text_selection.x1 = -1;
  priv->text_selection.y1 = -1;
  priv->text_selection.x2 = -1;
  priv->text_selection.y2 = -1;

  /* set default TAB position each 8 characters */
  for (i = 0; i < TABMAP_SIZE; i++)
    priv->tabs[i] = 0x01010101;

  /* callbacks */
  g_signal_connect (GTK_WIDGET (console), "button-press-event", G_CALLBACK (console_button_press_event_cb), console);
  g_signal_connect (GTK_WIDGET (console), "key-press-event", G_CALLBACK (console_key_press_event_cb), console);
  g_signal_connect (GTK_WIDGET (console), "button-release-event", G_CALLBACK (console_button_release_event_cb), console);
  g_signal_connect (GTK_WIDGET (console), "motion-notify-event", G_CALLBACK (console_motion_notify_event_cb), console);

  /* allocate console screen buffer */
  resize_screen (console, CONSOLE_WIDTH_DEFAULT, CONSOLE_HEIGHT_DEFAULT);
}

static gboolean
console_primary_text_selected (Console *console, const gchar *str)
{
  GtkClipboard *clipboard;

  clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);
  gtk_clipboard_set_text (clipboard, str, strlen(str));

  return TRUE;
}

static gboolean
console_clipboard_text_selected (Console *console, const gchar *str)
{
  GtkClipboard *clipboard;

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_text (clipboard, str, strlen(str));

  return TRUE;
}


static void
console_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
  GdkGeometry hints;
  GtkWidget *toplevel;
  Console *console;
  ConsolePrivate *priv;
  GdkScreen *screen;
  FT_Face face;
  double dpi_x, dpi_y, scale_x, scale_y;
  gint face_index, error;
  gint char_width, char_height, baseline;
  gchar *file;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_CONSOLE(widget));
  g_return_if_fail (requisition != NULL);

  console = CONSOLE (widget);
  priv = console->priv;

  g_debug ("console: size request width %d height %d", priv->width, priv->height);

  /* Get screen resolution in dots per inch. */
  screen = gtk_widget_get_screen (widget);
  dpi_x = gdk_screen_get_resolution (screen);
  dpi_y = dpi_x;

  fc_get_font_file (priv->font_family ? priv->font_family : FONT_FAMILY_DEFAULT,
                    priv->font_style ? priv->font_style : FONT_STYLE_DEFAULT,
                    TRUE, TRUE, &file, &face_index);

  priv->face_index = face_index;

  g_assert (file != NULL && face_index >= 0);

  if (priv->font_file != NULL)
    g_free (priv->font_file);

  priv->font_file = file;

  /* Before looking up font metrics, reset the cache, because we don't need
   * it's contents any more for sure. Resize is requested when the font it
   * changed, so we need to drop previous cached items either.
   */
  console_font_cache_reset (priv);

  /* Lookup face to determine font metrics. */
  error = FTC_Manager_LookupFace (priv->manager, (FTC_FaceID) priv, &face);
  if (error != 0)
    g_error ("can't lookup face in the cache");

  /* Scale metrics using screen dpi and units per EM. */
  scale_x = (priv->font_size * dpi_x / 72.0) / (double) face->units_per_EM;
  scale_y = (priv->font_size * dpi_y / 72.0) / (double) face->units_per_EM;

  g_debug ("scale_x = %f scale_y = %f", scale_x, scale_y);
  g_debug ("ascender %d descender %d underline %d max_advance_width %d",
           face->ascender >> 6, face->descender >> 6,
           face->underline_position >> 6, face->max_advance_width >> 6);

  /* Calculate font height, width and baseline position (in pixels). */
  if (face->bbox.xMin < 0)
    char_width = face->bbox.xMax * scale_x + 0.5;
  else
    char_width = (face->bbox.xMax + face->bbox.xMin) * scale_x + 0.5;

  char_height = (face->bbox.yMax - face->bbox.yMin) * scale_y + 0.5;
  if (char_height < 0)
    char_height = face->bbox.yMax * scale_y + 0.5;

  baseline = face->bbox.yMax * scale_y;

  g_assert (baseline >= 0);

  priv->char_width = char_width;
  priv->char_height = char_height;
  priv->baseline = baseline;

  g_debug ("xMin %d xMax %d yMax %d yMin %d", (int)face->bbox.xMin, (int)face->bbox.xMax, (int)face->bbox.yMax, (int)face->bbox.yMin);
  g_debug ("char_width %d, char_height %d, baseline %d", char_width, char_height, baseline);

  /* Calculate console window size required. */
  requisition->width = priv->char_width * priv->width;
  requisition->height = priv->char_height * priv->height;

  /* Do the canonical terminal window geometry hints to resize properly. */
  toplevel = gtk_widget_get_toplevel (widget);

  if (GTK_WIDGET_TOPLEVEL (toplevel))
    {
      hints.width_inc = priv->char_width;
      hints.height_inc = priv->char_height;
      hints.base_width = priv->char_width;
      hints.base_height = priv->char_height;
      hints.min_width = priv->char_width;
      hints.min_height = priv->char_height;

      gtk_window_set_geometry_hints (GTK_WINDOW (toplevel),
                                     GTK_WIDGET (widget),
                                     &hints,
                                     GDK_HINT_MIN_SIZE |
                                     GDK_HINT_BASE_SIZE |
                                     GDK_HINT_RESIZE_INC);
    }
}


static void
console_size_allocate (GtkWidget *widget,
                           GtkAllocation *allocation)
{
  gint width, height;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_CONSOLE(widget));
  g_return_if_fail (allocation != NULL);

  widget->allocation = *allocation;

  g_debug ("console: allocate x=%d y=%d width=%d height=%d",
           allocation->x, allocation->y,
           allocation->width, allocation->height);

  if (GTK_WIDGET_REALIZED (widget))
    {
      allocation->width = ROUND_UP(allocation->width, CONSOLE(widget)->priv->char_width);
      allocation->height = ROUND_UP(allocation->height, CONSOLE(widget)->priv->char_height);

      /* Resize widget window and move to allocated position
       * relative to it's parent window.
       */

      gdk_window_move_resize (widget->window,
                              allocation->x, allocation->y,
                              allocation->width, allocation->height);

      width = allocation->width / CONSOLE(widget)->priv->char_width;
      height = allocation->height / CONSOLE(widget)->priv->char_height;

      resize_screen (CONSOLE(widget), width, height);
    }
}

static void
console_realize (GtkWidget *widget)
{
  ConsolePrivate *priv;
  GdkWindowAttr attr;
  GdkColor color;
  guint attr_mask, event_mask;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_CONSOLE(widget));

  priv = CONSOLE (widget)->priv;

  g_debug ("console: realize");

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED | GTK_DOUBLE_BUFFERED | GTK_CAN_FOCUS);

  attr.window_type = GDK_WINDOW_CHILD;
  attr.x = widget->allocation.x;
  attr.y = widget->allocation.y;
  attr.width = widget->allocation.width;
  attr.height = widget->allocation.height;

  attr.wclass = GDK_INPUT_OUTPUT;

  event_mask = GDK_EXPOSURE_MASK | GDK_KEY_PRESS_MASK | GDK_BUTTON_PRESS_MASK |
               GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK;
  attr.event_mask = gtk_widget_get_events (widget) | event_mask;

  attr_mask = GDK_WA_X | GDK_WA_Y;

  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attr, attr_mask);

  gdk_window_set_user_data (widget->window, widget);

  gdk_color_parse (COLOR_BASE02, &color);
  gdk_rgb_find_color (gtk_widget_get_colormap (widget), &color);
  gdk_window_set_background (widget->window, &color);

 // gdk_window_set_back_pixmap (widget->window, NULL, TRUE);

  widget->style = gtk_style_attach (widget->style, widget->window);
  //gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);
}

void
console_get_foreground_color (Console *console, GdkColor *color)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (color != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  color->red = console->priv->color.red * GDK_COLOR_SCALE;
  color->green = console->priv->color.green * GDK_COLOR_SCALE;
  color->blue = console->priv->color.blue * GDK_COLOR_SCALE;
}

void
console_get_background_color (Console *console, GdkColor *color)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (color != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  color->red = console->priv->bg_color.red * GDK_COLOR_SCALE;
  color->green = console->priv->bg_color.green * GDK_COLOR_SCALE;
  color->blue = console->priv->bg_color.blue * GDK_COLOR_SCALE;
}

void
console_set_foreground_color (Console *console, const GdkColor *color)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (color != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  console->priv->color.red = color->red / GDK_COLOR_SCALE;
  console->priv->color.green = color->green / GDK_COLOR_SCALE;
  console->priv->color.blue = color->blue / GDK_COLOR_SCALE;
}

void
console_set_background_color (Console *console, const GdkColor *color)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (color != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  console->priv->bg_color.red = color->red / GDK_COLOR_SCALE;
  console->priv->bg_color.green = color->green / GDK_COLOR_SCALE;
  console->priv->bg_color.blue = color->blue / GDK_COLOR_SCALE;
}

void
console_set_foreground_color_from_string (Console *console, const gchar *color)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (color != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  color_parse (&console->priv->color, color);
}

void
console_set_background_color_from_string (Console *console, const gchar *color)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (color != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  color_parse (&console->priv->bg_color, color);
}

void
console_set_height (Console *console, gint height)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));
  g_return_if_fail (height > 0);

  resize_screen (console, console->priv->width, height);
  console->priv->height = height;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    {
      gtk_widget_queue_resize (GTK_WIDGET (console));
      gtk_widget_queue_draw (GTK_WIDGET (console));
    }
}

void
console_set_width (Console *console, gint width)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));
  g_return_if_fail (width > 0);

  resize_screen (console, width, console->priv->height);
  console->priv->width = width;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    {
      gtk_widget_queue_resize (GTK_WIDGET (console));
      gtk_widget_queue_draw (GTK_WIDGET (console));
    }
}

void
console_set_size (Console *console, gint width, gint height)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));
  g_return_if_fail (width > 0 && height > 0);

  resize_screen (console, width, height);
  console->priv->width = width;
  console->priv->height = height;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    {
      gtk_widget_queue_resize (GTK_WIDGET (console));
      gtk_widget_queue_draw (GTK_WIDGET (console));
    }
}

void
console_set_font_family (Console *console, const char *family)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (family != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  if (console->priv->font_family)
    g_free (console->priv->font_family);

  console->priv->font_family = g_strdup (family);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    {
      gtk_widget_queue_resize (GTK_WIDGET (console));
      gtk_widget_queue_draw (GTK_WIDGET (console));
    }
}

void
console_set_font_style (Console *console, const char *style)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (style != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  if (console->priv->font_style)
    g_free (console->priv->font_style);

  console->priv->font_style = g_strdup (style);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    {
      gtk_widget_queue_resize (GTK_WIDGET (console));
      gtk_widget_queue_draw (GTK_WIDGET (console));
    }
}

void
console_set_font_size (Console *console, gint size)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));
  g_return_if_fail (size > 0);

  console->priv->font_size = size;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    {
      gtk_widget_queue_resize (GTK_WIDGET (console));
      gtk_widget_queue_draw (GTK_WIDGET (console));
    }
}

void
console_set_cursor_shape (Console *console, ConsoleCursorShape shape)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  if (shape < 0 || shape >= CONSOLE_CURSOR_MAX)
    shape = CONSOLE_CURSOR_DEFAULT;

  console->priv->cursor_shape = shape;

  invalidate_cursor_rect (console);
}

void
console_set_cursor_timer (Console *console, ConsoleBlinkTimer timer)
{
  gint timeout;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  if (console->priv->cursor_timer_id > 0)
    g_source_remove (console->priv->cursor_timer_id);

  console->priv->cursor_timer_id = 0;
  timeout = 0;

  switch (timer)
    {
      case CONSOLE_BLINK_STEADY:
        console->priv->cursor_toggle = TRUE;
        break;

      case CONSOLE_BLINK_SLOW:
        timeout = CURSOR_BLINKING_TIMER * 2;
        break;

      case CONSOLE_BLINK_MEDIUM:
        timeout = CURSOR_BLINKING_TIMER;
        break;

      case CONSOLE_BLINK_FAST:
        timeout = CURSOR_BLINKING_TIMER / 2;
        break;

      default:
        g_warn_if_reached ();
        break;
    }

  if (timeout > 0)
    {
      console->priv->cursor_timer_id =
        g_timeout_add (timeout, (GSourceFunc) console_cursor_timer, console);
    }

  invalidate_cursor_rect (console);
}

gint
console_get_width (Console *console)
{
  g_return_val_if_fail (console != NULL, -1);
  g_return_val_if_fail (IS_CONSOLE (console), -1);

  return console->priv->width;
}

gint
console_get_height (Console *console)
{
  g_return_val_if_fail (console != NULL, -1);
  g_return_val_if_fail (IS_CONSOLE (console), -1);

  return console->priv->height;
}

gint
console_get_font_size (Console *console)
{
  g_return_val_if_fail (console != NULL, -1);
  g_return_val_if_fail (IS_CONSOLE (console), -1);

  return console->priv->font_size;
}

const gchar*
console_get_font_family (Console *console)
{
  g_return_val_if_fail (console != NULL, NULL);
  g_return_val_if_fail (IS_CONSOLE (console), NULL);

  return console->priv->font_family;
}

const gchar*
console_get_font_style (Console *console)
{
  g_return_val_if_fail (console != NULL, NULL);
  g_return_val_if_fail (IS_CONSOLE (console), NULL);

  return console->priv->font_style;
}

ConsoleCursorShape
console_get_cursor_shape (Console *console)
{
  g_return_val_if_fail (console != NULL, -1);
  g_return_val_if_fail (IS_CONSOLE (console), -1);

  return console->priv->cursor_shape;
}

static void
console_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_WIDTH:
      console_set_width (CONSOLE (object), g_value_get_int (value));
      break;

    case PROP_HEIGHT:
      console_set_height (CONSOLE (object), g_value_get_int (value));
      break;

    case PROP_FONT_SIZE:
      console_set_font_size (CONSOLE (object), g_value_get_int (value));
      break;

    case PROP_FONT_STYLE:
      console_set_font_style (CONSOLE (object), g_value_get_string (value));
      break;

    case PROP_FONT_FAMILY:
      console_set_font_family (CONSOLE (object), g_value_get_string (value));
      break;

    case PROP_CURSOR_SHAPE:
      console_set_cursor_shape (CONSOLE (object), g_value_get_enum (value));
      break;

    case PROP_CURSOR_TIMER:
      console_set_cursor_timer (CONSOLE (object), g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
console_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_WIDTH:
      g_value_set_int (value, console_get_width (CONSOLE (object)));
      break;

    case PROP_HEIGHT:
      g_value_set_int (value, console_get_height (CONSOLE (object)));
      break;

    case PROP_FONT_SIZE:
      g_value_set_int (value, console_get_font_size (CONSOLE (object)));
      break;

    case PROP_FONT_STYLE:
      g_value_set_string (value, console_get_font_style (CONSOLE (object)));
      break;

    case PROP_FONT_FAMILY:
      g_value_set_string (value, console_get_font_family (CONSOLE (object)));
      break;

    case PROP_CURSOR_SHAPE:
      g_value_set_int (value, console_get_cursor_shape (CONSOLE (object)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
console_expose (GtkWidget *widget, GdkEventExpose *event)
{
  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (IS_CONSOLE(widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

/*  g_debug ("console_expose: event->area.x %d event->area.y %d width %d height %d",
           event->area.x, event->area.y, event->area.width, event->area.height);*/

  console_draw (widget, event);

  return FALSE;
}

/* This helper determines cursor rectangle position and size
 * according to cursor appearence and returns it in the
 * GdkRectangle structure provided by the caller.
 */
static void
get_cursor_rectangle (ConsolePrivate *priv, double xc, double yc, GdkRectangle *rect)
{

  rect->width = priv->char_width;
  rect->x = xc;

  switch (priv->cursor_shape)
    {
    case CONSOLE_CURSOR_FULL_BLOCK:
    case CONSOLE_CURSOR_DEFAULT:
      rect->height = priv->char_height;
      rect->y = yc;
      break;

    case CONSOLE_CURSOR_UNDERSCORE:
      rect->height = 3.0;
      rect->y = yc + priv->baseline - 1.0;
      break;

    case CONSOLE_CURSOR_LOWER_THIRD:
      rect->height = priv->char_height / 3.0;
      rect->y = yc + priv->char_height * (2.0 / 3.0);
      break;

    case CONSOLE_CURSOR_LOWER_HALF:
      rect->height = priv->char_height * 0.5;
      rect->y = yc + priv->char_height * 0.5;
      break;

    case CONSOLE_CURSOR_TWO_THIRDS:
      rect->height = priv->char_height * (2.0 / 3.0);
      rect->y = yc + priv->char_height / 3.0;
      break;

    case CONSOLE_CURSOR_VERT_THIRD:
      rect->height = priv->char_height;
      rect->width = priv->char_width / 3.0;
      rect->y = yc;
      break;

    case CONSOLE_CURSOR_VERT_HALF:
      rect->height = priv->char_height;
      rect->width = priv->char_width * 0.5;
      rect->y = yc;
      break;

    case CONSOLE_CURSOR_INVISIBLE:
      rect->x = 0;
      rect->y = 0;
      rect->width = 0;
      rect->height = 0;
      break;
    }
}

/* This helper returns TRUE if the cursor must be drawn at character position [x,y].
 */
static gboolean
cursor_is_visible_at (const ConsolePrivate *priv, gint x, gint y)
{
  if ((x == priv->cursor_x && y == priv->cursor_y) &&
      (priv->cursor_shape != CONSOLE_CURSOR_INVISIBLE) &&
      priv->cursor_toggle)
    return TRUE;
  else
    return FALSE;
}

static gboolean
console_gdk_rectangle_intersect (GdkRectangle *src1, GdkRectangle *src2)
{
  gint dest_x, dest_y;
  gint dest_x2, dest_y2;

  dest_x = MAX (src1->x, src2->x);
  dest_y = MAX (src1->y, src2->y);
  dest_x2 = MIN (src1->x + src1->width, src2->x + src2->width);
  dest_y2 = MIN (src1->y + src1->height, src2->y + src2->height);

  if (dest_x2 >= dest_x && dest_y2 >= dest_y)
    {
      return TRUE;
    }
  
  return FALSE;
}

static void
console_draw (GtkWidget *widget, GdkEventExpose *event)
{
  ConsolePrivate *priv;
  ConsoleChar *scr;
  cairo_t *cr;
  gint x, y, baseline, width, height;
  gint char_width, char_height, max_stride;
  guchar *glyph_buffer;
  gdouble dpi;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_CONSOLE (widget));

  priv = CONSOLE (widget)->priv;

  /* Obtain cairo context for the widget window and clip it to redraw area. */
  cr = gdk_cairo_create (GDK_DRAWABLE (widget->window));
  gdk_cairo_region (cr, event->region);
  cairo_clip (cr);

  /* shortcuts to console properties */
  width = priv->width;
  height = priv->height;
  char_width = priv->char_width;
  char_height = priv->char_height;
  baseline = priv->baseline;
  scr = priv->scr;

  /* get screen resolution to scale font points to pixels later */
  dpi = gdk_screen_get_resolution (gtk_widget_get_screen (widget));

  /* Create temporary buffer used to keep around a glyph bitmap.
   * Note that cairo library uses some hardware optimizations and
   * requires proper alignment of rows of pixels.
   */
  max_stride = cairo_format_stride_for_width (CAIRO_FORMAT_A8, char_width);
  glyph_buffer = g_alloca (max_stride * char_height);

  if (scr != NULL)
    {
      ConsoleTextSelection *cs = &priv->text_selection;
      GdkRectangle selection;

      selection.x = MIN(cs->x1, cs->x2);
      selection.y = MIN(cs->y1, cs->y2);
      selection.width = ABS(cs->x2 - cs->x1);
      selection.height = ABS(cs->y2 - cs->y1);

      for (y = 0; y < height; y++)
        {
          for (x = 0; x < width; x++)
            {
              GdkRectangle rect;
              ConsoleChar *chr;
              ConsoleColor *bg_color, *color;
              double xc, yc;

              /* calculate upper left coordinates of the character rectangle */
              xc = x * char_width;
              yc = y * char_height;

              rect.x = xc;
              rect.y = yc;
              rect.height = char_height;
              rect.width = char_width;

              /* skip this character if we are out of redraw requested region */
              if (gdk_region_rect_in (event->region, &rect) == GDK_OVERLAP_RECTANGLE_OUT)
                 continue;

              /* shortcut to character */
              chr = scr + y*width + x;

              color = &chr->color;
              bg_color = &chr->bg_color;

              /* swap colors if character inside ConsoleTextSelection area */
              if (console_gdk_rectangle_intersect(&rect, &selection))
                {
                  ConsoleColor *tmp;

                  g_debug ("intersect");

                  tmp = color;
                  color = bg_color;
                  bg_color = tmp;
                }

              /* draw background rectangle at a character position */
              cairo_save (cr);
              cairo_move_to (cr, xc, yc);
              cairo_set_source_rgb (cr, bg_color->red, bg_color->green, bg_color->blue);
              cairo_rectangle (cr, xc, yc, char_width, char_height);
              cairo_fill (cr);
              cairo_restore (cr);

              /* draw character glyph and cursor at a character position */
              if (chr->chr == ' ' && cursor_is_visible_at (priv, x, y))
                {
                  GdkRectangle rect;

                  get_cursor_rectangle (priv, xc, yc, &rect);

                  /* Here we simply draw the cursor at the character position.
                   */
                  cairo_save (cr);
                  cairo_set_source_rgb (cr, color->red, color->green, color->blue);
                  cairo_rectangle (cr, rect.x, rect.y, rect.width, rect.height);
                  cairo_fill (cr);
                  cairo_restore (cr);
                }
              else if (chr->chr != ' ')
                {
                  FTC_SBit sbitmap;
                  FTC_ScalerRec scaler;
                  FTC_Node node;
                  cairo_surface_t *image;
                  gint i, error, glyph_index, stride;
                  guchar *dst, *src;

                  glyph_index = FTC_CMapCache_Lookup (priv->cmapcache, priv, 0, chr->chr);
                  if (glyph_index <= 0)
                    {
                      FT_Face face;

                      FTC_Manager_LookupFace (priv->manager, priv, &face);
                      glyph_index = FT_Get_Char_Index (face, chr->chr);
                      if (glyph_index <= 0)
                        {
                          g_warning ("no unicode char 0x%0x in character map", chr->chr);
                          continue;
                        }
                    }

                  scaler.face_id = priv;
                  scaler.pixel = FALSE;
                  scaler.height = priv->font_size << 6;
                  scaler.width = 0;
                  scaler.x_res = dpi;
                  scaler.y_res = dpi;

                  error = FTC_SBitCache_LookupScaler (priv->sbitcache, &scaler,
                                                      FT_LOAD_RENDER | FT_LOAD_DEFAULT,
                                                      glyph_index, &sbitmap, &node);
                  if (error)
                    {
                      g_warning ("failed looking up sbitmap for glyph index %d", glyph_index);
                      continue;
                    }

                  stride = cairo_format_stride_for_width (CAIRO_FORMAT_A8, sbitmap->width);

                  memset (glyph_buffer, 0, stride * sbitmap->height);
                  dst = glyph_buffer;
                  src = sbitmap->buffer;

                  for (i = 0; i < sbitmap->height; i++)
                    {
                      memcpy (dst, src, sbitmap->width);
                      src += sbitmap->pitch;
                      dst += stride;
                    }

                  image = cairo_image_surface_create_for_data (glyph_buffer, CAIRO_FORMAT_A8, sbitmap->width, sbitmap->height, stride);

                  cairo_set_source_rgb (cr, color->red, color->green, color->blue);

                  cairo_mask_surface (cr, image, xc + sbitmap->left, yc + baseline - sbitmap->top);

                  if (cursor_is_visible_at (priv, x, y))
                    {
                      GdkRectangle rect;

                      /* We draw the cursor using foreground color,
                       * then we place the glyph bitmap, clipped by cursor
                       * rectangle, above. The glyph is drawn in background
                       * color.
                       */
                      get_cursor_rectangle (priv, xc, yc, &rect);

                      cairo_save (cr);
                      cairo_set_source_rgb (cr, color->red, color->green, color->blue);
                      cairo_rectangle (cr, rect.x, rect.y, rect.width, rect.height);
                      cairo_fill_preserve (cr);
                      cairo_clip (cr);
                      cairo_set_source_rgb (cr, bg_color->red, bg_color->green, bg_color->blue);
                      cairo_mask_surface (cr, image, xc + sbitmap->left, yc + baseline - sbitmap->top);
                      cairo_restore (cr);
                    }

                  cairo_surface_destroy (image);

                  FTC_Node_Unref (node, priv->manager);
                }
            }
        }
    }

  cairo_destroy (cr);
}

/* This is called when the reference count drops to zero
 * and after the object was disposed and holds no references
 * to other objects.
 */
static void
console_finalize (GObject *object)
{
  GtkWidget *widget;
  GObjectClass *parent_class;
  ConsoleClass *klass;
  ConsolePrivate *priv;

  g_return_if_fail (object != NULL);
  g_return_if_fail (IS_CONSOLE(object));

  widget = GTK_WIDGET (object);
  priv = CONSOLE (object)->priv;

  if (priv->scr != NULL)
    g_free (priv->scr);

  priv->scr = NULL;

  if (priv->font_family != NULL)
    g_free (priv->font_family);

  if (priv->font_style != NULL)
    g_free (priv->font_style);

  if (priv->font_file != NULL)
    g_free (priv->font_file);

  priv->font_family = NULL;
  priv->font_style = NULL;
  priv->font_file = NULL;

  console_font_cache_deinit (priv);

  /* remove cursor blink timer */
  if (priv->cursor_timer_id > 0)
    {
      g_source_remove (priv->cursor_timer_id);
      priv->cursor_timer_id = 0;
    }

  klass = CONSOLE_CLASS (g_type_class_peek (console_get_type()));
  parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

  /* Since parent's class destroy method was overwritten in
   * console_class_init, we must explicitely call it here.
   */

  if (parent_class->finalize != NULL)
    (*parent_class->finalize) (object);
}

static void
scroll_box_down (Console *console, gint x, gint y, gint box_width, gint box_height, gint nlines)
{
  ConsolePrivate *priv;
  ConsoleChar *scr;
  gint width, height;

  priv = console->priv;
  scr = priv->scr;
  width = priv->width;
  height = priv->height;

  if (scr != NULL)
    {
      gint cnt;

      /* correct scroll box size */
      if ((x + box_width) > width)
        box_width -= (x + box_width) - width;

      if ((y + box_height) > height)
        box_height -= (y + box_height) - height;

      /* get number of lines to move */
      cnt = box_height - nlines;

      /* move lines to new positions */
      if (cnt > 0)
        {
          ConsoleChar *p1 = scr + ((y + box_height - 1)*width + x);
          ConsoleChar *p2 = scr + ((y + cnt - 1)*width + x);

          while (cnt > 0)
            {
              ConsoleChar *dst = p1;
              ConsoleChar *src = p2;
              gint n;

              for (n = 0; n < box_width; n++)
                {
                  *dst++ = *src;
                  src->attr = priv->attr;
                  src->chr = ' ';
                  src->color = priv->color;
                  src->bg_color = priv->bg_color;
                  ++src;
                }

              p1 -= width;
              p2 -= width;

              --cnt;
            }
        }
    }
}

static void
scroll_box_up (Console *console, gint x, gint y, gint box_width, gint box_height, gint nlines)
{
  ConsolePrivate *priv;
  ConsoleChar *scr;
  gint width, height;

  g_assert (console != NULL);
  g_assert (x >= 0 && y >= 0 && nlines >= 0);
  g_assert (box_width >= 0 && box_height >= 0);

  priv = console->priv;
  scr = priv->scr;
  width = priv->width;
  height = priv->height;

  if (scr != NULL)
    {
      gint cnt;

      /* correct scroll box size */
      if ((x + box_width) > width)
        box_width -= (x + box_width) - width;

      if ((y + box_height) > height)
        box_height -= (y + box_height) - height;

      /* get number of lines to move */
      cnt = box_height - nlines;

      /* move lines to new positions */
      if (cnt > 0)
        {
          ConsoleChar *p1 = scr + (y*width + x);
          ConsoleChar *p2 = scr + ((y + nlines)*width + x);

          while (cnt > 0)
            {
              ConsoleChar *dst = p1;
              ConsoleChar *src = p2;
              gint n;

              for (n = 0; n < box_width; n++)
                {
                  *dst++ = *src;
                  src->attr = priv->attr;
                  src->chr = ' ';
                  src->color = priv->color;
                  src->bg_color = priv->bg_color;
                  ++src;
                }

              p1 += width;
              p2 += width;

              --cnt;
            }
        }
    }
}

/* This helper invalidates console widget window area at the current cursor position.
 */
static void
invalidate_cursor_rect (Console *console)
{
  ConsolePrivate *priv;
  GdkRectangle rect;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    {
      priv = console->priv;

      rect.x = priv->cursor_x * priv->char_width;
      rect.y = priv->cursor_y * priv->char_height;
      rect.height = priv->char_height;
      rect.width = priv->char_width;

      gdk_window_invalidate_rect (GTK_WIDGET (console)->window, &rect, FALSE);
    }
}

/* This helper invalidates console widget window area at coordinates x and y.
 */
static void
invalidate_char_rect (Console *console, int x, int y)
{
  ConsolePrivate *priv;
  GdkRectangle rect;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    {
      priv = console->priv;

      g_assert (x < priv->width && y < priv->height);

      rect.x = x * priv->char_width;
      rect.y = y * priv->char_height;
      rect.height = priv->char_height;
      rect.width = priv->char_width ;

      gdk_window_invalidate_rect (GTK_WIDGET (console)->window, &rect, FALSE);
    }
}

/* This function is called by cursor blinking timer. */
static gboolean
console_cursor_timer (gpointer user_data)
{
  ConsolePrivate *priv;
  Console *console;

  g_return_val_if_fail (user_data != NULL, FALSE);
  g_return_val_if_fail (IS_CONSOLE (user_data), FALSE);

  console = CONSOLE (user_data);
  priv = console->priv;

  priv->cursor_toggle = !priv->cursor_toggle;

  invalidate_cursor_rect (console);

  return TRUE;
}

void
console_put_char (Console *console, gunichar uc)
{
  ConsolePrivate *priv;
  ConsoleChar *chr;
  gint width, height;
  gint cursor_x, cursor_y;
  gint32 mask;
  gint i, pos;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  priv = console->priv;

  width = priv->width;
  height = priv->height;
  cursor_x = priv->cursor_x;
  cursor_y = priv->cursor_y;

  switch (uc)
    {
    case ASCII_ESC: /* ESCAPE */
    case ASCII_NUL: /* NUL */
      break;

    case ASCII_FF: /* form feed */
    case ASCII_LF: /* line feed */
      /* previous cursor position must be redrawn */
      invalidate_cursor_rect (console);
      /* move cursor to the next line */
      ++cursor_y;
      if (cursor_y >= height)
        {
          cursor_y = height - 1;
          scroll_box_up (console, 0, 0, width, height, 1);
          gtk_widget_queue_draw (GTK_WIDGET (console));
        }
      /* redraw character at the new cursor position */
      invalidate_char_rect (console, cursor_x, cursor_y);
      /* save modification back */
      priv->cursor_y = cursor_y;
      break;

    case ASCII_CR: /* carriage return */
      /* character at previous cursor position must be redrawn */
      invalidate_cursor_rect (console);
      /* move cursor to the beginning of the current line */
      cursor_x = 0;
      invalidate_char_rect (console, cursor_x, cursor_y);
      priv->cursor_x = cursor_x;
      break;

    case ASCII_BEL: /* ring the bell */
      gdk_window_beep (GTK_WIDGET (console)->window);
      break;

    case ASCII_BS: /* backspace */
    case ASCII_DEL: /* delete */
      if (cursor_x > 0)
        {
          invalidate_cursor_rect (console);
          --cursor_x;
          chr = priv->scr + (width*cursor_y + cursor_x);
          chr->attr = priv->attr;
          chr->color = priv->color;
          chr->bg_color = priv->bg_color;
          chr->chr = ' ';
          invalidate_char_rect (console, cursor_x, cursor_y);
          priv->cursor_x = cursor_x;
        }
      break;

    case ASCII_VT:
      break;

    case ASCII_HT: /* horizontal tab */
      invalidate_cursor_rect (console);
      /* determine the nearest tab position using bitmap */
      pos = cursor_x + 1;
      mask = 1 << (pos & ((1 << TABMAP_SIZE)-1));
      i = pos >> 5;
      while (i < TABMAP_SIZE)
        {
          if (mask == 0)
            mask = 0x1;
          while (mask != 0)
            {
              if ((priv->tabs[i] & mask) != 0)
                goto out;
              mask <<= 1;
              ++pos;
            }
          ++i;
        }
      out:
      if (pos < width)
        cursor_x = pos;
      else
        cursor_x = width - 1;
      priv->cursor_x = cursor_x;
      invalidate_char_rect (console, cursor_x, cursor_y);
      break;

    default:
      g_assert (cursor_x < width && cursor_y < height);
      /* shortcut to character */
      chr = priv->scr + (width*cursor_y + cursor_x);
      /* put the character at the current cursor position */
      chr->attr = priv->attr;
      chr->color = priv->color;
      chr->bg_color = priv->bg_color;
      chr->chr = uc;
      /* invalidate a character at the cursor position */
      invalidate_cursor_rect (console);
      /* advance the cursor to the next position */
      ++cursor_x;
      if (cursor_x >= width)
        {
          cursor_x = 0;
          ++cursor_y;
        }
      /* scroll console one line up if needed */
      if (cursor_y >= height)
        {
          cursor_y = height - 1;
          scroll_box_up (console, 0, 0, width, height, 1);
          gtk_widget_queue_draw (GTK_WIDGET (console));
        }
      /* save modified cursor position back */
      priv->cursor_x = cursor_x;
      priv->cursor_y = cursor_y;
      /* request redrawing the widget */
      invalidate_char_rect (console, cursor_x, cursor_y);
      break;
    }
}

gboolean
console_put_char_at (Console *console, gunichar c, gint x, gint y)
{
  ConsolePrivate *priv;
  ConsoleChar *chr;

  g_return_val_if_fail (console != NULL, FALSE);
  g_return_val_if_fail (IS_CONSOLE (console), FALSE);
  g_return_val_if_fail (x >= 0 && y >= 0, FALSE);

  priv = console->priv;

  if (x >= priv->width || y >= priv->height)
    return FALSE;

  if (priv->scr != NULL)
    {
      GdkRectangle rect;
      gint char_width, char_height;

      char_width = priv->char_width;
      char_height = priv->char_height;

      rect.width = char_width;
      rect.height = char_height;
      rect.x = x * char_width;
      rect.y = y * char_height;

      chr = priv->scr + (y*priv->width + x);

      chr->chr = c;
      chr->color = priv->color;
      chr->bg_color = priv->bg_color;
      chr->attr = priv->attr;

      if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
        gdk_window_invalidate_rect (GTK_WIDGET (console)->window, &rect, TRUE);
    }

  return TRUE;
}

void
console_scroll_box_down (Console *console, gint x, gint y, gint box_width, gint box_height, gint nlines)
{
  GdkRectangle rect;
  ConsolePrivate *priv;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));
  g_return_if_fail (x >= 0 && y >= 0);

  priv = console->priv;

  rect.x = x * priv->char_width;
  rect.y = y * priv->char_height;
  rect.width = box_width * priv->char_width;
  rect.height = box_height * priv->char_height;

  scroll_box_down (console, x, y, box_width, box_height, nlines);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    gdk_window_invalidate_rect (GTK_WIDGET (console)->window, &rect, FALSE);
}

void
console_scroll_box_up (Console *console, gint x, gint y, gint box_width, gint box_height, gint nlines)
{
  GdkRectangle rect;
  ConsolePrivate *priv;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));
  g_return_if_fail (x >= 0 && y >= 0);

  priv = console->priv;

  rect.x = x * priv->char_width;
  rect.y = y * priv->char_height;
  rect.width = box_width * priv->char_width;
  rect.height = box_height * priv->char_height;

  scroll_box_up (console, x, y, box_width, box_height, nlines);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
    gdk_window_invalidate_rect (GTK_WIDGET (console)->window, &rect, FALSE);
}

void
console_move_cursor_to (Console *console, gint x, gint y)
{
  ConsolePrivate *priv;
  gint old_x, old_y;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  priv = console->priv;
  old_x = priv->cursor_x;
  old_y = priv->cursor_y;

  /* Negative x or y cursor coordinate indicates that
   * the user asked to leave the old position unchanged.
   */
  if (x >= 0)
    {
      if (x >= priv->width)
        x = priv->width - 1;
      priv->cursor_x = x;
    }

  if (y >= 0)
    {
      if (y >= priv->height)
        y = priv->height - 1;
      priv->cursor_y = y;
    }

  /* request to redraw a rectangle at the new cursor position */
  invalidate_cursor_rect (console);

  /* request to redraw a rectangle at the old cursor position */
  invalidate_char_rect (console, old_x, old_y);
}

void
console_erase_line (Console *console, ConsoleEraseMode mode)
{
  ConsoleChar *chr;
  GdkRectangle rect;
  gint char_width, char_height;
  gint width, height;
  gint nr_chars_erased;
  gint x, y;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  rect.x = rect.y = 0;
  rect.width = rect.height = 0;

  /* Obtain shortcuts to the most used fields of the structure.
   */
  char_width = console->priv->char_width;
  char_height = console->priv->char_height;
  width = console->priv->width;
  height = console->priv->height;
  x = console->priv->cursor_x;
  y = console->priv->cursor_y;
  nr_chars_erased = 0;

  if (console->priv->scr != NULL)
    {
      /* Calculate the rectangular region of the window, which will require an update.
       */
      switch (mode)
        {
        case CONSOLE_ERASE_FROM_START:
          rect.x = 0;
          rect.y = y * char_height;
          rect.width = (x+1) * char_width;
          rect.height = 1 * char_height;
          nr_chars_erased = x + 1;
          chr = console->priv->scr + y*width;
          break;

        case CONSOLE_ERASE_TO_END:
          rect.x = x * char_width;
          rect.y = y * char_height;
          rect.width = (width - x) * char_width;
          rect.height = 1 * char_height;
          nr_chars_erased = width - x;
          chr = console->priv->scr + y*width + x;
          break;

        case CONSOLE_ERASE_WHOLE:
          rect.x = 0;
          rect.y = y * char_height;
          rect.width = width * char_width;
          rect.height = 1 * char_height;
          nr_chars_erased = width;
          chr = console->priv->scr + y*width;
          break;

        default:
          g_warn_if_reached ();
        }

      /* Erase characters of the line.
       */
      for (; nr_chars_erased > 0; --nr_chars_erased, ++chr)
        {
          chr->attr = 0;
          chr->chr = ' ';
          chr->bg_color = console->priv->bg_color;
          chr->color = console->priv->color;
        }

      /* Notify that the rectangular region of the widget's window needs an update.
       */
      if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
        gdk_window_invalidate_rect (GTK_WIDGET (console)->window, &rect, FALSE);
    }
}

void
console_erase_display (Console *console, ConsoleEraseMode mode)
{
  ConsoleChar *chr;
  GdkRegion *region;
  GdkRectangle rect;
  gint char_width, char_height;
  gint width, height;
  gint nr_chars_erased;
  gint x, y;

  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  char_width = console->priv->char_width;
  char_height = console->priv->char_height;
  width = console->priv->width;
  height = console->priv->height;
  x = console->priv->cursor_x;
  y = console->priv->cursor_y;
  nr_chars_erased = 0;

  if (console->priv->scr != NULL)
    {
      region = gdk_region_new ();

      switch (mode)
        {
        case CONSOLE_ERASE_FROM_START:
          rect.x = 0;
          rect.y = y * char_height;
          rect.width = (x+1) * char_width;
          rect.height = 1 * char_height;
          gdk_region_union_with_rect (region, &rect);
          rect.x = 0;
          rect.y = 0;
          rect.width = width * char_width;
          rect.height = y * char_height;
          gdk_region_union_with_rect (region, &rect);
          nr_chars_erased = (x+1) + y*width;
          chr = console->priv->scr;
          break;

        case CONSOLE_ERASE_TO_END:
          rect.x = x * char_width;
          rect.y = y * char_height;
          rect.width = (width - x) * char_width;
          rect.height = 1 * char_height;
          gdk_region_union_with_rect (region, &rect);

          if (y+1 < height)
            {
              rect.x = 0;
              rect.y = (y + 1) * char_height;
              rect.width = width * char_width;
              rect.height = (height - (y+1)) * char_height;
              gdk_region_union_with_rect (region, &rect);
            }

          nr_chars_erased = (width - x) + (height - (y+1))*width;
          chr = console->priv->scr + y*width + x;
          break;

        case CONSOLE_ERASE_WHOLE:
          rect.x = 0;
          rect.y = 0;
          rect.width = width * char_width;
          rect.height = height * char_height;
          nr_chars_erased = width * height;
          chr = console->priv->scr;
          gdk_region_union_with_rect (region, &rect);
          break;

        default:
          g_warn_if_reached ();
        }

      for (; nr_chars_erased > 0; --nr_chars_erased, ++chr)
        {
          chr->attr = 0;
          chr->chr = ' ';
          chr->bg_color = console->priv->bg_color;
          chr->color = console->priv->color;
        }

      if (GTK_WIDGET_REALIZED (GTK_WIDGET (console)))
        gdk_window_invalidate_region (GTK_WIDGET (console)->window, region, FALSE);

      gdk_region_destroy (region);
    }
}

void
console_get_cursor (Console *console, gint *x, gint *y)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  if (x != NULL)
    *x = console->priv->cursor_x;

  if (y != NULL)
    *y = console->priv->cursor_y;
}

void
console_window_to_display_coords (Console *console, double *x_coord, double *y_coord)
{
  g_return_if_fail (console != NULL);
  g_return_if_fail (IS_CONSOLE (console));

  if (x_coord != NULL)
    *x_coord = floor (*x_coord / console->priv->char_width);

  if (y_coord != NULL)
    *y_coord = floor (*y_coord / console->priv->char_height);
}

