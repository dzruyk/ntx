/* FontConfig API wrappers and related stuff.
 */
#ifndef __FC_H__
#define __FC_H__

G_BEGIN_DECLS


typedef gboolean (*FcListFacesFunc) (const gchar *family,
				     const gchar *style,
				     gint         width,
				     gint         weight,
				     gint         slant,
				     gpointer     user_data);

void   fc_init ();

void   fc_finalize ();

gchar* fc_synthesize_style (gint            width,
			    gint            weight,
			    gint            slant);

void   fc_get_matched      (const gchar    *family,
			    const gchar    *style,
			    gboolean        monospaced,
			    gboolean        scalable,
			    gchar         **matched_family,
			    gchar         **matched_style);

void   fc_get_font_file    (const gchar    *family,
			    const gchar    *style,
			    gboolean        monospaced,
			    gboolean        scalable,
			    gchar         **file,
			    gint           *face_index);

void   fc_list_faces       (gboolean        monospaced,
			    gboolean        scalable,
			    FcListFacesFunc callback,
			    gpointer        user_data);


G_END_DECLS

#endif /* __FC_H__ */
