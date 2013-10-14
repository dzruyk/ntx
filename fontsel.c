/* ConsoleFontSelection -- custom pangoless font selection widget
 *
 * Copyright (C) 2012 Grisha Sitkarev
 */
#define GETTEXT_PACKAGE "gtk20"
#include <glib/gi18n-lib.h>

#include "fontsel.h"
#include "fc.h"

#define FONT_LIST_WIDTH		180
#define FONT_LIST_HEIGHT	170
#define STYLE_LIST_WIDTH	180
#define STYLE_LIST_HEIGHT	170

enum
{
  PROP_0,
  PROP_FONT,
  PROP_STYLE,
  PROP_SIZE
};

enum
{
  FONTLIST_FAMILY_COLUMN,
  FONTLIST_STYLES_COLUMN,
  FONTLIST_N_COLUMNS
};

enum
{
  STYLELIST_STYLE_COLUMN,
  STYLELIST_N_COLUMNS
};

static const guint16 font_sizes[] = {
  6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
  20, 22, 24, 26, 28,	/* 2 increment */
  32, 36, 40,		/* 4 increment */
  48, 56, 64, 72	/* 8 increment */
};

static void    console_font_selection_set_property    (GObject	       *object,
						       guint            prop_id,
						       const GValue    *value,
						       GParamSpec      *pspec);
static void    console_font_selection_get_property    (GObject         *object,
						       guint            prop_id,
						       GValue          *value,
						       GParamSpec      *pspec);
static void    console_font_selection_finalize         (GObject        *object);

static void    size_list_set_cursor_to_nearest        (ConsoleFontSelection *fontsel,
						       gint                  size);
static void    family_list_set_cursor                 (ConsoleFontSelection *fontsel,
						       const gchar          *family);
static void    style_list_set_cursor                  (ConsoleFontSelection *fontsel,
						       const gchar          *style);


G_DEFINE_TYPE (ConsoleFontSelection, console_font_selection, GTK_TYPE_VBOX)

static void
console_font_selection_class_init (ConsoleFontSelectionClass *klass)
{
  GParamSpec *pspec;

  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->set_property = console_font_selection_set_property;
  gobject_class->get_property = console_font_selection_get_property;

  pspec = g_param_spec_int ("size", "Font Size",
			    "The size of the font in points",
			    0, 72,
			    10,
			    G_PARAM_READWRITE);

  g_object_class_install_property (gobject_class, PROP_SIZE, pspec);

  pspec = g_param_spec_string ("font", "Font Face",
			       "The selected font face",
			       "monospace",
			       G_PARAM_READWRITE);

  g_object_class_install_property (gobject_class, PROP_FONT, pspec);

  pspec = g_param_spec_string ("style", "Font Style",
			       "The selected font style",
			       "regular",
			       G_PARAM_READWRITE);

  g_object_class_install_property (gobject_class, PROP_STYLE, pspec);

  gobject_class->finalize = console_font_selection_finalize;
}

void
console_font_selection_set_family (ConsoleFontSelection *fontsel,
			 	   const gchar          *family)
{
  gchar *canonical_family;

  g_return_if_fail (CONSOLE_IS_FONT_SELECTION (fontsel));
  g_return_if_fail (family != NULL);

  if (fontsel->family != NULL)
    g_free (fontsel->family);

  fontsel->family = g_strdup (family);

  fc_get_matched (fontsel->family, fontsel->style, TRUE, TRUE, &canonical_family, NULL);
  family_list_set_cursor (fontsel, canonical_family);
  g_free (canonical_family);
}

void
console_font_selection_set_style (ConsoleFontSelection *fontsel,
				  const gchar          *style)
{
  gchar *canonical_style;

  g_return_if_fail (CONSOLE_IS_FONT_SELECTION (fontsel));
  g_return_if_fail (style != NULL);

  if (fontsel->style != NULL)
    g_free (fontsel->style);

  fontsel->style = g_strdup (style);

  fc_get_matched (fontsel->family, fontsel->style, TRUE, TRUE, NULL, &canonical_style);
  style_list_set_cursor (fontsel, canonical_style);
  g_free (canonical_style);
}

