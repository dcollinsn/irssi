#ifndef IRSSI_PERL_TEXTUI_WRAPPER_BUFFER_LINE_H
#define IRSSI_PERL_TEXTUI_WRAPPER_BUFFER_LINE_H

struct Buffer_Line_Wrapper {
	LINE_REC *line;
	TEXT_BUFFER_REC *buffer;
};

static int magic_free_buffer_line(pTHX_ SV *sv, MAGIC *mg)
{
	struct Buffer_Line_Wrapper *wrap = (struct Buffer_Line_Wrapper *) mg->mg_ptr;
	g_free(wrap);
	mg->mg_ptr = NULL;
	sv_setiv(sv, 0);
	return 0;
}

static MGVTBL vtbl_free_buffer_line =
{
    NULL, NULL, NULL, NULL, magic_free_buffer_line
};

static struct Buffer_Line_Wrapper *perl_wrap_buffer_line(TEXT_BUFFER_REC *buffer, LINE_REC *line)
{
	struct Buffer_Line_Wrapper *wrap;

	if (line == NULL)
		return NULL;

	wrap = g_new0(struct Buffer_Line_Wrapper, 1);
	wrap->buffer = buffer;
	wrap->line = line;

	return wrap;
}

static SV *perl_buffer_line_bless(struct Buffer_Line_Wrapper *object)
{
	SV *ret, *tmp;

	if (object == NULL)
		return &PL_sv_undef;

	ret = irssi_bless_plain("Irssi::TextUI::Line", object);

	tmp = *hv_fetch(hvref(ret), "_irssi", 6, 0);
	sv_magic(tmp, NULL, '~', NULL, 0);

	SvMAGIC(tmp)->mg_private = 0x1551; /* HF */
	SvMAGIC(tmp)->mg_virtual = &vtbl_free_buffer_line;
	SvMAGIC(tmp)->mg_ptr = (char *) object;

	return ret;
}

#endif
