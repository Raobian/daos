/**
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_dist_tx.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"
#include <daos/dtx.h>

#define MUST(rc)		assert_int_equal(rc, 0)
#define	DTX_TEST_SUB_REQS	32
#define DTX_IO_SMALL		32
#define DTX_IO_LARGE		4096

static const char *dts_dtx_dkey	= "dtx_dkey";
static const char *dts_dtx_akey	= "dtx_akey";

static void
dtx_1(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_bufs[DTX_TEST_SUB_REQS][DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 i;

	print_message("DTX with multiple SV updates against the same obj\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	/* Repeatedly insert different SV for the same obj, overwrite. */
	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		dts_buf_render(write_bufs[i], DTX_IO_SMALL);
		insert_single(dkey, akey, 0, write_bufs[i], DTX_IO_SMALL,
			      th, &req);
	}

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);
	/* The last value will be fetched. */
	assert_memory_equal(write_bufs[DTX_TEST_SUB_REQS - 1], fetch_buf,
			    DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_2(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_bufs[DTX_TEST_SUB_REQS][DTX_IO_SMALL];
	char		 fetch_buf[DTX_TEST_SUB_REQS + DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 i;

	print_message("DTX with multiple EV updates against the same obj\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/* Repeatedly insert different SV for the same obj, some overlap. */
	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		dts_buf_render(write_bufs[i], DTX_IO_SMALL);
		insert_single(dkey, akey, i, write_bufs[i], DTX_IO_SMALL,
			      th, &req);
	}

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_TEST_SUB_REQS + DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size,
			 DTX_TEST_SUB_REQS + DTX_IO_SMALL - 1);

	for (i = 0; i < DTX_TEST_SUB_REQS - 1; i++)
		assert_memory_equal(&write_bufs[i][0], &fetch_buf[i], 1);

	assert_memory_equal(write_bufs[DTX_TEST_SUB_REQS - 1],
			    &fetch_buf[DTX_TEST_SUB_REQS], DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_update_multiple_objs(test_arg_t *arg, daos_iod_type_t iod_type,
			 uint16_t oclass)
{
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		*write_bufs[DTX_TEST_SUB_REQS];
	char		*fetch_buf;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid[DTX_TEST_SUB_REQS];
	struct ioreq	 req[DTX_TEST_SUB_REQS];
	int		 i;

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));
	arg->async = 0;

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		oid[i] = dts_oid_gen(oclass, 0, arg->myrank);
		ioreq_init(&req[i], arg->coh, oid[i], iod_type, arg);

		D_ALLOC(write_bufs[i], DTX_IO_LARGE);
		assert_non_null(write_bufs[i]);

		dts_buf_render(write_bufs[i], DTX_IO_LARGE);
		insert_single(dkey, akey, 0, write_bufs[i], DTX_IO_LARGE,
			      th, &req[i]);
	}

	MUST(daos_tx_commit(th, NULL));

	D_ALLOC(fetch_buf, DTX_IO_LARGE);
	assert_non_null(fetch_buf);

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_LARGE,
			      DAOS_TX_NONE, &req[i]);
		assert_int_equal(req[i].iod[0].iod_size, DTX_IO_LARGE);
		assert_memory_equal(write_bufs[i], fetch_buf, DTX_IO_LARGE);
	}

	for (i = 0; i < DTX_TEST_SUB_REQS; i++) {
		D_FREE(write_bufs[i]);
		ioreq_fini(&req[i]);
	}
	D_FREE(fetch_buf);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_3(void **state)
{
	print_message("DTX with multiple SV updates against multiple objs\n");
	dtx_update_multiple_objs(*state, DAOS_IOD_SINGLE, OC_S1);
}

static void
dtx_4(void **state)
{
	print_message("DTX with multiple EV updates against multiple objs\n");
	dtx_update_multiple_objs(*state, DAOS_IOD_ARRAY, OC_S1);
}

static void
dtx_5(void **state)
{
	test_arg_t	*arg = *state;

	print_message("DTX with multiple SV updates against multiple objs\n");

	if (!test_runable(arg, 3))
		skip();

	dtx_update_multiple_objs(arg, DAOS_IOD_SINGLE, OC_EC_2P1G1);
}

