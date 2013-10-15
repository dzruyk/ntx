#include <glib.h>
#include <fontconfig/fontconfig.h>

#include "fc.h"

#define DEFAULT_FAMILY		 "sans"
#define DEFAULT_STYLE		 "regular"
#define DEFAULT_MONOSPACE_FAMILY "sans mono"
#define DEFAULT_MONOSPACE_STYLE  "regular"

/* This converts FontConfig slant FC_SLANT_xxx to string.
 */
static const gchar*
slant_to_string (gint slant)
{
  switch (slant)
    {
    case FC_SLANT_ITALIC:
      return "Italic";
    case FC_SLANT_OBLIQUE:
      return "Oblique";
    case FC_SLANT_ROMAN:
      return "Roman";
    default:
      g_warn_if_reached ();
    }
  return NULL;
}

/* This converts FontConfig weight FC_WEIGHT_xxx to string.
 */
static const gchar*
weight_to_string (gint weight)
{
  switch (weight)
    {
    case FC_WEIGHT_THIN:
      return "Thin";
    case FC_WEIGHT_EXTRALIGHT:
      return "Extralight";
    case FC_WEIGHT_LIGHT:
      return "Light";
    case FC_WEIGHT_BOOK:
      return "Book";
    case FC_WEIGHT_REGULAR:
      return "Regular";
    case FC_WEIGHT_MEDIUM:
      return "Medium";
    case FC_WEIGHT_DEMIBOLD:
      return "Demibold";
    case FC_WEIGHT_BOLD:
      return "Bold";
    case FC_WEIGHT_EXTRABOLD:
      return "Extrabold";
    case FC_WEIGHT_BLACK:
      return "Black";
    case FC_WEIGHT_EXTRABLACK:
      return "Extrablack";
    default:
      g_warn_if_reached ();
    }
  return NULL;
}

/* This converts FontConfig FC_WIDTH_xxx to string.
 */
static const gchar*
width_to_string (gint width)
{
  switch (width)
    {
    case FC_WIDTH_ULTRACONDENSED:
      return "Ultracondensed";
    case FC_WIDTH_EXTRACONDENSED:
      return "Extracondensed";
    case FC_WIDTH_CONDENSED:
      return "Condensed";
    case FC_WIDTH_SEMICONDENSED:
      return "Semicondensed";
    case FC_WIDTH_NORMAL:
      return "Normal";
    case FC_WIDTH_SEMIEXPANDED:
      return "Semiexpanded";
    case FC_WIDTH_EXPANDED:
      return "Expanded";
    case FC_WIDTH_EXTRAEXPANDED:
      return "Extraexpanded";
    case FC_WIDTH_ULTRAEXPANDED:
      return "Ultraexpanded";
    default:
	g_warn_if_reached ();
    }
  return NULL;
}

void
fc_init ()
{
  FcInit ();
}

void
fc_finalize ()
{
  FcFini ();
}

gchar*
fc_synthesize_style (gint width, gint weight, gint slant)
{
  gchar *s = NULL;

  if (slant != FC_SLANT_ROMAN)
    {
      if (width != FC_WIDTH_NORMAL)
	s = g_strdup_printf ("%s %s %s", width_to_string (width), weight_to_string (weight), slant_to_string (slant));
      else
	s = g_strdup_printf ("%s %s", weight_to_string (weight), slant_to_string (slant));
    }
  else
    {
      if (width != FC_WIDTH_NORMAL)
	s = g_strdup_printf ("%s %s", width_to_string (width), weight_to_string (weight));
      else
	s = g_strdup (weight_to_string (weight));
    }

  return s;
}

void
fc_get_matched (const gchar  *family,
 	        const gchar  *style,
	        gboolean      monospaced,
	        gboolean      scalable,
	        gchar       **matched_family,
	        gchar       **matched_style)
{
  FcPattern *pattern;
  FcPattern *match;
  FcResult res;
  gint weight, width, slant;

  pattern = FcPatternCreate ();

  if (monospaced)
    FcPatternAddBool (pattern, FC_SCALABLE, FcTrue);

  if (scalable)
    FcPatternAddInteger (pattern, FC_SPACING, FC_MONO);

  if (family != NULL)
      FcPatternAddString (pattern, FC_FAMILY, (const FcChar8 *)family);
  else
    {
      if (monospaced)
	FcPatternAddString (pattern, FC_FAMILY, (const FcChar8 *)DEFAULT_MONOSPACE_FAMILY);
      else
        FcPatternAddString (pattern, FC_FAMILY, (const FcChar8 *)DEFAULT_FAMILY);
    }

  if (style != NULL)
    FcPatternAddString (pattern, FC_STYLE, (const FcChar8 *)style);
  else
    {
      if (monospaced)
	FcPatternAddString (pattern, FC_STYLE, (const FcChar8 *)DEFAULT_MONOSPACE_STYLE);
      else
	FcPatternAddString (pattern, FC_STYLE, (const FcChar8 *)DEFAULT_STYLE);
    }

  FcConfigSubstitute (NULL, pattern, FcMatchPattern);
  FcDefaultSubstitute (pattern);

  match = FcFontMatch (NULL, pattern, &res);

  res = FcPatternGetInteger (match, FC_WEIGHT, 0, &weight);

  if (res != FcResultMatch)
    weight = FC_WEIGHT_NORMAL;

  res = FcPatternGetInteger (match, FC_SLANT, 0, &slant);

  if (res != FcResultMatch)
   slant = FC_SLANT_ROMAN;

  res = FcPatternGetInteger (match, FC_WIDTH, 0, &width);

  if (res != FcResultMatch)
    width = FC_WIDTH_NORMAL;

  if (matched_family != NULL)
    {
      gchar *tmp;

      res = FcPatternGetString (match, FC_FAMILY, 0, (FcChar8 **) &tmp);

      if (res == FcResultMatch)
	*matched_family = g_strdup (tmp);
      else
	{
	  if (monospaced)
	    *matched_family = g_strdup (DEFAULT_MONOSPACE_FAMILY);
	  else
	    *matched_family = g_strdup (DEFAULT_FAMILY);
	}
    }

  if (matched_style != NULL)
    *matched_style = fc_synthesize_style (width, weight, slant);

  FcPatternDestroy (pattern);
  FcPatternDestroy (match);
}

