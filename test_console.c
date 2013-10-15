#include <gdk/gdkkeysyms.h>
#define GETTEXT_PACKAGE "gtk20"
#include <glib/gi18n-lib.h>

#include "fontsel.h"
#include "console.h"

typedef struct _ConsoleScrollBoxInfo
{
  GtkWidget *x_entry;
  GtkWidget *y_entry;
  GtkWidget *width_entry;
  GtkWidget *height_entry;
  GtkWidget *lines_entry;
  GtkWidget *down_radio_button;
  GtkWidget *up_radio_button;
} ConsoleScrollBoxInfo;

typedef struct _ConsoleCursorMoveInfo
{
  GtkWidget *x_spinner;
  GtkWidget *y_spinner;
} ConsoleCursorMoveInfo;

typedef struct _ConsoleClearInfo
{
  gboolean clear_display;
  ConsoleEraseMode mode;
} ConsoleClearInfo;

static GtkWidget *main_window;
static GtkWidget *main_console;

static int resize_timeout_id;

static void
font_selection_dialog (GtkWidget *widget, gpointer user_data)
{
  ConsoleFontSelectionDialog *dialog;
  Console *console;
  gint res, size;

  g_assert (IS_CONSOLE (user_data));

  console = CONSOLE (user_data);

  dialog =
    CONSOLE_FONT_SELECTION_DIALOG (console_font_selection_dialog_new ());
  console_font_selection_dialog_set_family (dialog, console_get_font_family (console));
  console_font_selection_dialog_set_style (dialog, console_get_font_style (console));
  console_font_selection_dialog_set_size (dialog, console_get_font_size (console));

  res = gtk_dialog_run (GTK_DIALOG (dialog));

  switch (res)
    {
    case GTK_RESPONSE_OK:
      console_set_font_family (console,
			       console_font_selection_dialog_get_family (dialog));
      console_set_font_style (console,
			      console_font_selection_dialog_get_style (dialog));
      console_set_font_size (console,
			     console_font_selection_dialog_get_size (dialog));
      break;

    case GTK_RESPONSE_CANCEL:
    case GTK_RESPONSE_DELETE_EVENT:
      break;

    default:
      g_warn_if_reached ();
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
console_queue_redraw(GtkWidget *widget)
{
  GdkRegion *region;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (IS_CONSOLE(widget));

  region = gdk_drawable_get_clip_region (GDK_DRAWABLE(widget->window));
  gdk_window_invalidate_region (widget->window, region, TRUE);
  gdk_window_process_updates (widget->window, TRUE);
}

static void
console_cursor_shape_change (GtkWidget *widget, gpointer user_data)
{
  Console *console;
  ConsoleCursorShape cursor_shape;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (user_data != NULL);
  g_return_if_fail (IS_CONSOLE (user_data));

  console = CONSOLE (user_data);

  cursor_shape = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "cursor-shape"));

  console_set_cursor_shape (console, cursor_shape);
}

static void
console_cursor_timer_change (GtkWidget *widget, gpointer user_data)
{
  Console *console;
  ConsoleBlinkTimer timer;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (user_data != NULL);
  g_return_if_fail (IS_CONSOLE (user_data));

  console = CONSOLE (user_data);

  timer = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "cursor-timer"));

  console_set_cursor_timer (console, timer);
}

static void
background_color_button_color_set_cb (GtkWidget *widget, gpointer user_data)
{
  GdkColor color;

  gtk_color_button_get_color (GTK_COLOR_BUTTON (widget), &color);

  console_set_background_color (CONSOLE (main_console), &color);
}

static void
foreground_color_button_color_set_cb (GtkWidget *widget, gpointer user_data)
{
  GdkColor color;

  gtk_color_button_get_color (GTK_COLOR_BUTTON (widget), &color);

  console_set_foreground_color (CONSOLE (main_console), &color);
}

