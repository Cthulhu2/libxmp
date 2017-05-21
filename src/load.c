/* Extended Module Player
 * Copyright (C) 1996-2016 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifdef __native_client__
#include <sys/syslimits.h>
#else
#include <limits.h>
#endif

#include "format.h"
#include "buffer.h"
#include "list.h"
#include "tempfile.h"

#ifndef LIBXMP_CORE_PLAYER
#if !defined(HAVE_POPEN) && defined(WIN32)
#include "win32/ptpopen.h"
#define HAVE_POPEN 1
#endif
#if defined(__WATCOMC__)
#define popen  _popen
#define pclose _pclose
#define HAVE_POPEN 1
#endif
#include "md5.h"
#include "extras.h"
#endif


extern struct format_loader *format_loader[];

void libxmp_load_prologue(struct context_data *);
void libxmp_load_epilogue(struct context_data *);
int  libxmp_prepare_scan(struct context_data *);

#ifndef LIBXMP_CORE_PLAYER

#define BUFLEN 16384

static void set_md5sum(LIBXMP_BUFFER buf, unsigned char *digest)
{
/*
	MD5_CTX ctx;
	long n, size;

	size = libxmp_buffer_size(buf);

	if (size <= 0) {
		memset(digest, 0, 16);
		return;
	}

	MD5Init(&ctx);

	for (n = 0; n < size; n += BUFLEN) {
		MD5Update(&ctx, buf->start + n, size - n > BUFLEN ? BUFLEN : size - n);
	}
	MD5Final(digest, &ctx);
*/
}

/*
static char *get_dirname(char *name)
{
	char *div, *dirname;
	int len;

	if ((div = strrchr(name, '/'))) {
		len = div - name + 1;
		dirname = malloc(len + 1);
		if (dirname != NULL) {
			memcpy(dirname, name, len);
			dirname[len] = 0;
		}
	} else {
		dirname = strdup("");
	}

	return dirname;
}

static char *get_basename(char *name)
{
	char *div, *basename;

	if ((div = strrchr(name, '/'))) {
		basename = strdup(div + 1);
	} else {
		basename = strdup(name);
	}

	return basename;
}
*/
#endif /* LIBXMP_CORE_PLAYER */

int xmp_test_module(void *src, long size, struct xmp_test_info *info)
{
	LIBXMP_BUFFER buf;
	struct libxmp_mem *mem;
	char name[XMP_NAME_SIZE];
	int i;
	int ret = -XMP_ERROR_SYSTEM;
	char *err;

	if ((buf = libxmp_buffer_new(src, size)) == NULL) {
		goto err;
	}
	if ((err = libxmp_buffer_catch(buf)) != NULL) {
		D_(D_CRIT "%s", err);
		goto err2;
	}
	if ((mem = libxmp_mem_new()) == NULL) {
		goto err2;
	}
	if (setjmp(mem->jmp) != 0) {
		goto err3;
	}

	if (info != NULL) {
		*info->name = 0;	/* reset name before testing */
		*info->type = 0;	/* reset type before testing */
	}

	for (i = 0; format_loader[i] != NULL; i++) {

		libxmp_buffer_seek(buf, 0, SEEK_SET);

		if (format_loader[i]->test(mem, buf, name, 0) == 0) {
			int is_prowizard = 0;

/*
#ifndef LIBXMP_CORE_PLAYER
			if (strcmp(format_loader[i]->name, "prowizard") == 0) {
				hio_seek(h, 0, SEEK_SET);
				pw_test_format(h, buf, 0, info);
				is_prowizard = 1;
			}
#endif

			fclose(h->handle.file);

#ifndef LIBXMP_CORE_PLAYER
			unlink_temp_file(temp);
#endif

*/

			if (info != NULL && !is_prowizard) {
				strncpy(info->name, name, XMP_NAME_SIZE - 1);
				strncpy(info->type, format_loader[i]->name, XMP_NAME_SIZE - 1);
			}

			ret = 0;
			break;
		}
	}

    err3:
	libxmp_mem_release(mem);
    err2:
	libxmp_buffer_release(buf);
    err:
	return ret;
}

static int load_module(xmp_context opaque, LIBXMP_BUFFER buf)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	struct xmp_module *mod = &m->mod;
	struct libxmp_mem *mem = &m->mem;
	int i, j, ret;
	int test_result, load_result;

	libxmp_load_prologue(ctx);

	D_(D_WARN "load");
	test_result = load_result = -1;
	for (i = 0; format_loader[i] != NULL; i++) {
		libxmp_buffer_seek(buf, 0, SEEK_SET);

		if (libxmp_buffer_catch(buf) != NULL) {
			/* Go to next format if access fault testing file */
			continue;
		}

		D_(D_WARN "test %s", format_loader[i]->name);
		test_result = format_loader[i]->test(mem, buf, NULL, 0);
		if (test_result == 0) {
			char *err;

			libxmp_buffer_seek(buf, 0, SEEK_SET);
			if ((err = libxmp_buffer_catch(buf)) != NULL) {
				D_(D_CRIT "exception loading module: %s", err);
				return -XMP_ERROR_LOAD;
			}
			D_(D_WARN "load format: %s", format_loader[i]->name);
			load_result = format_loader[i]->loader(mem, buf, m, 0);
			break;
		}
	}