void
console_font_selection_set_size (ConsoleFontSelection *fontsel,
		       		 gint                  size)
{
  gchar text[64];

  g_return_if_fail (CONSOLE_IS_FONT_SELECTION (fontsel));
  g_return_if_fail (size >= 0);

  fontsel->size = size;

  g_snprintf (text, sizeof (text), "%u", size);
  gtk_entry_set_text (GTK_ENTRY (fontsel->size_entry), text);
  size_list_set_cursor_to_nearest (fontsel, size);
}

static void
console_font_selection_set_property (GObject	     *object,
				     guint            prop_id,
				     const GValue    *value,
				     GParamSpec	     *pspec)
{
  ConsoleFontSelection *fontsel = CONSOLE_FONT_SELECTION (object);

  switch (prop_id)
    {
    case PROP_FONT:
      console_font_selection_set_family (fontsel, g_value_get_string (value));
      break;

    case PROP_STYLE:
      console_font_selection_set_style (fontsel, g_value_get_string (value));
      break;

    case PROP_SIZE:
      console_font_selection_set_size (fontsel, g_value_get_int (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
console_font_selection_get_property (GObject	   *object,
				     guint          prop_id,
				     GValue        *value,
				     GParamSpec	   *pspec)
{
  ConsoleFontSelection *fontsel;
  gint size;
  gchar *str;

  fontsel = CONSOLE_FONT_SELECTION (object);

  switch (prop_id)
    {
    case PROP_FONT:
      g_value_set_string (value, console_font_selection_get_family (fontsel)); 
      break;

    case PROP_STYLE:
      g_value_set_string (value, console_font_selection_get_style (fontsel));
      break;

    case PROP_SIZE:
      g_value_set_int (value, console_font_selection_get_size (fontsel)); 
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

gint
console_font_selection_get_size (ConsoleFontSelection *fontsel)
{

  g_return_val_if_fail (fontsel != NULL, -1);
  g_return_val_if_fail (CONSOLE_IS_FONT_SELECTION (fontsel), -1);

  return fontsel->size;
}

const gchar *
console_font_selection_get_family (ConsoleFontSelection *fontsel)
{
  g_return_val_if_fail (fontsel != NULL, NULL);
  g_return_val_if_fail (CONSOLE_IS_FONT_SELECTION (fontsel), NULL);

  if (fontsel->selected_family)
    return fontsel->selected_family;
  else
    return NULL;
}

const gchar *
console_font_selection_get_style (ConsoleFontSelection *fontsel)
{
  g_return_val_if_fail (fontsel != NULL, NULL);
  g_return_val_if_fail (CONSOLE_IS_FONT_SELECTION (fontsel), NULL);

  if (fontsel->selected_style)
    return fontsel->selected_style;
  else
    return NULL;
}

/* This sets cursor of the size list to best matching size or none.
 */
static void
size_list_set_cursor_to_nearest (ConsoleFontSelection *fontsel, gint size)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (font_sizes); i++)
    {
      if (size == font_sizes[i])
	{
	  GtkTreePath *path;

	  path = gtk_tree_path_new_from_indices (i, -1);
	  gtk_tree_view_set_cursor (GTK_TREE_VIEW (fontsel->size_list), path, NULL, FALSE);
	  gtk_tree_path_free (path);
	  
	  break;
	}
    }
}

static void
style_list_set_cursor (ConsoleFontSelection *fontsel, const gchar *style)
{
  GtkTreePath *path;
  GtkTreeModel *model;
  GtkTreeIter iter;

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (fontsel->style_list));

  g_assert (model != NULL);

  /* Set cursor to the first entry in the style list.
   * When no better choice exists, the cursor will stay there unmodified.
   */
  path = gtk_tree_path_new_from_indices (0, -1);
  gtk_tree_view_set_cursor (GTK_TREE_VIEW (fontsel->style_list), path, NULL, FALSE);
  gtk_tree_path_free (path);

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      g_assert (GTK_IS_LIST_STORE (model));

      do
	{
	  gchar *style_from_list;

	  gtk_tree_model_get (model, &iter, STYLELIST_STYLE_COLUMN, &style_from_list, -1);

	  if (g_strcmp0 (style_from_list, style) == 0)
	    {
	      GtkTreePath *path;

	      path = gtk_tree_model_get_path (model, &iter);
	      gtk_tree_view_set_cursor (GTK_TREE_VIEW (fontsel->style_list), path, NULL, FALSE);
	      gtk_tree_path_free (path);

	      if (fontsel->selected_style)
		g_free (fontsel->selected_style);
	      fontsel->selected_style = style_from_list;

	      break;
	    }

	  g_free (style_from_list);

	} while (gtk_tree_model_iter_next (model, &iter));
    }
}

static void
family_list_set_cursor (ConsoleFontSelection *fontsel, const gchar *family)
{
  GtkTreeModel *model;
  GtkTreeIter iter;

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (fontsel->family_list));

  g_assert (model != NULL);

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      g_assert (GTK_IS_LIST_STORE (model));

      do
	{
	  gchar *family_from_list;

	  gtk_tree_model_get (model, &iter, FONTLIST_FAMILY_COLUMN, &family_from_list, -1);

	  if (g_strcmp0 (family_from_list, family) == 0)
	    {
	      GtkTreePath *path;
	      GtkTreeSelection *selection;

	      path = gtk_tree_model_get_path (model, &iter);
	      gtk_tree_view_set_cursor (GTK_TREE_VIEW (fontsel->family_list), path, NULL, FALSE);
	      gtk_tree_path_free (path);

	      if (fontsel->selected_family)
		g_free (fontsel->selected_family);
	      fontsel->selected_family = family_from_list;

	      break;
	    }

	  g_free (family_from_list);

	} while (gtk_tree_model_iter_next (model, &iter));
    }
}