static void
console_color_change_dialog (GtkWidget *widget, gpointer user_data)
{
  Console *console;
  GtkWidget *dialog;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *color_button;
  GdkColor bg_color, fg_color;
  gint res;

  g_return_if_fail (user_data != NULL);
  g_return_if_fail (IS_CONSOLE (user_data));

  console = CONSOLE (user_data);

  dialog = gtk_dialog_new_with_buttons (_("Console colors"), GTK_WINDOW (main_window),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					NULL);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

  label = gtk_label_new_with_mnemonic (_("<b>Colors</b>"));
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  table = gtk_table_new (2, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), table, FALSE, FALSE, 0);

  label = gtk_label_new_with_mnemonic (_("Foreground Color:"));
  gtk_widget_set_size_request (label, 120, -1);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, GTK_SHRINK, GTK_SHRINK, 0, 0);

  label = gtk_label_new_with_mnemonic (_("Background Color:"));
  gtk_widget_set_size_request (label, 120, -1);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2, GTK_SHRINK, GTK_SHRINK, 0, 0);

  console_get_foreground_color (console, &fg_color);
  color_button = gtk_color_button_new_with_color (&fg_color);
  gtk_table_attach (GTK_TABLE (table), color_button, 1, 2, 0, 1, GTK_SHRINK, GTK_SHRINK, 0, 0);
  g_signal_connect (G_OBJECT (color_button), "color-set", (GCallback) foreground_color_button_color_set_cb, console);

  console_get_background_color (console, &bg_color);
  color_button = gtk_color_button_new_with_color (&bg_color);
  gtk_table_attach (GTK_TABLE (table), color_button, 1, 2, 1, 2, GTK_SHRINK, GTK_SHRINK, 0, 0);
  g_signal_connect (G_OBJECT (color_button), "color-set", (GCallback) background_color_button_color_set_cb, console);

  gtk_widget_show_all (table);

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  switch (res)
    {
    case GTK_RESPONSE_ACCEPT:
      break;

    case GTK_RESPONSE_CANCEL:
    case GTK_RESPONSE_DELETE_EVENT:
      /* set previous console colors back */
      console_set_foreground_color (console, &fg_color);
      console_set_background_color (console, &bg_color);
      break;

    default:
      g_warn_if_reached ();
    }

  gtk_widget_destroy (dialog);
}

static void
change_width (GtkSpinButton *spinbutton, gpointer user_data)
{
  Console *console;
  gint new_width;

  console = CONSOLE (user_data);
  new_width = gtk_spin_button_get_value_as_int (spinbutton);

  console_set_width (console, new_width);
}

static void
change_height (GtkSpinButton *spinbutton, gpointer user_data)
{
  Console *console;
  gint new_height;

  console = CONSOLE (user_data);
  new_height = gtk_spin_button_get_value_as_int (spinbutton);

  console_set_height (console, new_height);
}

static void
console_size_change_dialog (GtkWidget *widget, gpointer user_data)
{
  Console *console;
  GtkWidget *dialog;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *spin_button;
  gint old_width, old_height;
  gint result;

  g_return_if_fail (user_data != NULL);
  g_return_if_fail (IS_CONSOLE (user_data));

  console = CONSOLE (user_data);
  old_width = console_get_width (console);
  old_height = console_get_height (console);

  dialog =
    gtk_dialog_new_with_buttons (_("Console size"), GTK_WINDOW (main_window),
				 GTK_DIALOG_DESTROY_WITH_PARENT,
				 GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
				 NULL);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

  label = gtk_label_new_with_mnemonic (_("<b>Screen</b>"));
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  table = gtk_table_new (2, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_container_set_border_width (GTK_CONTAINER (table), 6);

  label = gtk_label_new_with_mnemonic (_("Screen width:"));
  gtk_widget_set_size_request (label, 120, -1);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, GTK_SHRINK, GTK_SHRINK, 0, 0);

  label = gtk_label_new_with_mnemonic (_("Screen height:"));
  gtk_widget_set_size_request (label, 120, -1);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2, GTK_SHRINK, GTK_SHRINK, 0, 0);

  spin_button = gtk_spin_button_new_with_range (1, 1024, 1);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (spin_button), 0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button), old_width);
  gtk_table_attach (GTK_TABLE (table), spin_button, 1, 2, 0, 1, GTK_SHRINK, GTK_SHRINK, 0, 0);
  g_signal_connect (G_OBJECT (spin_button), "value-changed", (GCallback) change_width, console);

  spin_button = gtk_spin_button_new_with_range (1, 1024, 1);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (spin_button), 0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button), old_height);
  gtk_table_attach (GTK_TABLE (table), spin_button, 1, 2, 1, 2, GTK_SHRINK, GTK_SHRINK, 0, 0);
  g_signal_connect (G_OBJECT (spin_button), "value-changed", (GCallback) change_height, console);

  gtk_box_pack_start(GTK_BOX (GTK_DIALOG (dialog)->vbox), table, 0, 0, 0);

  gtk_widget_show_all (table);

  result = gtk_dialog_run (GTK_DIALOG (dialog));
  switch (result)
    {
    case GTK_RESPONSE_ACCEPT:
      break;

    case GTK_RESPONSE_REJECT:
    case GTK_RESPONSE_DELETE_EVENT:
      console_set_size (console, old_width, old_height);
      break;

    default:
      g_warn_if_reached ();
    }

  gtk_widget_destroy (dialog);
}

