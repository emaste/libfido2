/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <fido.h>

#include "../openbsd-compat/openbsd-compat.h"
#include "extern.h"

static fido_cred_t *
prepare_cred(FILE *in_f, int type, bool rk, bool uv, bool debug)
{
	fido_cred_t *cred = NULL;
	struct blob cdh;
	struct blob authdata;
	struct blob id;
	struct blob sig;
	struct blob x5c;
	char *rpid = NULL;
	char *fmt = NULL;
	int r;

	memset(&cdh, 0, sizeof(cdh));
	memset(&authdata, 0, sizeof(authdata));
	memset(&id, 0, sizeof(id));
	memset(&sig, 0, sizeof(sig));
	memset(&x5c, 0, sizeof(x5c));

	r = base64_read(in_f, &cdh);
	r |= string_read(in_f, &rpid);
	r |= string_read(in_f, &fmt);
	r |= base64_read(in_f, &authdata);
	r |= base64_read(in_f, &id);
	r |= base64_read(in_f, &sig);
	r |= base64_read(in_f, &x5c);
	if (r < 0)
		errx(1, "input error");

	if (debug) {
		fprintf(stderr, "client data hash:\n");
		xxd(cdh.ptr, cdh.len);
		fprintf(stderr, "relying party id: %s\n", rpid);
		fprintf(stderr, "format: %s\n", fmt);
		fprintf(stderr, "authenticator data:\n");
		xxd(authdata.ptr, authdata.len);
		fprintf(stderr, "credential id:\n");
		xxd(id.ptr, id.len);
		fprintf(stderr, "signature:\n");
		xxd(sig.ptr, sig.len);
		fprintf(stderr, "x509:\n");
		xxd(x5c.ptr, x5c.len);
	}

	if ((cred = fido_cred_new()) == NULL)
		errx(1, "fido_cred_new");

	if ((r = fido_cred_set_type(cred, type)) != FIDO_OK ||
	    (r = fido_cred_set_clientdata_hash(cred, cdh.ptr,
	    cdh.len)) != FIDO_OK ||
	    (r = fido_cred_set_rp(cred, rpid, NULL)) != FIDO_OK ||
	    (r = fido_cred_set_authdata(cred, authdata.ptr,
	    authdata.len)) != FIDO_OK ||
	    (r = fido_cred_set_x509(cred, x5c.ptr, x5c.len)) != FIDO_OK ||
	    (r = fido_cred_set_sig(cred, sig.ptr, sig.len)) != FIDO_OK ||
	    (r = fido_cred_set_fmt(cred, fmt)) != FIDO_OK)
		errx(1, "fido_cred_set: %s", fido_strerr(r));

	if (rk && (r = fido_cred_set_rk(cred, FIDO_OPT_TRUE)) != FIDO_OK)
		errx(1, "fido_cred_set_rk: %s", fido_strerr(r));
	if (uv && (r = fido_cred_set_uv(cred, FIDO_OPT_TRUE)) != FIDO_OK)
		errx(1, "fido_cred_set_uv: %s", fido_strerr(r));

	free(cdh.ptr);
	free(authdata.ptr);
	free(id.ptr);
	free(sig.ptr);
	free(x5c.ptr);
	free(rpid);
	free(fmt);

	return (cred);
}

static void
print_cred(FILE *out_f, int type, const fido_cred_t *cred)
{
	char *id;
	int r;

	r = base64_encode(fido_cred_id_ptr(cred), fido_cred_id_len(cred), &id);
	if (r < 0)
		errx(1, "output error");

	fprintf(out_f, "%s\n", id);

	if (type == COSE_ES256) {
		write_ec_pubkey(out_f, fido_cred_pubkey_ptr(cred),
		    fido_cred_pubkey_len(cred));
	} else if (type == COSE_RS256) {
		write_rsa_pubkey(out_f, fido_cred_pubkey_ptr(cred),
		    fido_cred_pubkey_len(cred));
	} else if (type == COSE_EDDSA) {
		write_eddsa_pubkey(out_f, fido_cred_pubkey_ptr(cred),
		    fido_cred_pubkey_len(cred));
	} else {
		errx(1, "print_cred: unknown type");
	}

	free(id);
}

int
cred_verify(int argc, char **argv)
{
	fido_cred_t *cred = NULL;
	char *in_path = NULL;
	char *out_path = NULL;
	FILE *in_f = NULL;
	FILE *out_f = NULL;
	bool rk = false;
	bool uv = false;
	bool debug = false;
	int type = COSE_ES256;
	int ch;
	int r;

	while ((ch = getopt(argc, argv, "di:o:v")) != -1) {
		switch (ch) {
		case 'd':
			debug = true;
			break;
		case 'i':
			in_path = optarg;
			break;
		case 'o':
			out_path = optarg;
			break;
		case 'v':
			uv = true;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	in_f = open_read(in_path);
	out_f = open_write(out_path);

	if (argc > 0) {
		if (strcmp(argv[0], "es256") == 0)
			type = COSE_ES256;
		else if (strcmp(argv[0], "rs256") == 0)
			type = COSE_RS256;
		else if (strcmp(argv[0], "eddsa") == 0)
			type = COSE_EDDSA;
		else
			errx(1, "unknown type %s", argv[0]);
	}

	fido_init(debug ? FIDO_DEBUG : 0);
	cred = prepare_cred(in_f, type, rk, uv, debug);
	if ((r = fido_cred_verify(cred)) != FIDO_OK)
		errx(1, "fido_cred_verify: %s", fido_strerr(r));

	print_cred(out_f, type, cred);
	fido_cred_free(&cred);

	fclose(in_f);
	fclose(out_f);
	in_f = NULL;
	out_f = NULL;

	exit(0);
}