/* This is called when the font list becomes visible.
 * It sets cursors at the font and style lists and sets size entry value.
 */
static void
family_list_map_cb (GtkWidget *widget, gpointer user_data)
{
  GtkTreeModel *model;
  ConsoleFontSelection *fontsel;
  GtkTreeIter iter;
  gchar buf[64];
  gchar *family;
  gint weight, slant;
  gchar *style;

  fontsel = CONSOLE_FONT_SELECTION (user_data);

  /* Lookup canonical names for the user-suppled font and style. */
  fc_get_matched (fontsel->family, fontsel->style, TRUE, TRUE, &family, &style);

  g_snprintf (buf, sizeof (buf), "%u", fontsel->size);
  gtk_entry_set_text (GTK_ENTRY (fontsel->size_entry), buf);

  /* Set cursor in the size list to nearest match. */
  size_list_set_cursor_to_nearest (fontsel, fontsel->size);

  /* Set cursor in the family list. */
  family_list_set_cursor (fontsel, family);

  /* Set cursor in the style list. */
  style_list_set_cursor (fontsel, style);

  g_free (family);
  g_free (style);
}

/* This is called when user double-clicks any list row.
 * It activates the default widget of the top-level container.
 */
static void
list_row_activated_cb (GtkTreeView       *tree_view,
                       GtkTreePath       *path,
                       GtkTreeViewColumn *column,
                       gpointer           user_data)
{
  GtkWindow *window;
  GtkWidget *widget;

  widget = GTK_WIDGET (tree_view);
  window = GTK_WINDOW (gtk_widget_get_toplevel (widget));

  if (!GTK_WIDGET_TOPLEVEL (window))
    window = NULL;

  if (window != NULL &&
      (window->focus_widget != widget ||
       (window->default_widget != NULL && gtk_widget_is_sensitive (window->default_widget))))
    {
      gtk_window_activate_default (window);
    }
}

/* This extracts the value set by user from the size entry and applies it.
 */
static void
size_entry_apply (ConsoleFontSelection *fontsel)
{
  GtkTreeSelection *selection;
  gchar buf[64];
  gint i, size;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (fontsel->size_list));
  gtk_tree_selection_unselect_all (selection);

  size = g_ascii_strtoll (gtk_entry_get_text (GTK_ENTRY (fontsel->size_entry)), NULL, 10);
  g_snprintf (buf, sizeof (buf), "%u", size);
  gtk_entry_set_text (GTK_ENTRY (fontsel->size_entry), buf);

  size_list_set_cursor_to_nearest (fontsel, size);

  fontsel->size = size;
}