static void
scroll_apply_button_clicked_cb (GtkWidget *widget, gpointer user_data)
{
  gint x, y, box_width, box_height, nlines;
  gboolean down;

  g_assert (user_data != NULL);

  ConsoleScrollBoxInfo *info = user_data;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->down_radio_button)))
    down = TRUE;
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->up_radio_button)))
    down = FALSE;
  else
    g_warn_if_reached ();

  x = g_ascii_strtoll (gtk_entry_get_text (GTK_ENTRY (info->x_entry)), NULL, 10);
  y = g_ascii_strtoll (gtk_entry_get_text (GTK_ENTRY (info->y_entry)), NULL, 10);
  box_width = g_ascii_strtoll (gtk_entry_get_text (GTK_ENTRY (info->width_entry)), NULL, 10);
  box_height = g_ascii_strtoll (gtk_entry_get_text (GTK_ENTRY (info->height_entry)), NULL, 10);
  nlines = g_ascii_strtoll (gtk_entry_get_text (GTK_ENTRY (info->lines_entry)), NULL, 10);

  if (down)
    console_scroll_box_down (CONSOLE (main_console), x, y, box_width, box_height, nlines);
  else
    console_scroll_box_up (CONSOLE (main_console), x, y, box_width, box_height, nlines);
}

