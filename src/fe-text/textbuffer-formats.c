#include "module.h"
#include <irssi/src/core/expandos.h>
#include <irssi/src/core/levels.h>
#include <irssi/src/core/servers.h>
#include <irssi/src/core/signals.h>
#include <irssi/src/fe-common/core/printtext.h>
#include <irssi/src/fe-common/core/themes.h>
#include <irssi/src/fe-text/gui-printtext.h>
#include <irssi/src/fe-text/gui-windows.h>
#include <irssi/src/fe-text/textbuffer-formats.h>
#include <irssi/src/fe-text/textbuffer-view.h>

TEXT_BUFFER_FORMAT_REC *format_rec;
LINE_INFO_REC lineinfo_tmp;

#if GLIB_CHECK_VERSION(3, 58, 0)

#define i_refstr_init() /* nothing */
#define i_refstr_release(str) ((str) == NULL ? NULL : g_ref_string_release(str))
#define i_refstr_intern(str) ((str) == NULL ? NULL : g_ref_string_new_intern(str))
#define i_refstr_deinit() /* nothing */

#else

GHashTable *i_refstr_table;

static void i_refstr_init(void)
{
	i_refstr_table = g_hash_table_new(g_str_hash, g_str_equal);
}

static char *i_refstr_intern(const char *str)
{
	char *ret;
	gpointer rc_p, ret_p;
	size_t rc;

	if (str == NULL)
		return NULL;

	if (g_hash_table_lookup_extended(i_refstr_table, str, &ret_p, &rc_p)) {
		rc = GPOINTER_TO_SIZE(rc_p);
		ret = ret_p;
	} else {
		rc = 0;
		ret = g_strdup(str);
	}

	if (rc + 1 <= G_MAXSIZE) {
		g_hash_table_insert(i_refstr_table, ret, GSIZE_TO_POINTER(rc + 1));
		return ret;
	} else {
		return g_strdup(str);
	}
}

static void i_refstr_release(char *str)
{
	char *ret;
	gpointer rc_p, ret_p;
	size_t rc;

	if (str == NULL)
		return;

	if (g_hash_table_lookup_extended(i_refstr_table, str, &ret_p, &rc_p)) {
		rc = GPOINTER_TO_SIZE(rc_p);
		ret = ret_p;
	} else {
		rc = 0;
		ret = NULL;
	}

	if (ret == str) {
		if (rc > 1) {
			g_hash_table_insert(i_refstr_table, ret, GSIZE_TO_POINTER(rc - 1));
		} else {
			g_hash_table_remove(i_refstr_table, ret);
			g_free(ret);
		}
	} else {
		g_free(str);
	}
}

static void i_refstr_deinit(void)
{
	g_hash_table_foreach(i_refstr_table, (GHFunc) g_free, NULL);
	g_hash_table_destroy(i_refstr_table);
}

void i_refstr_table_size_dbg(void)
{
	GHashTableIter iter;
	void *k_p, *v_p;
	size_t count, mem;
	count = 0;
	mem = 0;
	g_hash_table_iter_init(&iter, i_refstr_table);
	while (g_hash_table_iter_next(&iter, &k_p, &v_p)) {
		char *key = k_p;
		count++;
		mem += sizeof(char) * (strlen(key) + 1) + 2 * sizeof(void *);
	}

	printtext(NULL, NULL, MSGLEVEL_CLIENTCRAP, "Shared strings: %d, %dkB of data", count,
	          (int) (mem / 1024));
}
#endif

void textbuffer_format_rec_free(TEXT_BUFFER_FORMAT_REC *rec)
{
	int n;

	if (rec == NULL)
		return;
	if (rec == LINE_INFO_FORMAT_SET)
		return;

	i_refstr_release(rec->module);
	i_refstr_release(rec->format);
	i_refstr_release(rec->server_tag);
	i_refstr_release(rec->target);
	i_refstr_release(rec->nick);
	if (rec->nargs >= 1) {
		i_refstr_release(rec->args[0]);
	}
	for (n = 1; n < rec->nargs; n++) {
		g_free(rec->args[n]);
	}
	rec->nargs = 0;
	g_free(rec->args);
	g_slice_free(TEXT_BUFFER_FORMAT_REC, rec);
}

static TEXT_BUFFER_FORMAT_REC *format_rec_new(const char *module, const char *format_tag,
                                              const char *server_tag, const char *target,
                                              const char *nick, int nargs, const char **args)
{
	int n;
	TEXT_BUFFER_FORMAT_REC *ret = g_slice_new0(TEXT_BUFFER_FORMAT_REC);
	ret->module = i_refstr_intern(module);
	ret->format = i_refstr_intern(format_tag);
	ret->server_tag = i_refstr_intern(server_tag);
	ret->target = i_refstr_intern(target);
	ret->nick = i_refstr_intern(nick);
	ret->nargs = nargs;
	ret->args = g_new0(char *, nargs);
	if (nargs >= 1) {
		ret->args[0] = i_refstr_intern(args[0]);
	}
	for (n = 1; n < nargs; n++) {
		ret->args[n] = g_strdup(args[n]);
	}
	return ret;
}

static void store_lineinfo_tmp(TEXT_DEST_REC *dest)
{
	GUI_WINDOW_REC *gui;

	lineinfo_tmp.level = dest == NULL ? 0 : dest->level;
	gui = WINDOW_GUI(dest->window);
	lineinfo_tmp.time =
	    (gui->use_insert_after && gui->insert_after_time) ? gui->insert_after_time : time(NULL);
}