static void
size_entry_activate_cb (GtkWidget *widget, gpointer user_data)
{
  ConsoleFontSelection *fontsel;

  g_assert (widget != NULL);
  g_assert (user_data != NULL);
  g_assert (GTK_IS_ENTRY (widget));
  g_assert (CONSOLE_IS_FONT_SELECTION (user_data));

  fontsel = user_data;

  size_entry_apply (fontsel);
}

static gboolean
size_entry_focus_out_cb (GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
  ConsoleFontSelection *fontsel;

  g_assert (widget != NULL);
  g_assert (user_data != NULL);
  g_assert (GTK_IS_ENTRY (widget));
  g_assert (CONSOLE_IS_FONT_SELECTION (user_data));

  fontsel = user_data;

  size_entry_apply (fontsel);

  return FALSE;
}

static void
size_list_cursor_changed_cb (GtkTreeView *tree, gpointer user_data)
{
  ConsoleFontSelection *fontsel;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;

  fontsel = CONSOLE_FONT_SELECTION (user_data);
  selection = gtk_tree_view_get_selection (tree);
  model = gtk_tree_view_get_model (tree);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      gchar buf[64];
      gint size;

      gtk_tree_model_get (model, &iter, 0, &size, -1);

      g_snprintf (buf, sizeof (buf), "%u", size);
      gtk_entry_set_text (GTK_ENTRY (fontsel->size_entry), buf);

      fontsel->size = size;
    }
}

static void
family_list_destroy_cb (GtkWidget *widget, gpointer user_data)
{
  GtkTreeModel *model;
  GtkTreeIter iter;

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      do
	{
	  GSList *list, *item;

	  gtk_tree_model_get (model, &iter, FONTLIST_STYLES_COLUMN, &list, -1);

	  item = list;
	  while (item != NULL)
	    {
	      if (item->data != NULL)
	        g_free (item->data);
	      item = item->next;
	    }

	  g_slist_free (list);

	} while (gtk_tree_model_iter_next (model, &iter));
    }
}

/* This is called when user selects a font in the font list.
 */
static void
family_list_cursor_changed_cb (GtkTreeView *tree, gpointer user_data)
{
  ConsoleFontSelection *fontsel;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GSList *item, *styles;
  gchar *family_from_list;
  gchar *default_style;

  fontsel = CONSOLE_FONT_SELECTION (user_data);

  /* Get the list of styles for the selected font family.
   */
  model = gtk_tree_view_get_model (tree);
  selection = gtk_tree_view_get_selection (tree);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      gtk_tree_model_get (model, &iter, FONTLIST_FAMILY_COLUMN, &family_from_list, FONTLIST_STYLES_COLUMN, &styles, -1);

      if (fontsel->selected_family)
	g_free (fontsel->selected_family);
      fontsel->selected_family = family_from_list;

      /* Fill the style list store with available styles.
       */
      model = gtk_tree_view_get_model (GTK_TREE_VIEW (fontsel->style_list));
      gtk_list_store_clear (GTK_LIST_STORE (model));

      for (item = styles; item != NULL; item = item->next)
	{
	  GtkTreeIter iter;
	  gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	  gtk_list_store_set (GTK_LIST_STORE (model), &iter, STYLELIST_STYLE_COLUMN, item->data, -1);
	}

      /* Set cursor in the style list view to the default style.
       */
      fc_get_matched (fontsel->family, NULL, TRUE, TRUE, NULL, &default_style);
      style_list_set_cursor (fontsel, default_style);
      g_free (default_style);
    }
}