static void
console_scroll_box_dialog (GtkWidget *widget, gpointer user_data)
{
  ConsoleScrollBoxInfo info;
  Console *console;
  GtkWidget *dialog;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *radio_button;
  GtkWidget *button;
  GSList *group = NULL;
  gint res;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (user_data != NULL);
  g_return_if_fail (IS_CONSOLE (user_data));

  console = CONSOLE (user_data);

  dialog =
    gtk_dialog_new_with_buttons (_("Console Commands"), GTK_WINDOW (main_window),
				 GTK_DIALOG_DESTROY_WITH_PARENT,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
				 NULL);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

  button = gtk_button_new_from_stock (GTK_STOCK_APPLY);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), button, FALSE, FALSE, 0);
  gtk_widget_show (button);
  g_signal_connect (G_OBJECT (button), "clicked", (GCallback) scroll_apply_button_clicked_cb, &info);

  label = gtk_label_new_with_mnemonic (_("<b>Box scrolling</b>"));
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 0);

  table = gtk_table_new (6, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 6);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), table, FALSE, FALSE, 0);

  radio_button = gtk_radio_button_new_with_label (NULL, _("Scroll up"));
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio_button));
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 2, 0, 1, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);
  info.up_radio_button = radio_button;

  radio_button = gtk_radio_button_new_with_label (group, _("Scroll down"));
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 2, 1, 2, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);
  info.down_radio_button = radio_button;

  label = gtk_label_new_with_mnemonic (_("x:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

  entry = gtk_entry_new_with_max_length (3);
  gtk_entry_set_alignment (GTK_ENTRY (entry), 1.0);
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 4);
  gtk_entry_set_editable (GTK_ENTRY (entry), TRUE);
  gtk_entry_set_text (GTK_ENTRY (entry), "0");
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 2, 3, GTK_SHRINK, GTK_SHRINK, 0, 0);
  info.x_entry = entry;

  label = gtk_label_new_with_mnemonic (_("y:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

  entry = gtk_entry_new_with_max_length (3);
  gtk_entry_set_alignment (GTK_ENTRY (entry), 1.0);
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 4);
  gtk_entry_set_editable (GTK_ENTRY (entry), TRUE);
  gtk_entry_set_text (GTK_ENTRY (entry), "0");
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 3, 4, GTK_SHRINK, GTK_SHRINK, 0, 0);
  info.y_entry = entry;

  label = gtk_label_new_with_mnemonic (_("Width:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

  entry = gtk_entry_new_with_max_length (3);
  gtk_entry_set_alignment (GTK_ENTRY (entry), 1.0);
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 4);
  gtk_entry_set_editable (GTK_ENTRY (entry), TRUE);
  gtk_entry_set_text (GTK_ENTRY (entry), "0");
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 4, 5, GTK_SHRINK, GTK_SHRINK, 0, 0);
  info.width_entry = entry;

  label = gtk_label_new_with_mnemonic (_("Height:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 5, 6, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

  entry = gtk_entry_new_with_max_length (3);
  gtk_entry_set_alignment (GTK_ENTRY (entry), 1.0);
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 4);
  gtk_entry_set_editable (GTK_ENTRY (entry), TRUE);
  gtk_entry_set_text (GTK_ENTRY (entry), "0");
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 5, 6, GTK_SHRINK, GTK_SHRINK, 0, 0);
  info.height_entry = entry;

  label = gtk_label_new_with_mnemonic (_("Lines:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 6, 7, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

  entry = gtk_entry_new_with_max_length (3);
  gtk_entry_set_alignment (GTK_ENTRY (entry), 1.0);
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 4);
  gtk_entry_set_editable (GTK_ENTRY (entry), TRUE);
  gtk_entry_set_text (GTK_ENTRY (entry), "0");
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 6, 7, GTK_SHRINK, GTK_SHRINK, 0, 0);
  info.lines_entry = entry;

  gtk_widget_show_all (table);

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  switch (res)
    {
    case GTK_RESPONSE_REJECT:
    case GTK_RESPONSE_DELETE_EVENT:
      break;
    default:
      g_warn_if_reached ();
    }

  gtk_widget_destroy (dialog);
}

static void
cursor_x_value_changed_cb (GtkSpinButton *spinner, gpointer user_data)
{
  Console *console;
  gint x_coord;

  g_assert (user_data != NULL);
  g_assert (IS_CONSOLE (user_data));

  console = user_data;

  x_coord = gtk_spin_button_get_value_as_int (spinner);

  console_move_cursor_to (console, x_coord, -1);

  console_get_cursor (console, &x_coord, NULL);
  gtk_spin_button_set_value (spinner, x_coord);
}

static void
cursor_y_value_changed_cb (GtkSpinButton *spinner, gpointer user_data)
{
  Console *console;
  gint y_coord;

  g_assert (user_data != NULL);
  g_assert (IS_CONSOLE (user_data));

  console = user_data;

  y_coord = gtk_spin_button_get_value_as_int (spinner);

  console_move_cursor_to (console, -1, y_coord);

  console_get_cursor (console, NULL, &y_coord);
  gtk_spin_button_set_value (spinner, y_coord);
}

static void
cursor_apply_button_clicked_cb (GtkWidget *widget, gpointer user_data)
{
  ConsoleCursorMoveInfo *info;
  gint x, y;

  g_assert (user_data != NULL);

  info = user_data;

  x = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (info->x_spinner));
  y = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (info->y_spinner));

  console_move_cursor_to (CONSOLE (main_console), x, y);
}