void
fc_list_faces (gboolean        monospaced,
	       gboolean        scalable,
	       FcListFacesFunc callback,
	       gpointer        user_data)
{
  FcFontSet *font_list;
  FcObjectSet *object_set;
  FcPattern *pat;
  gboolean done = FALSE;
  gint i;

  g_assert (callback != NULL);

  pat = FcPatternCreate ();

  if (monospaced)
    FcPatternAddInteger (pat, FC_SPACING, FC_MONO);

  if (scalable)
    FcPatternAddBool (pat, FC_SCALABLE, FcTrue);

  object_set = FcObjectSetBuild (FC_FAMILY, FC_WEIGHT, FC_SLANT, FC_WIDTH, NULL);

  font_list = FcFontList (NULL, pat, object_set);

  for (i = 0; i < font_list->nfont; i++)
    {
      gint weight, width, slant;
      gchar *family, *style;
      FcResult res;

      if (done)
	break;

      res = FcPatternGetString (font_list->fonts[i], FC_FAMILY, 0, (FcChar8 **) &family);
      g_assert (res == FcResultMatch);

      res = FcPatternGetInteger (font_list->fonts[i], FC_WEIGHT, 0, &weight);

      if (res != FcResultMatch)
	weight = FC_WEIGHT_NORMAL;

      res = FcPatternGetInteger (font_list->fonts[i], FC_SLANT, 0, &slant);

      if (res != FcResultMatch)
	slant = FC_SLANT_ROMAN;

      res = FcPatternGetInteger (font_list->fonts[i], FC_WIDTH, 0, &width);

      if (res != FcResultMatch)
	width = FC_WIDTH_NORMAL;

      style = fc_synthesize_style (width, weight, slant);

      done = (*callback) (family, style, width, weight, slant, user_data);

      g_free (style);
    }

  FcFontSetDestroy (font_list);
  FcObjectSetDestroy (object_set);
  FcPatternDestroy (pat);
}

void
fc_get_font_file (const gchar  *family,
		  const gchar  *style,
		  gboolean      monospaced,
		  gboolean      scalable,
		  gchar       **file,
		  gint         *face_index)
{
  FcPattern *pat, *match;
  FcResult res;

  /* Select the best suitable font using a pattern. */
  pat = FcPatternCreate ();

  if (family != NULL)
    FcPatternAddString (pat, FC_FAMILY, (const FcChar8 *)family);
  else
    {
      if (monospaced)
	FcPatternAddString (pat, FC_FAMILY, (const FcChar8 *)DEFAULT_MONOSPACE_FAMILY);
      else
	FcPatternAddString (pat, FC_FAMILY, (const FcChar8 *)DEFAULT_FAMILY);
    }

  if (style != NULL)
    FcPatternAddString (pat, FC_STYLE, (const FcChar8 *)style);
  else
    {
      if (monospaced)
	FcPatternAddString (pat, FC_STYLE, (const FcChar8 *)DEFAULT_MONOSPACE_STYLE);
      else
	FcPatternAddString (pat, FC_STYLE, (const FcChar8 *)DEFAULT_STYLE);
    }

  if (monospaced)
    FcPatternAddInteger (pat, FC_SPACING, FC_MONO);

  if (scalable)
    FcPatternAddBool (pat, FC_SCALABLE, FcTrue);

  /* Set default pattern for the match. */
  FcConfigSubstitute (NULL, pat, FcMatchPattern);
  FcDefaultSubstitute (pat);

  /* Find the best match for the pattern. */
  match = FcFontMatch (NULL, pat, &res);

  if (face_index != NULL)
    {
      res = FcPatternGetInteger (match, FC_INDEX, 0, face_index);
      if (res != FcResultMatch)
	*face_index = 0;
    }

  if (file != NULL)
    {
      gchar *tmp;

      res = FcPatternGetString (match, FC_FILE, 0, (FcChar8 **) &tmp);
      if (res != FcResultMatch)
	*file = NULL;
      else
	*file = g_strdup (tmp);
    }

  FcPatternDestroy (match);
  FcPatternDestroy (pat);
}