#ifndef LIBXMP_CORE_PLAYER
	if (test_result == 0 && load_result == 0)
		set_md5sum(buf, m->md5);
#endif

	if (test_result < 0) {
		return -XMP_ERROR_FORMAT;
	}

	if (load_result < 0) {
		goto err_load;
	}

	/* Sanity check: number of channels, module length */
	if (mod->chn > XMP_MAX_CHANNELS || mod->len > XMP_MAX_MOD_LENGTH) {
		goto err_load;
	}

	/* Sanity check: channel pan */
	for (i = 0; i < mod->chn; i++) {
		if (mod->xxc[i].vol < 0 || mod->xxc[i].vol > 0xff) {
			goto err_load;
		}
		if (mod->xxc[i].pan < 0 || mod->xxc[i].pan > 0xff) {
			goto err_load;
		}
	}

	/* Sanity check: patterns */
	if (mod->xxp == NULL) {
		goto err_load;
	}
	for (i = 0; i < mod->pat; i++) {
		if (mod->xxp[i] == NULL) {
			goto err_load;
		}
		for (j = 0; j < mod->chn; j++) {
			int t = mod->xxp[i]->index[j];
			if (t < 0 || t >= mod->trk || mod->xxt[t] == NULL) {
				goto err_load;
			}
		}
	}

	libxmp_adjust_string(mod->name);
	for (i = 0; i < mod->ins; i++) {
		libxmp_adjust_string(mod->xxi[i].name);
	}
	for (i = 0; i < mod->smp; i++) {
		libxmp_adjust_string(mod->xxs[i].name);
	}

	libxmp_load_epilogue(ctx);

	ret = libxmp_prepare_scan(ctx);
	if (ret < 0) {
		xmp_release_module(opaque);
		return ret;
	}

	libxmp_scan_sequences(ctx);

	ctx->state = XMP_STATE_LOADED;

	return 0;

    err_load:
	return -XMP_ERROR_LOAD;
}

int xmp_load_module(xmp_context opaque, void *src, long size)
{
	struct context_data *ctx = (struct context_data *)opaque;
#ifndef LIBXMP_CORE_PLAYER
	struct module_data *m = &ctx->m;
#endif
	LIBXMP_BUFFER buf;
	int ret;

	if ((buf = libxmp_buffer_new(src, size)) == NULL) {
		return -XMP_ERROR_SYSTEM;
	}
		
	if (ctx->state > XMP_STATE_UNLOADED) {
		xmp_release_module(opaque);
	}

#ifndef LIBXMP_CORE_PLAYER
	m->size = size;
#endif

	ret = load_module(opaque, buf);

	libxmp_buffer_release(buf);

	return ret;
}

#if 0
int xmp_load_module_from_memory(xmp_context opaque, void *mem, long size)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	HIO_HANDLE *h;
	int ret;

	/* Use size < 0 for unknown/undetermined size */
	if (size == 0)
		size--;

	if ((h = hio_open_mem(mem, size)) == NULL)
		return -XMP_ERROR_SYSTEM;

	if (ctx->state > XMP_STATE_UNLOADED)
		xmp_release_module(opaque);

	m->filename = NULL;
	m->basename = NULL;
	m->dirname = NULL;
	m->size = size;

	ret = load_module(opaque, h);

	hio_close(h);

	return ret;
}

int xmp_load_module_from_file(xmp_context opaque, void *file, long size)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	HIO_HANDLE *h;
	FILE *f = fdopen(fileno((FILE *)file), "rb");
	int ret;

	if ((h = hio_open_file(f)) == NULL)
		return -XMP_ERROR_SYSTEM;

	if (ctx->state > XMP_STATE_UNLOADED)
		xmp_release_module(opaque);

	m->filename = NULL;
	m->basename = NULL;
	m->dirname = NULL;
	m->size = hio_size(h);

	ret = load_module(opaque, h);

	hio_close(h);

	return ret;
}
#endif

void xmp_release_module(xmp_context opaque)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	struct libxmp_mem *mem = &m->mem;

	/* can't test this here, we must call release_module to clean up
	 * load errors
	if (ctx->state < XMP_STATE_LOADED)
		return;
	 */

	if (ctx->state > XMP_STATE_LOADED) {
		xmp_end_player(opaque);
	}

	ctx->state = XMP_STATE_UNLOADED;

	D_(D_INFO "Freeing memory");

	libxmp_mem_clear(mem);
}

void xmp_scan_module(xmp_context opaque)
{
	struct context_data *ctx = (struct context_data *)opaque;

	if (ctx->state < XMP_STATE_LOADED)
		return;

	libxmp_scan_sequences(ctx);
}