static void
console_move_cursor_dialog (GtkWidget *widget, gpointer user_data)
{
  ConsoleCursorMoveInfo info;
  Console *console;
  GtkWidget *dialog;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *spin_button;
  GtkWidget *button;
  gint res, x, y;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (user_data != NULL);
  g_return_if_fail (IS_CONSOLE (user_data));

  console = CONSOLE (user_data);

  console_get_cursor (console, &x, &y);

  dialog =
    gtk_dialog_new_with_buttons (_("Cursor Position"), GTK_WINDOW (main_window),
				 GTK_DIALOG_DESTROY_WITH_PARENT,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
				 GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
				 NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_REJECT);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

  label = gtk_label_new_with_mnemonic (_("<b>Cursor Move</b>"));
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 0);

  table = gtk_table_new (2, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 6);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), table, FALSE, FALSE, 0);

  label = gtk_label_new_with_mnemonic (_("x:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

  spin_button = gtk_spin_button_new_with_range (0, 1024, 1.0);
  gtk_table_attach (GTK_TABLE (table), spin_button, 1, 2, 0, 1, GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button), x);
  g_signal_connect (G_OBJECT (spin_button), "value-changed", (GCallback) cursor_x_value_changed_cb, console);
  info.x_spinner = spin_button;

  label = gtk_label_new_with_mnemonic (_("y:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

  spin_button = gtk_spin_button_new_with_range (0, 1024, 1.0);
  gtk_table_attach (GTK_TABLE (table), spin_button, 1, 2, 1, 2, GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_button), y);
  g_signal_connect (G_OBJECT (spin_button), "value-changed", (GCallback) cursor_y_value_changed_cb, console);
  info.y_spinner = spin_button;

  gtk_widget_show_all (table);

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  switch (res)
    {
    case GTK_RESPONSE_REJECT:
    case GTK_RESPONSE_DELETE_EVENT:
      console_move_cursor_to (console, x, y);
      break;
    case GTK_RESPONSE_ACCEPT:
      break;
    default:
      g_warn_if_reached ();
    }

  gtk_widget_destroy (dialog);
}

static void
clear_command_radio_button_toggled_cb (GtkWidget *widget, gpointer user_data)
{
  ConsoleClearInfo *info;
  gboolean clear_display;

  g_assert (user_data != NULL);

  info = user_data;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
    {
      clear_display = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "clear-display"));

      if (clear_display)
	info->clear_display = TRUE;
      else
	info->clear_display = FALSE;
    }
}

static void
clear_mode_radio_button_toggled_cb (GtkWidget *widget, gpointer user_data)
{
  ConsoleClearInfo *info;
  ConsoleEraseMode mode;

  g_assert (user_data != NULL);

  info = user_data;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
    {
      mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "erase-mode"));

      switch (mode)
	{
	case CONSOLE_ERASE_FROM_START:
	  info->mode = CONSOLE_ERASE_FROM_START;
	  break;
	case CONSOLE_ERASE_TO_END:
	  info->mode = CONSOLE_ERASE_TO_END;
	  break;
	case CONSOLE_ERASE_WHOLE:
	  info->mode = CONSOLE_ERASE_WHOLE;
	  break;
	default:
	  g_warn_if_reached ();
	}
    }
}

static void
clear_apply_button_clicked_cb (GtkWidget *widget, gpointer user_data)
{
  ConsoleClearInfo *info;

  g_assert (user_data != NULL);

  info = user_data;

  if (info->clear_display)
    console_erase_display (CONSOLE (main_console), info->mode);
  else
    console_erase_line (CONSOLE (main_console), info->mode);
}

