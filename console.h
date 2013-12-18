#ifndef __CONSOLE_H
#define __CONSOLE_H

#include <gtk/gtk.h>
#include <cairo.h>

G_BEGIN_DECLS

#define CONSOLE_TYPE (console_get_type ())
#define CONSOLE(obj) GTK_CHECK_CAST(obj, console_get_type (), Console)
#define CONSOLE_CLASS(klass) GTK_CHECK_CLASS_CAST(klass, console_get_type (), ConsoleClass)
#define IS_CONSOLE(obj) GTK_CHECK_TYPE(obj, console_get_type ())

typedef enum
{
  CONSOLE_CURSOR_DEFAULT,
  CONSOLE_CURSOR_INVISIBLE,
  CONSOLE_CURSOR_UNDERSCORE,
  CONSOLE_CURSOR_LOWER_THIRD,
  CONSOLE_CURSOR_LOWER_HALF,
  CONSOLE_CURSOR_TWO_THIRDS,
  CONSOLE_CURSOR_FULL_BLOCK,
  CONSOLE_CURSOR_VERT_THIRD,
  CONSOLE_CURSOR_VERT_HALF,
  CONSOLE_CURSOR_MAX
} ConsoleCursorShape;

typedef enum
{
  CONSOLE_BLINK_STEADY,
  CONSOLE_BLINK_SLOW,
  CONSOLE_BLINK_MEDIUM,
  CONSOLE_BLINK_FAST
} ConsoleBlinkTimer;

typedef enum
{
  CONSOLE_ERASE_TO_END,            /* erase from the cursor position to the end */
  CONSOLE_ERASE_FROM_START,        /* erase from the start to the cursor position */
  CONSOLE_ERASE_WHOLE
} ConsoleEraseMode;

typedef struct _Console Console;
typedef struct _ConsoleClass ConsoleClass;
typedef struct _ConsolePrivate ConsolePrivate;

struct _Console
{
  GtkWidget widget;

  ConsolePrivate *priv;
};

struct _ConsoleClass
{
  GtkWidgetClass parent_class;

  /*events */
  gboolean (* primary_text_selected) (Console *console,
                              const gchar *str);
  gboolean (* primary_text_pasted)   (Console *console,
                              const gchar *str);
  gboolean (* clipboard_text_selected) (Console *console,
                              const gchar *str);
  gboolean (* clipboard_text_pasted)   (Console *console,
                              const gchar *str);
};

GType              console_get_type         ();

GtkWidget*         console_new              ();

GtkWidget*         console_new_with_size    (gint     width,
                                             gint     height);

gint               console_get_width        (Console *console);
gint               console_get_height       (Console *console);
gint               console_get_font_size    (Console *console);
const gchar*       console_get_font_family  (Console *console);
const gchar*       console_get_font_style   (Console *console);

void               console_set_width        (Console     *console,
                                             gint         width);
void               console_set_height       (Console     *console,
                                             gint         height);
void               console_set_size         (Console     *console,
                                             gint         width,
                                             gint         height);
void               console_set_font_size    (Console     *console,
                                             gint         size);
void               console_set_font_family  (Console     *console,
                                             const gchar *family);
void               console_set_font_style   (Console     *console,
                                             const gchar *style);

void               console_get_foreground_color (Console        *console,
                                                 GdkColor       *color);
void               console_get_background_color (Console        *console,
                                                 GdkColor       *color);
void               console_set_background_color (Console        *console,
                                                 const GdkColor *color);
void               console_set_foreground_color (Console        *console,
                                                 const GdkColor *color);

void               console_set_background_color_from_string (Console *console,
                                                             const gchar *spec);
void               console_set_foreground_color_from_string (Console *console,
                                                             const gchar *spec);

void               console_set_cursor_timer (Console            *console,
                                             ConsoleBlinkTimer   timer);

ConsoleCursorShape console_get_cursor_shape (Console            *console);
void               console_set_cursor_shape (Console            *console,
                                             ConsoleCursorShape  cursor_shape);
void               console_erase_display    (Console            *console,
                                             ConsoleEraseMode    mode);
void               console_erase_line       (Console            *console,
                                             ConsoleEraseMode    mode);
void               console_clear            (Console            *console);
void               console_reset            (Console            *console);

void               console_set_tab_stop     (Console            *console);
void               console_clear_tab_stop   (Console            *console);
void               console_clear_tab_stops  (Console            *console);

void               console_get_cursor       (Console            *console,
                                             gint               *x,
                                             gint               *y);
void               console_move_cursor_to   (Console                *console,
                                             gint                x,
                                             gint                y);
void               console_put_char         (Console            *console,
                                             gunichar            c);
gboolean           console_put_char_at      (Console            *console,
                                             gunichar            c,
                                             gint                x,
                                             gint                y);
void               console_scroll_box_up    (Console            *console,
                                             gint                x,
                                             gint                y,
                                             gint                scroll_width,
                                             gint                scroll_height,
                                             gint                nlines);
void               console_scroll_box_down  (Console            *console,
                                             gint                x,
                                             gint                y,
                                             gint                scroll_width,
                                             gint                scroll_height,
                                             gint                nlines);
void               console_window_to_display_coords (Console     *console,
                                             double             *x_coord,
                                             double             *y_coord);

G_END_DECLS

#endif /* __CONSOLE_H */