static void sig_print_format(THEME_REC *theme, const char *module, TEXT_DEST_REC *dest,
                             void *formatnump, const char **args)
{
	int formatnum;
	FORMAT_REC *formats;

	store_lineinfo_tmp(dest);

	formatnum = GPOINTER_TO_INT(formatnump);
	formats = g_hash_table_lookup(default_formats, module);

	textbuffer_format_rec_free(format_rec);
	format_rec = format_rec_new(module, formats[formatnum].tag, dest->server_tag, dest->target,
	                            dest->nick, formats[formatnum].params, args);
	format_rec->flags = dest->flags;

	dest->flags |= PRINT_FLAG_FORMAT;
}

static void sig_print_noformat(TEXT_DEST_REC *dest, const char *text)
{
	store_lineinfo_tmp(dest);

	textbuffer_format_rec_free(format_rec);
	format_rec = format_rec_new(NULL, NULL, dest->server_tag, dest->target, dest->nick, 2,
	                            (const char *[]){ NULL, text });
	format_rec->flags = dest->flags;

	dest->flags |= PRINT_FLAG_FORMAT;
}

static void sig_gui_print_text_finished(WINDOW_REC *window)
{
	GUI_WINDOW_REC *gui;
	TEXT_BUFFER_VIEW_REC *view;
	LINE_REC *insert_after;
	LINE_INFO_REC lineinfo = { 0 };

	static const unsigned char eol[] = { 0, LINE_CMD_EOL };

	if (format_rec == NULL)
		return;

	gui = WINDOW_GUI(window);
	view = gui->view;
	insert_after = gui->use_insert_after ? gui->insert_after : view->buffer->cur_line;

	lineinfo.format = format_rec;
	lineinfo.level = lineinfo_tmp.level | MSGLEVEL_FORMAT;
	lineinfo.time = lineinfo_tmp.time;
	format_rec = NULL;

	insert_after = textbuffer_insert(view->buffer, insert_after, eol, 2, &lineinfo);
	textbuffer_view_insert_line(view, insert_after);

	if (gui->use_insert_after)
		gui->insert_after = insert_after;
}

char *textbuffer_line_get_text(TEXT_BUFFER_REC *buffer, LINE_REC *line)
{
	GUI_WINDOW_REC *gui;
	LINE_REC *curr;

	g_return_val_if_fail(buffer != NULL, NULL);
	g_return_val_if_fail(buffer->window != NULL, NULL);

	gui = WINDOW_GUI(buffer->window);
	if (line == NULL || gui == NULL)
		return NULL;

	if (line->info.level & MSGLEVEL_FORMAT) {
		TEXT_DEST_REC dest;
		THEME_REC *theme;
		int formatnum;
		TEXT_BUFFER_FORMAT_REC *format_rec;
		char *text, *tmp, *str;

		curr = line;
		line = NULL;
		format_rec = curr->info.format;

		format_create_dest(
		    &dest,
		    format_rec->server_tag != NULL ? server_find_tag(format_rec->server_tag) : NULL,
		    format_rec->target, curr->info.level & ~MSGLEVEL_FORMAT, buffer->window);

		theme = window_get_theme(dest.window);

		if (format_rec->format != NULL) {
			char *arglist[MAX_FORMAT_PARAMS] = { 0 };
			formatnum = format_find_tag(format_rec->module, format_rec->format);
			memcpy(arglist, format_rec->args, format_rec->nargs * sizeof(char *));
			text = format_get_text_theme_charargs(theme, format_rec->module, &dest,
			                                      formatnum, arglist);
		} else {
			text = g_strdup(format_rec->args[1]);
		}

		if (*text != '\0') {
			current_time = curr->info.time;

			tmp = format_get_level_tag(theme, &dest);
			str = !theme->info_eol ? format_add_linestart(text, tmp) :
			                         format_add_lineend(text, tmp);
			g_free_not_null(tmp);
			g_free_not_null(text);
			text = str;
			tmp = format_get_line_start(theme, &dest, curr->info.time);
			str = !theme->info_eol ? format_add_linestart(text, tmp) :
			                         format_add_lineend(text, tmp);
			g_free_not_null(tmp);
			g_free_not_null(text);
			text = str;
			/* str = g_strconcat(text, "\n", NULL); */
			/* g_free(text); */

			dest.flags |= PRINT_FLAG_FORMAT;

			current_time = (time_t) -1;
			return str;
		} else if (format_rec->format != NULL) {
			g_free(text);
			return NULL;
		} else {
			return text;
		}
	} else {
		return g_strdup(line->info.text);
	}
}

void textbuffer_formats_init(void)
{
	format_rec = NULL;
	i_refstr_init();

	signal_add("print format", (SIGNAL_FUNC) sig_print_format);
	signal_add("print noformat", (SIGNAL_FUNC) sig_print_noformat);
	signal_add("gui print text finished", (SIGNAL_FUNC) sig_gui_print_text_finished);
}

void textbuffer_formats_deinit(void)
{
	signal_remove("print format", (SIGNAL_FUNC) sig_print_format);
	signal_remove("print noformat", (SIGNAL_FUNC) sig_print_noformat);
	signal_remove("gui print text finished", (SIGNAL_FUNC) sig_gui_print_text_finished);

	textbuffer_format_rec_free(format_rec);
	i_refstr_deinit();
}