static void
console_clear_dialog (GtkWidget *widget, gpointer user_data)
{
  ConsoleClearInfo info;
  GtkWidget *dialog;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *radio_button;
  GtkWidget *vbox;
  GSList *group = NULL;
  gint res;

  g_return_if_fail (user_data != NULL);
  g_return_if_fail (IS_CONSOLE (user_data));

  info.clear_display = FALSE;
  info.mode = CONSOLE_ERASE_FROM_START;

  dialog =
    gtk_dialog_new_with_buttons (_("Clear line or screen"), GTK_WINDOW (main_window),
				 GTK_DIALOG_DESTROY_WITH_PARENT,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
				 NULL);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  gtk_widget_set_size_request (dialog, 250, 250);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_REJECT);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

  button = gtk_button_new_from_stock (GTK_STOCK_APPLY);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), button, FALSE, FALSE, 0);
  gtk_widget_show (button);
  g_signal_connect (G_OBJECT (button), "clicked", (GCallback) clear_apply_button_clicked_cb, &info);

  label = gtk_label_new_with_mnemonic (_("<b>Clear command</b>"));
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 0);

  vbox = gtk_vbox_new (0, 6);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, FALSE, FALSE, 0);

  radio_button = gtk_radio_button_new_with_mnemonic (NULL, _("Clear line"));
  g_signal_connect (G_OBJECT (radio_button), "toggled", (GCallback) clear_command_radio_button_toggled_cb, &info);
  g_object_set_data (G_OBJECT (radio_button), "clear-display", GINT_TO_POINTER (FALSE));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio_button));

  radio_button = gtk_radio_button_new_with_mnemonic (group, _("Clear screen"));
  g_signal_connect (G_OBJECT (radio_button), "toggled", (GCallback) clear_command_radio_button_toggled_cb, &info);
  g_object_set_data (G_OBJECT (radio_button), "clear-display", GINT_TO_POINTER (TRUE));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);

  label = gtk_label_new_with_mnemonic (_("<b>Mode</b>"));
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 0);

  vbox = gtk_vbox_new (0, 6);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, FALSE, FALSE, 0);

  radio_button = gtk_radio_button_new_with_mnemonic (NULL, _("Clear to cursor"));
  g_signal_connect (G_OBJECT (radio_button), "toggled", (GCallback) clear_mode_radio_button_toggled_cb, &info);
  g_object_set_data (G_OBJECT (radio_button), "erase-mode", GINT_TO_POINTER (CONSOLE_ERASE_FROM_START));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio_button));

  radio_button = gtk_radio_button_new_with_mnemonic (group, _("Clear to end"));
  g_signal_connect (G_OBJECT (radio_button), "toggled", (GCallback) clear_mode_radio_button_toggled_cb, &info);
  g_object_set_data (G_OBJECT (radio_button), "erase-mode", GINT_TO_POINTER (CONSOLE_ERASE_TO_END));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio_button));

  radio_button = gtk_radio_button_new_with_mnemonic (group, _("Clear all"));
  g_signal_connect (G_OBJECT (radio_button), "toggled", (GCallback) clear_mode_radio_button_toggled_cb, &info);
  g_object_set_data (G_OBJECT (radio_button), "erase-mode", GINT_TO_POINTER (CONSOLE_ERASE_WHOLE));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  switch (res)
    {
      case GTK_RESPONSE_APPLY:
	break;
      case GTK_RESPONSE_REJECT:
      case GTK_RESPONSE_DELETE_EVENT:
	break;
      default:
	g_warn_if_reached ();
    }

  gtk_widget_destroy (dialog);
}

static gboolean
console_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  g_debug ("button pressed: x=%f y=%f code=%d", event->x, event->y, event->button);
}

static gboolean
console_pointer_motion_event (GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  g_debug ("motion: x=%f y=%f", event->x, event->y);

  return FALSE;
}

static gboolean
console_pointer_motion_event2 (GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  g_debug ("console mouse motion event");

  return FALSE;
}

static gboolean
console_key_press_event2 (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  Console *console;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (IS_CONSOLE (widget), FALSE);

  g_debug ("console key press event");

  return FALSE;
}

static void
console_text_selected_event_cb (GtkWidget *widget, const gchar *s, gpointer user_data)
{
  g_debug ("text-selected event callback: get string %s", s);
}

static void
console_text_pasted_event_cb (GtkWidget *widget, const gchar *s, gpointer user_data)
{
  g_debug ("text-pasted event callback: get string %s", s);
}