static void
add_font_family (gpointer key, gpointer value, gpointer user_data)
{
  GtkListStore *model;
  GtkTreeIter iter;
  GSList *styles;
  const char *family;

  g_assert (key != NULL && value != NULL);
  g_assert (user_data != NULL);
  g_assert (GTK_IS_LIST_STORE (user_data));

  model = user_data;
  family = key;
  styles = value;

  gtk_list_store_append (model, &iter);

  gtk_list_store_set (model, &iter, FONTLIST_FAMILY_COLUMN, family, FONTLIST_STYLES_COLUMN, styles, -1);
}

static gboolean
insert_face (const gchar *family,
	     const gchar *style,
	     gint         width,
	     gint         weight,
	     gint         slant,
	     gpointer     user_data)
{
  GHashTable *table = user_data;
  GSList *list = NULL;

  list = g_hash_table_lookup (table, family);

  if (list == NULL)
    {
      list = g_slist_append (NULL, g_strdup (style)); 
      g_hash_table_insert (table, g_strdup (family), list);
    }
  else
    {
      GSList *iter;

      /* avoid duplicated styles in the font family */

      iter = list;
      while (iter != NULL)
	{
	  if (g_strcmp0 (iter->data, style) == 0)
	    break;
	  iter = iter->next;
	}

      if (iter == NULL)
	{
	  list = g_slist_append (list, g_strdup (style)); 
	  g_hash_table_replace (table, g_strdup (family), list);
	}
    }

  return FALSE;
}

static void
populate_font_list (GtkListStore *model)
{
  GHashTable *table;
  gint i;

  table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* Get all the faces and styles in the hash table. */
  fc_list_faces (TRUE, TRUE, insert_face, table);

  /* Add each family and a list of styles to the list store. */
  g_hash_table_foreach (table, add_font_family, model);

  /* We don't need the hash table any more. */
  g_hash_table_destroy (table);
}

