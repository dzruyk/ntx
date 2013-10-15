#ifndef __INTERNAL_H__
#define __INTERNAL_H__

/*
 * Network Terminal protocol -- client functions.
 */

/* Initializes client, must be called first. */
void     client_init           ();

/* Deinitializes client and releases its resources. */
void     client_deinit         ();

/* Processes the buffer `buf' of length `len' according to protocol. */
void     client_do_input       (guchar *buf, gsize len);

/* Returns TRUE if client is now in TELNET mode, FALSE otherwise. */
gboolean client_in_telnet_mode ();


/*
 * Network Terminal protocol -- keyboard functions.
 *
 * These are used only when TELNET mode is off.
 */

/*
 * This function decodes and sends a key event `event' to channel
 */
void key_send      (const GdkEventKey *event);

/*
 * This function sends the key code `keycode' to channel.
 *
 * See gdk/gdkkeysyms.h for available values.
 */
void key_send_code (gint keycode);

#define key_send_page_down()      key_send_code (GDK_Page_Down)
#define key_send_page_up()        key_send_code (GDK_Page_Up)
#define key_send_up()             key_send_code (GDK_Up)
#define key_send_down()           key_send_code (GDK_Down)

/*
 * GUI related functions and controls.
 */

/* Initializes GUI and its widgets. */
void gui_init             (gint *argc, char ***argv);

/* Enables mouse events on console screen. */
void gui_mouse_enable     ();

/* Disables mouse events on console screen. */
void gui_mouse_disable    ();

/* Enables key press events on console screen. */
void gui_keyboard_enable  ();

/* Disables key press events on console screen. */
void gui_keyboard_disable ();


#endif /* __INTERNAL_H_ */