static gboolean
console_key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  Console *console;
  guint modifiers;
  gunichar uc;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_WINDOW (widget), FALSE);
  g_return_val_if_fail (IS_CONSOLE (user_data), FALSE);

  console = user_data;

  if (event->type == GDK_KEY_PRESS)
    {
      modifiers = gtk_accelerator_get_default_mod_mask ();

      if ((event->state & modifiers) != 0 &&
	  (event->state & modifiers) != GDK_SHIFT_MASK)
        {
          if ((event->state & modifiers) == GDK_CONTROL_MASK)
            {

            }
          else if ((event->state & modifiers) == GDK_MOD1_MASK)
            {

            }
        }
      else
        {
          switch (event->keyval)
            {
            case GDK_Return:
            case GDK_KP_Enter:
              console_put_char (console, '\r');
              console_put_char (console, '\n');
              break;
            case GDK_BackSpace:
              console_put_char (console, '\b');
              break;
            case GDK_Tab:
              console_put_char (console, '\t');
              break;
            default:
              uc = gdk_keyval_to_unicode (event->keyval);
              if (g_unichar_isprint (uc) || g_unichar_iscntrl (uc))
        	console_put_char (console, uc);
              break;
            }
        }
    }

  return FALSE;
}

struct
{
  const gchar *name;
  ConsoleBlinkTimer timer;
} blink_list[] = {
      { "steady", CONSOLE_BLINK_STEADY },
      { "slow", CONSOLE_BLINK_SLOW },
      { "medium", CONSOLE_BLINK_MEDIUM },
      { "fast", CONSOLE_BLINK_FAST }
};

struct
{
  const gchar *name;
  ConsoleCursorShape shape;
} cursor_list[] = {
      { "default",	CONSOLE_CURSOR_DEFAULT },
      { "invisible", CONSOLE_CURSOR_INVISIBLE },
      { "underscore", CONSOLE_CURSOR_UNDERSCORE },
      { "lower third", CONSOLE_CURSOR_LOWER_THIRD },
      { "lower half", CONSOLE_CURSOR_LOWER_HALF },
      { "two thirds", CONSOLE_CURSOR_TWO_THIRDS },
      { "full block", CONSOLE_CURSOR_FULL_BLOCK },
      { "vertical third", CONSOLE_CURSOR_VERT_THIRD },
      { "vertical half", CONSOLE_CURSOR_VERT_HALF },
};