static void
console_font_selection_init (ConsoleFontSelection *fontsel)
{
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *scrolled_win;
  GtkWidget *view;
  GtkWidget *vbox;
  GtkListStore *model;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  gint i;

  fontsel->size = 10;
  fontsel->family = NULL;
  fontsel->style = NULL;
  fontsel->selected_family = NULL;
  fontsel->selected_style = NULL;

  gtk_box_set_spacing (GTK_BOX (fontsel), 12);

  table = gtk_table_new (2, 3, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_table_set_col_spacings (GTK_TABLE (table), 12);

  label = gtk_label_new_with_mnemonic (_("Family:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);

  label = gtk_label_new_with_mnemonic (_("Style:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 1, 2, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);

  label = gtk_label_new_with_mnemonic (_("Size:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table),label, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);

  vbox = gtk_vbox_new (FALSE, 6);

  fontsel->size_entry = gtk_entry_new ();
  gtk_entry_set_max_length (GTK_ENTRY (fontsel->size_entry), 6);
  gtk_entry_set_width_chars (GTK_ENTRY (fontsel->size_entry), 6);

  gtk_box_pack_start (GTK_BOX (vbox), fontsel->size_entry, FALSE, FALSE, 0);

  gtk_table_attach (GTK_TABLE (table), vbox, 2, 3, 1, 2, 0, GTK_FILL | GTK_EXPAND, 0, 0);

  model = gtk_list_store_new (FONTLIST_N_COLUMNS, G_TYPE_STRING, G_TYPE_POINTER);

  populate_font_list (model);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model), FONTLIST_FAMILY_COLUMN, GTK_SORT_ASCENDING);

  fontsel->family_list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (fontsel->family_list), FALSE);
  gtk_widget_set_size_request (fontsel->family_list, FONT_LIST_WIDTH, FONT_LIST_HEIGHT);

  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (NULL, renderer, "text", FONTLIST_FAMILY_COLUMN, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (fontsel->family_list), column);

  scrolled_win = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_win), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (scrolled_win), fontsel->family_list);
  gtk_table_attach_defaults (GTK_TABLE (table), scrolled_win, 0, 1, 1, 2);

  model = gtk_list_store_new (STYLELIST_N_COLUMNS, G_TYPE_STRING);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model), STYLELIST_STYLE_COLUMN, GTK_SORT_ASCENDING);

  fontsel->style_list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (fontsel->style_list), FALSE);
  gtk_widget_set_size_request (fontsel->style_list, STYLE_LIST_WIDTH, STYLE_LIST_HEIGHT);

  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (NULL, renderer, "text", STYLELIST_STYLE_COLUMN, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (fontsel->style_list), column);

  scrolled_win = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_win), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (scrolled_win), fontsel->style_list);
  gtk_table_attach_defaults (GTK_TABLE (table), scrolled_win, 1, 2, 1, 2);

  model = gtk_list_store_new (1, G_TYPE_INT);

  for (i = 0; i < G_N_ELEMENTS (font_sizes); i++)
    {
      GtkTreeIter iter;

      gtk_list_store_append (model, &iter);
      gtk_list_store_set (model, &iter, 0, font_sizes[i], -1);
    }

  fontsel->size_list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (fontsel->size_list), FALSE);

  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (NULL, renderer, "text", 0, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (fontsel->size_list), column);

  scrolled_win = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_win), GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled_win), fontsel->size_list);

  gtk_box_pack_start (GTK_BOX (vbox), scrolled_win, TRUE, TRUE, 0);

  gtk_widget_show_all (table);

  gtk_container_add (GTK_CONTAINER (fontsel), table);

  g_signal_connect (G_OBJECT (fontsel->family_list),
		    "cursor-changed", G_CALLBACK (family_list_cursor_changed_cb), fontsel);
  g_signal_connect (G_OBJECT (fontsel->family_list),
		    "destroy", G_CALLBACK (family_list_destroy_cb), fontsel);
  g_signal_connect (G_OBJECT (fontsel->family_list),
		    "map", G_CALLBACK (family_list_map_cb), fontsel); 
  g_signal_connect (G_OBJECT (fontsel->family_list),
		    "row-activated", G_CALLBACK (list_row_activated_cb), fontsel);

  g_signal_connect (G_OBJECT (fontsel->size_list),
		    "cursor-changed", G_CALLBACK (size_list_cursor_changed_cb), fontsel);
  g_signal_connect (G_OBJECT (fontsel->size_list),
		    "row-activated", G_CALLBACK (list_row_activated_cb), fontsel);

  g_signal_connect (G_OBJECT (fontsel->size_entry),
		    "activate", G_CALLBACK (size_entry_activate_cb), fontsel);
  g_signal_connect (G_OBJECT (fontsel->size_entry),
		    "focus-out-event", G_CALLBACK (size_entry_focus_out_cb), fontsel);

  g_signal_connect (G_OBJECT (fontsel->style_list),
		    "row-activated", G_CALLBACK (list_row_activated_cb), fontsel);
}

static void
console_font_selection_finalize (GObject *object)
{
  ConsoleFontSelection *fontsel;

  g_return_if_fail (CONSOLE_IS_FONT_SELECTION (object));

  fontsel = CONSOLE_FONT_SELECTION (object);

  if (fontsel->family != NULL)
    {
      g_free (fontsel->family);
      fontsel->family = NULL;
    }

  if (fontsel->style != NULL)
    {
      g_free (fontsel->style);
      fontsel->style = NULL;
    }

  if (fontsel->selected_family != NULL)
    {
      g_free (fontsel->selected_family);
      fontsel->selected_family = NULL;
    }

  if (fontsel->selected_style != NULL)
    {
      g_free (fontsel->selected_style);
      fontsel->selected_style = NULL;
    }

  G_OBJECT_CLASS (console_font_selection_parent_class)->finalize (object);
}

GtkWidget *
console_font_selection_new (void)
{
  ConsoleFontSelection *fontsel;

  fontsel = g_object_new (CONSOLE_TYPE_FONT_SELECTION, NULL);

  return GTK_WIDGET (fontsel);
}

/* ConsoleFontSelectionDialog implementation on top of GtkDialog.
 */

G_DEFINE_TYPE (ConsoleFontSelectionDialog, console_font_selection_dialog, GTK_TYPE_DIALOG)

static void
console_font_selection_dialog_class_init (ConsoleFontSelectionDialogClass *klass)
{

}

