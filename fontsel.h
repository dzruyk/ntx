#ifndef __CONSOLE_FONTSEL_H__
#define __CONSOLE_FONTSEL_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CONSOLE_TYPE_FONT_SELECTION		(console_font_selection_get_type ())
#define CONSOLE_FONT_SELECTION(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), CONSOLE_TYPE_FONT_SELECTION, ConsoleFontSelection))
#define CONSOLE_FONT_SELECTION_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), CONSOLE_TYPE_FONT_SELECTION, ConsoleFontSelectionClass))
#define CONSOLE_IS_FONT_SELECTION(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), CONSOLE_TYPE_FONT_SELECTION))
#define CONSOLE_IS_FONT_SELECTION_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), CONSOLE_TYPE_FONT_SELECTION))
#define CONSOLE_FONT_SELECTION_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), CONSOLE_TYPE_FONT_SELECTION, ConsoleFontSelectionClass))


typedef struct _ConsoleFontSelection		ConsoleFontSelection;
typedef struct _ConsoleFontSelectionClass	ConsoleFontSelectionClass;

struct _ConsoleFontSelection
{
  GtkVBox parent_instance;

  GtkWidget *family_list;
  GtkWidget *style_list;
  GtkWidget *size_entry;
  GtkWidget *size_list;

  gint size;
  gchar *family;
  gchar *style;

  gchar *selected_family;
  gchar *selected_style;
};

struct _ConsoleFontSelectionClass
{
  GtkVBoxClass parent_class;
};

GtkWidget* console_font_selection_new ();

gint         console_font_selection_get_size     (ConsoleFontSelection    *fontsel);

const gchar* console_font_selection_get_family   (ConsoleFontSelection    *fontsel);

const gchar* console_font_selection_get_style    (ConsoleFontSelection    *fontsel);

void         console_font_selection_set_size     (ConsoleFontSelection    *fontsel,
	  					  gint                     size);
void         console_font_selection_set_family   (ConsoleFontSelection    *fontsel,
						  const gchar             *family);
void         console_font_selection_set_style    (ConsoleFontSelection    *fontsel,
						  const gchar             *style);


#define CONSOLE_TYPE_FONT_SELECTION_DIALOG		(console_font_selection_dialog_get_type ())
#define CONSOLE_FONT_SELECTION_DIALOG(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), CONSOLE_TYPE_FONT_SELECTION_DIALOG, ConsoleFontSelectionDialog))
#define CONSOLE_FONT_SELECTION_DIALOG_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), CONSOLE_TYPE_FONT_SELECTION_DIALOG, ConsoleFontSelectionDialogClass))
#define CONSOLE_IS_FONT_SELECTION_DIALOG(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), CONSOLE_TYPE_FONT_SELECTION_DIALOG))
#define CONSOLE_IS_FONT_SELECTION_DIALOG_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), CONSOLE_TYPE_FONT_SELECTION_DIALOG))
#define CONSOLE_FONT_SELECTION_DIALOG_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), CONSOLE_TYPE_FONT_SELECTION_DIALOG, ConsoleFontSelectionDialogClass))

typedef struct _ConsoleFontSelectionDialog ConsoleFontSelectionDialog;
typedef struct _ConsoleFontSelectionDialogClass ConsoleFontSelectionDialogClass;

struct _ConsoleFontSelectionDialog
{
  GtkDialog parent_instance;

  GtkWidget *fontsel;	/* ConsoleFontSelection widget */

  GtkWidget *ok_button;
  GtkWidget *cancel_button;
};

struct _ConsoleFontSelectionDialogClass
{
  GtkDialogClass parent_class;
};

GtkWidget*   console_font_selection_dialog_new ();

gint         console_font_selection_dialog_get_size   (ConsoleFontSelectionDialog *fontsel);

const gchar* console_font_selection_dialog_get_family (ConsoleFontSelectionDialog *fontsel);

const gchar* console_font_selection_dialog_get_style  (ConsoleFontSelectionDialog *fontsel);

void         console_font_selection_dialog_set_size   (ConsoleFontSelectionDialog *fontsel,
						       gint                        size);

void         console_font_selection_dialog_set_family (ConsoleFontSelectionDialog *fontsel,
						       const gchar                *family);

void         console_font_selection_dialog_set_style  (ConsoleFontSelectionDialog *fontsel,
					 	       const gchar                *style);


G_END_DECLS


#endif /* __CONSOLE_FONTSEL_H__ */