int
main (int argc, char *argv[])
{
  GtkWidget *window;
  GtkWidget *console;
  GtkWidget *alignment;
  GtkWidget *vbox;
  GtkWidget *menu_bar;
  GtkWidget *status_bar;
  GtkWidget *menu_item;
  GtkWidget *menu;
  GtkWidget *cursor_menu;
  GSList *group = NULL;
  gint i;

  gtk_set_locale ();

  gtk_init (&argc, &argv);

  //gdk_window_set_debug_updates (TRUE);

  g_log_set_fatal_mask ("Gdk", G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  main_window = window;
  gtk_window_set_title (GTK_WINDOW(window), "console");
  gtk_window_set_position (GTK_WINDOW(window), GTK_WIN_POS_CENTER);
  //gtk_window_set_default_size (GTK_WINDOW(window), 800, 600);
  gtk_window_set_resizable (GTK_WINDOW (window), TRUE);

  g_signal_connect (G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

  //console = console_new ();
  console = console_new_with_size (80, 25);

  //g_signal_connect (GTK_WIDGET (console), "key-press-event", G_CALLBACK (console_key_press_event2), NULL);
  //g_signal_connect (GTK_WIDGET (console), "motion-notify-event", G_CALLBACK (console_pointer_motion_event2), NULL);

  main_console = console;

  console_set_cursor_timer (CONSOLE (console), CONSOLE_BLINK_MEDIUM);
  g_signal_connect (console, "text-selected", G_CALLBACK (console_text_selected_event_cb), console);
  g_signal_connect (console, "text-pasted", G_CALLBACK (console_text_pasted_event_cb), console);


  g_signal_connect (GTK_WIDGET (window), "key-press-event", G_CALLBACK (console_key_press_event), console);
//  g_signal_connect (GTK_WIDGET (window), "motion-notify-event", G_CALLBACK (console_pointer_motion_event), console);
//  g_signal_connect (GTK_WIDGET (window), "button-press-event", G_CALLBACK (console_button_press_event), console);

  vbox = gtk_vbox_new (FALSE, 0);

  status_bar = gtk_statusbar_new ();

  menu_bar = gtk_menu_bar_new ();

  /* font menu items */

  menu_item = gtk_menu_item_new_with_label ("Font");
  gtk_container_add (GTK_CONTAINER (menu_bar), menu_item);

  menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);

  menu_item = gtk_menu_item_new_with_label (_("Font Selection..."));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
  g_signal_connect (G_OBJECT (menu_item), "activate", (GCallback) font_selection_dialog, console);

  /* cursor menu items */

  menu_item = gtk_menu_item_new_with_label ("Cursor");
  gtk_container_add (GTK_CONTAINER (menu_bar), GTK_WIDGET (menu_item));

  menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);
  cursor_menu = menu;

  menu_item = gtk_menu_item_new_with_label ("Shape");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

  menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);

  group = NULL;

  for (i = 0; i < G_N_ELEMENTS (cursor_list); i++)
    {
      GtkWidget *item;

      item = gtk_radio_menu_item_new_with_label (group, cursor_list[i].name);
      group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));

      if (cursor_list[i].shape == CONSOLE_CURSOR_DEFAULT)
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

      g_object_set_data (G_OBJECT (item), "cursor-shape", GINT_TO_POINTER (cursor_list[i].shape));
      g_signal_connect (item, "activate", G_CALLBACK (console_cursor_shape_change), console);

      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

  menu_item = gtk_menu_item_new_with_label ("Blink period");
  gtk_menu_shell_append (GTK_MENU_SHELL (cursor_menu), menu_item);

  menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);

  group = NULL;

  for (i = 0; i < G_N_ELEMENTS (blink_list); i++)
    {
      GtkWidget *item;

      item = gtk_radio_menu_item_new_with_label (group, blink_list[i].name);
      group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));

      g_object_set_data (G_OBJECT (item), "cursor-timer", GINT_TO_POINTER (blink_list[i].timer));
      g_signal_connect (item, "activate", G_CALLBACK (console_cursor_timer_change), console);

      if (blink_list[i].timer == CONSOLE_BLINK_MEDIUM)
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

  /* command menu items */

  menu_item = gtk_menu_item_new_with_label (_("Command"));
  gtk_container_add (GTK_CONTAINER (menu_bar), menu_item);

  menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);

  menu_item = gtk_menu_item_new_with_label (_("Scroll box..."));
  g_signal_connect (G_OBJECT (menu_item), "activate", (GCallback) console_scroll_box_dialog, console);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

  menu_item = gtk_menu_item_new_with_label (_("Move cursor..."));
  g_signal_connect (G_OBJECT (menu_item), "activate", (GCallback) console_move_cursor_dialog, console);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

  menu_item = gtk_menu_item_new_with_label (_("Clear line or screen..."));
  g_signal_connect (G_OBJECT (menu_item), "activate", (GCallback) console_clear_dialog, console);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

  /* console screen size and color menu */

  menu_item = gtk_menu_item_new_with_label ("Screen");
  gtk_container_add (GTK_CONTAINER (menu_bar), menu_item);

  menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);

  menu_item = gtk_menu_item_new_with_label ("Size...");
  g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (console_size_change_dialog), console);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

  menu_item = gtk_menu_item_new_with_label ("Colors...");
  g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (console_color_change_dialog), console);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

  /* pack widgets to the main window */

  alignment = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
  gtk_container_add (GTK_CONTAINER (alignment), console);

  gtk_box_pack_start (GTK_BOX (vbox), menu_bar, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), alignment, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), status_bar, FALSE, FALSE, 0);

  gtk_statusbar_push(GTK_STATUSBAR (status_bar), gtk_statusbar_get_context_id (GTK_STATUSBAR (status_bar), "help"), "Connecting to server...");

  gtk_container_add (GTK_CONTAINER (window), vbox);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}