static void
console_font_selection_dialog_init (ConsoleFontSelectionDialog *dialog)
{
  GtkDialog *parent;
  GtkVBox *vbox;

  parent = GTK_DIALOG (dialog);

  gtk_dialog_set_has_separator (parent, FALSE);

  vbox = GTK_VBOX (parent->vbox);
  gtk_container_set_border_width (GTK_CONTAINER (parent), 5);
  gtk_box_set_spacing (GTK_BOX (vbox), 2);
  gtk_container_set_border_width (GTK_CONTAINER (parent->action_area), 5);
  gtk_box_set_spacing (GTK_BOX (parent->action_area), 6);

  gtk_window_set_resizable (GTK_WINDOW (parent), TRUE);

  dialog->ok_button = gtk_dialog_add_button (parent, GTK_STOCK_OK, GTK_RESPONSE_OK);

  dialog->cancel_button = gtk_dialog_add_button (parent, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

  gtk_dialog_set_alternative_button_order (parent, GTK_RESPONSE_OK, GTK_RESPONSE_CANCEL, -1);

  gtk_dialog_set_default_response (parent, GTK_RESPONSE_OK);
  gtk_window_set_focus (GTK_WINDOW (parent), dialog->ok_button);
  gtk_window_set_default (GTK_WINDOW (parent), dialog->ok_button);

  dialog->fontsel = console_font_selection_new ();

  gtk_widget_show (dialog->fontsel);

  gtk_container_set_border_width (GTK_CONTAINER (dialog->fontsel), 5);

  gtk_box_pack_start (GTK_BOX (vbox), dialog->fontsel, TRUE, TRUE, 0);
}

GtkWidget *
console_font_selection_dialog_new ()
{
  GtkWidget *dial;

  dial = g_object_new (CONSOLE_TYPE_FONT_SELECTION_DIALOG, NULL);

  gtk_window_set_title (GTK_WINDOW (dial), _("Font Selection"));

  return GTK_WIDGET (dial);
}

void
console_font_selection_dialog_set_family (ConsoleFontSelectionDialog *dialog,
					  const gchar                *family)
{
  g_return_if_fail (CONSOLE_IS_FONT_SELECTION_DIALOG (dialog));

  console_font_selection_set_family (CONSOLE_FONT_SELECTION (dialog->fontsel), family);
}

void
console_font_selection_dialog_set_style (ConsoleFontSelectionDialog *dialog,
					 const gchar                *style)
{
  g_return_if_fail (CONSOLE_IS_FONT_SELECTION_DIALOG (dialog));

  console_font_selection_set_style (CONSOLE_FONT_SELECTION (dialog->fontsel), style);
}

void
console_font_selection_dialog_set_size (ConsoleFontSelectionDialog *dialog,
					gint                        size)
{
  g_return_if_fail (CONSOLE_IS_FONT_SELECTION_DIALOG (dialog));

  console_font_selection_set_size (CONSOLE_FONT_SELECTION (dialog->fontsel), size);
}

gint
console_font_selection_dialog_get_size (ConsoleFontSelectionDialog *dialog)
{
  g_return_val_if_fail (dialog != NULL, -1);
  g_return_val_if_fail (CONSOLE_IS_FONT_SELECTION_DIALOG (dialog), -1);

  return console_font_selection_get_size (CONSOLE_FONT_SELECTION (dialog->fontsel));
}

const gchar *
console_font_selection_dialog_get_family (ConsoleFontSelectionDialog *dialog)
{

  g_return_val_if_fail (dialog != NULL, NULL);
  g_return_val_if_fail (CONSOLE_IS_FONT_SELECTION_DIALOG (dialog), NULL);

  return console_font_selection_get_family (CONSOLE_FONT_SELECTION (dialog->fontsel));
}

const gchar *
console_font_selection_dialog_get_style (ConsoleFontSelectionDialog *dialog)
{

  g_return_val_if_fail (dialog != NULL, NULL);
  g_return_val_if_fail (CONSOLE_IS_FONT_SELECTION_DIALOG (dialog), NULL);

  return console_font_selection_get_style (CONSOLE_FONT_SELECTION (dialog->fontsel));
}