static void
dtx_6(void **state)
{
	test_arg_t	*arg = *state;

	print_message("DTX with multiple EV updates against multiple objs\n");

	if (!test_runable(arg, 3))
		skip();

	dtx_update_multiple_objs(arg, DAOS_IOD_ARRAY, OC_EC_2P1G1);
}

static void
dtx_7(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX with SV update plus punch\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	punch_akey(dkey, akey, th, &req);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_8(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_bufs[2][DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL * 2];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX with EV update plus punch\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_bufs[0], DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_bufs[0], DTX_IO_SMALL, th, &req);

	punch_akey(dkey, akey, th, &req);

	dts_buf_render(write_bufs[1], DTX_IO_SMALL);
	insert_single(dkey, akey, DTX_IO_SMALL, write_bufs[1], DTX_IO_SMALL,
		      th, &req);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL * 2,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL * 2);
	memset(write_bufs[0], 0, DTX_IO_SMALL);
	assert_memory_equal(write_bufs[0], fetch_buf, DTX_IO_SMALL);
	assert_memory_equal(write_bufs[1], &fetch_buf[DTX_IO_SMALL], DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_9(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_bufs[2][DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX with conditional insert/update\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_bufs[0], DTX_IO_SMALL);
	req.arg->expect_result = -DER_NONEXIST;
	insert_single_with_flags(dkey, akey, 0, write_bufs[0], DTX_IO_SMALL, th,
				 &req, DAOS_COND_DKEY_UPDATE);

	req.arg->expect_result = 0;
	insert_single_with_flags(dkey, akey, 0, write_bufs[0], DTX_IO_SMALL, th,
				 &req, DAOS_COND_DKEY_INSERT);

	dts_buf_render(write_bufs[1], DTX_IO_SMALL);
	req.arg->expect_result = -DER_EXIST;
	insert_single_with_flags(dkey, akey, 0, write_bufs[1], DTX_IO_SMALL, th,
				 &req, DAOS_COND_AKEY_INSERT);

	req.arg->expect_result = 0;
	insert_single_with_flags(dkey, akey, 0, write_bufs[1], DTX_IO_SMALL, th,
				 &req, DAOS_COND_AKEY_UPDATE);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);
	assert_memory_equal(write_bufs[1], fetch_buf, DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_10(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	const char	*dkey2 = "tmp_dkey";
	const char	*akey2 = "tmp_akey";
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX with conditional punch\n");

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, DAOS_TX_NONE,
		      &req);

	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	req.arg->expect_result = -DER_NONEXIST;
	punch_akey_with_flags(dkey2, akey2, th, &req, DAOS_COND_PUNCH);

	req.arg->expect_result = 0;
	punch_akey_with_flags(dkey, akey, th, &req, DAOS_COND_PUNCH);

	req.arg->expect_result = -DER_NONEXIST;
	punch_dkey_with_flags(dkey2, th, &req, DAOS_COND_PUNCH);

	req.arg->expect_result = 0;
	punch_dkey_with_flags(dkey, th, &req, DAOS_COND_PUNCH);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_11(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	const char	*dkey2 = "tmp_dkey";
	const char	*akey2 = "tmp_akey";
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("Read only transaction\n");

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, DAOS_TX_NONE,
		      &req);

	insert_single(dkey2, akey, 0, write_buf, DTX_IO_SMALL, DAOS_TX_NONE,
		      &req);

	MUST(daos_tx_open(arg->coh, &th, DAOS_TF_RDONLY, NULL));

	req.arg->expect_result = -DER_NO_PERM;

	insert_single(dkey, akey2, 0, write_buf, DTX_IO_SMALL, th, &req);
	punch_akey(dkey, akey, th, &req);

	req.arg->expect_result = 0;

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);

	lookup_single(dkey, akey2, 0, fetch_buf, DTX_IO_SMALL, th, &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	MUST(daos_tx_commit(th, NULL));

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_12(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	const char	*akey2 = "tmp_akey";
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 i;

	print_message("DTX with zero copy flag\n");
	MUST(daos_tx_open(arg->coh, &th, DAOS_TF_ZERO_COPY, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	/* Reuse 'write_buf' */
	for (i = 0; i < DTX_IO_SMALL; i++)
		write_buf[i] += 1;

	insert_single(dkey, akey2, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey2, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);
	/* 'write_buf' has been overwritten, so it is the same as 2nd update. */
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_13(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	print_message("DTX status machnie\n");

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	dts_buf_render(write_buf, DTX_IO_SMALL);

	print_message("Open the TX1...\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	print_message("Commit the empty TX1...\n");
	MUST(daos_tx_commit(th, NULL));

	print_message("Commit the committed TX1...\n");
	MUST(daos_tx_commit(th, NULL));

	print_message("Abort the committed TX1...\n");
	rc = daos_tx_abort(th, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	print_message("Restart the committed TX1...\n");
	rc = daos_tx_restart(th, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	print_message("Update against the committed TX1...\n");
	req.arg->expect_result = -DER_NO_PERM;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	print_message("Fetch against the committed TX1...\n");
	req.arg->expect_result = 0;
	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);
	assert_int_equal(req.iod[0].iod_size, 0);

	print_message("Close the TX1...\n");
	MUST(daos_tx_close(th, NULL));

	print_message("Open the TX2...\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	print_message("Update via the TX2...\n");
	req.arg->expect_result = 0;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	print_message("Restart the TX2...\n");
	MUST(daos_tx_restart(th, NULL));

	print_message("Restart the TX2 again...\n");
	MUST(daos_tx_restart(th, NULL));

	print_message("Abort the TX2...\n");
	MUST(daos_tx_abort(th, NULL));

	print_message("Abort the TX2 again...\n");
	MUST(daos_tx_abort(th, NULL));

	print_message("Commit the aborted TX2...\n");
	rc = daos_tx_commit(th, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	print_message("Update against the aborted TX2...\n");
	req.arg->expect_result = -DER_NO_PERM;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	print_message("Fetch against the aborted TX2...\n");
	req.arg->expect_result = -DER_NO_PERM;
	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);

	print_message("Close the TX2...\n");
	MUST(daos_tx_close(th, NULL));

	ioreq_fini(&req);
}

static void
dtx_14(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	const char	*akey2 = "tmp_akey";
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	print_message("DTX restart because of conflict with others\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	/* Simulate the conflict with other DTX. */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_DTX_RESTART | DAOS_FAIL_ALWAYS,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	rc = daos_tx_commit(th, NULL);
	assert_int_equal(rc, -DER_TX_RESTART);

	/* Not allow new I/O before restart the TX. */
	req.arg->expect_result = -DER_NO_PERM;
	insert_single(dkey, akey2, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_restart(th, NULL));

	/* Reset the fail_loc */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	req.arg->expect_result = 0;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_commit(th, NULL));

	lookup_single(dkey, akey2, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_15(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("DTX restart because of stale pool map\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	daos_fail_loc_set(DAOS_DTX_RESTART | DAOS_FAIL_ALWAYS);
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_DTX_RESTART | DAOS_FAIL_ALWAYS,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	req.arg->expect_result = -DER_TX_RESTART;
	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);

	/* Reset the fail_loc */
	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(0);
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	/* Not allow new I/O before restart the TX. */
	req.arg->expect_result = -DER_NO_PERM;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_restart(th, NULL));

	req.arg->expect_result = 0;
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	MUST(daos_tx_commit(th, NULL));

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_handle_resent(test_arg_t *arg, uint64_t fail_loc)
{
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;

	print_message("Resend commit because of lost CPD request\n");
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     fail_loc | DAOS_FAIL_ALWAYS, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_commit(th, NULL));

	/* Reset the fail_loc */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	ioreq_fini(&req);
	MUST(daos_tx_close(th, NULL));
}

static void
dtx_16(void **state)
{
	print_message("Resend commit because of lost CPD request\n");

	dtx_handle_resent(*state, DAOS_DTX_LOST_RPC_REQUEST);
}

static void
dtx_17(void **state)
{
	print_message("Resend commit because of lost CPD reply\n");

	dtx_handle_resent(*state, DAOS_DTX_LOST_RPC_REPLY);
}

static void
dtx_18(void **state)
{
	test_arg_t	*arg = *state;
	const char	*dkey = dts_dtx_dkey;
	const char	*akey = dts_dtx_akey;
	char		 write_buf[DTX_IO_SMALL];
	char		 fetch_buf[DTX_IO_SMALL];
	daos_epoch_t	 epoch = 0;
	daos_handle_t	 th = { 0 };
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 rc;

	print_message("Spread read time-stamp when commit\n");

	if (!test_runable(arg, 3))
		skip();

	arg->async = 0;
	oid = dts_oid_gen(OC_RP_3G1, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL,
		      DAOS_TX_NONE, &req);

	/* Start read only transaction. */
	MUST(daos_tx_open(arg->coh, &th, DAOS_TF_RDONLY, NULL));

	if (arg->myrank == 0)
		/* DAOS_DTX_NO_READ_TS will skip the initial read TS. */
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			DAOS_DTX_NO_READ_TS | DAOS_FAIL_ALWAYS, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	lookup_single(dkey, akey, 0, fetch_buf, DTX_IO_SMALL, th, &req);
	assert_int_equal(req.iod[0].iod_size, DTX_IO_SMALL);
	assert_memory_equal(write_buf, fetch_buf, DTX_IO_SMALL);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_commit(th, NULL));

	MUST(daos_tx_hdl2epoch(th, &epoch));
	assert_int_not_equal(epoch, 0);

	MUST(daos_tx_close(th, NULL));

	MPI_Barrier(MPI_COMM_WORLD);
	daos_fail_loc_set(DAOS_DTX_SPEC_EPOCH | DAOS_FAIL_ALWAYS);
	daos_fail_value_set(epoch);
	MPI_Barrier(MPI_COMM_WORLD);

	/* Start another RW transaction. */
	MUST(daos_tx_open(arg->coh, &th, 0, NULL));

	dts_buf_render(write_buf, DTX_IO_SMALL);
	insert_single(dkey, akey, 0, write_buf, DTX_IO_SMALL, th, &req);

	/* Expect to hit conflict with the read TS on other shards. */
	rc = daos_tx_commit(th, NULL);
	assert_int_equal(rc, -DER_TX_RESTART);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
	daos_fail_loc_set(0);
	daos_fail_value_set(0);
	MPI_Barrier(MPI_COMM_WORLD);

	MUST(daos_tx_close(th, NULL));

	ioreq_fini(&req);
}

static const struct CMUnitTest dtx_tests[] = {
	{"DTX1: DTX with multiple SV updates against the same obj",
	 dtx_1, NULL, test_case_teardown},
	{"DTX2: DTX with multiple EV updates against the same obj",
	 dtx_2, NULL, test_case_teardown},
	{"DTX3: DTX with multiple SV updates against multiple objs",
	 dtx_3, NULL, test_case_teardown},
	{"DTX4: DTX with multiple EV updates against multiple objs",
	 dtx_4, NULL, test_case_teardown},
	{"DTX5: DTX with multiple SV updates against multiple EC objs",
	 dtx_5, NULL, test_case_teardown},
	{"DTX6: DTX with multiple EV updates against multiple EC objs",
	 dtx_6, NULL, test_case_teardown},
	{"DTX7: DTX with SV update plus punch",
	 dtx_7, NULL, test_case_teardown},
	{"DTX8: DTX with EV update plus punch",
	 dtx_8, NULL, test_case_teardown},
	{"DTX9: DTX with conditional insert/update",
	 dtx_9, NULL, test_case_teardown},
	{"DTX10: DTX with conditional punch",
	 dtx_10, NULL, test_case_teardown},
	{"DTX11: read only transaction",
	 dtx_11, NULL, test_case_teardown},
	{"DTX12: DTX with zero copy flag",
	 dtx_12, NULL, test_case_teardown},
	{"DTX13: DTX status machnie",
	 dtx_13, NULL, test_case_teardown},
	{"DTX14: DTX restart because of conflict with others",
	 dtx_14, NULL, test_case_teardown},
	{"DTX15: DTX restart because of stale pool map",
	 dtx_15, NULL, test_case_teardown},
	{"DTX16: Resend commit because of lost CPD request",
	 dtx_16, NULL, test_case_teardown},
	{"DTX17: Resend commit because of lost CPD reply",
	 dtx_17, NULL, test_case_teardown},
	{"DTX18: Spread read time-stamp when commit",
	 dtx_18, NULL, test_case_teardown},
};

static int
dtx_test_setup(void **state)
{
	int     rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			NULL);

	return rc;
}

int
run_daos_dist_tx_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(dtx_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("Distributed TX tests", dtx_tests,
				ARRAY_SIZE(dtx_tests), sub_tests,
				sub_tests_size, dtx_test_setup, test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
