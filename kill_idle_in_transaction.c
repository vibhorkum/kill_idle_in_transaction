/*-------------------------------------------------------------------------
 *
 * kill_idle_in_transaction.c
 *		Kill idle connections of a Postgres server inactive for a given
 *		amount of time.
 *
 * Copyright (c) 1996-2016, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		kill_idle_in_transaction/kill_idle_in_transaction.c
 *
 *-------------------------------------------------------------------------
 */

/* Some general headers for custom bgworker facility */
#include "postgres.h"
#include "fmgr.h"
#include "access/xact.h"
#include "lib/stringinfo.h"
#include "pgstat.h"
#include "executor/spi.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "utils/guc.h"
#include "utils/snapmgr.h"

/* Allow load of this module in shared libs */
PG_MODULE_MAGIC;

/* Entry point of library loading */
void _PG_init(void);

/* Signal handling */
static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

/* GUC variables */
static int kill_max_idle_time = 5;

/* Worker name */
static char *worker_name = "kill_idle_in_transaction";

static void
kill_idle_in_transaction_sigterm(SIGNAL_ARGS)
{
	int save_errno = errno;
	got_sigterm = true;
	if (MyProc)
		SetLatch(&MyProc->procLatch);
	errno = save_errno;
}

static void
kill_idle_in_transaction_sighup(SIGNAL_ARGS)
{
	int save_errno = errno;
	got_sighup = true;
	if (MyProc)
		SetLatch(&MyProc->procLatch);
	errno = save_errno;
}

static void
kill_idle_in_transaction_build_query(StringInfoData *buf)
{
	appendStringInfo(buf, "SELECT pid, pg_terminate_backend(pid) "
			   "AS status, usename, datname, client_addr "
			   "FROM pg_stat_activity "
			   "WHERE now() - state_change > interval '%d s' AND "
                           "(state = 'idle in transaction' OR "
                           "state = 'idle in transaction (aborted)') AND "
			   "pid != pg_backend_pid();",
					 kill_max_idle_time);
}

static void
kill_idle_in_transaction_main(Datum main_arg)
{
	StringInfoData buf;

	/* Register functions for SIGTERM/SIGHUP management */
	pqsignal(SIGHUP, kill_idle_in_transaction_sighup);
	pqsignal(SIGTERM, kill_idle_in_transaction_sigterm);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/* Connect to a database */
	BackgroundWorkerInitializeConnection("postgres", NULL);

	/* Build query for process */
	initStringInfo(&buf);
	kill_idle_in_transaction_build_query(&buf);

	while (!got_sigterm)
	{
		int rc, ret, i;

		/* Wait necessary amount of time */
		rc = WaitLatch(&MyProc->procLatch,
					   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
					   kill_max_idle_time * 1000L);
		ResetLatch(&MyProc->procLatch);

		/* Emergency bailout if postmaster has died */
		if (rc & WL_POSTMASTER_DEATH)
			proc_exit(1);

		/* Process signals */
		if (got_sighup)
		{
			int old_interval;
			/* Save old value of kill interval */
			old_interval = kill_max_idle_time;

			/* Process config file */
			ProcessConfigFile(PGC_SIGHUP);
			got_sighup = false;
			ereport(LOG, (errmsg("bgworker kill_idle_in_transaction signal: processed SIGHUP")));

			/* Rebuild query if necessary */
			if (old_interval != kill_max_idle_time)
			{
				resetStringInfo(&buf);
				initStringInfo(&buf);
				kill_idle_in_transaction_build_query(&buf);
			}
		}

		if (got_sigterm)
		{
			/* Simply exit */
			ereport(LOG, (errmsg("bgworker kill_idle_in_transaction signal: processed SIGTERM")));
			proc_exit(0);
		}

		/* Process idle connection kill */
		SetCurrentStatementStartTimestamp();
		StartTransactionCommand();
		SPI_connect();
		PushActiveSnapshot(GetTransactionSnapshot());
		pgstat_report_activity(STATE_RUNNING, buf.data);

		/* Statement start time */
		SetCurrentStatementStartTimestamp();

		/* Execute query */
		ret = SPI_execute(buf.data, false, 0);

		/* Some error handling */
		if (ret != SPI_OK_SELECT)
			elog(FATAL, "Error when trying to kill idle connections");

		/* Do some processing and log stuff disconnected */
		for (i = 0; i < SPI_processed; i++)
		{
			int32 pidValue;
			bool isnull;
			char *datname = NULL;
			char *usename = NULL;
			char *client_addr = NULL;

			/* Fetch values */
			pidValue = DatumGetInt32(SPI_getbinval(SPI_tuptable->vals[i],
												   SPI_tuptable->tupdesc,
												   1, &isnull));
			usename = DatumGetCString(SPI_getbinval(SPI_tuptable->vals[i],
													SPI_tuptable->tupdesc,
													3, &isnull));
			datname = DatumGetCString(SPI_getbinval(SPI_tuptable->vals[i],
													SPI_tuptable->tupdesc,
													4, &isnull));
			client_addr = DatumGetCString(SPI_getbinval(SPI_tuptable->vals[i],
														SPI_tuptable->tupdesc,
														5, &isnull));

			/* Log what has been disconnected */
			elog(LOG, "Disconnected idle in transaction connection: PID %d database: %s username: %s client_addr: %s",
				 pidValue, datname ? datname : "none",
				 usename ? usename : "none",
				 client_addr ? client_addr : "none");
		}

		SPI_finish();
		PopActiveSnapshot();
		CommitTransactionCommand();
		pgstat_report_activity(STATE_IDLE, NULL);
	}

	/* No problems, so clean exit */
	proc_exit(0);
}

static void
kill_idle_in_transaction_load_params(void)
{
	/*
	 * Kill backends with idle time more than this interval, possible
	 * candidates for execution are scanned at the same time interbal.
	 */
	DefineCustomIntVariable("kill_idle_in_transaction.max_idle_time",
                            "Maximum time allowed for backends to be idle (s).",
                            "Default of 5s, max of 3600s",
                            &kill_max_idle_time,
                            5,
                            1,
                            3600,
                            PGC_SIGHUP,
                            0,
                            NULL,
                            NULL,
                            NULL);
}

/*
 * Entry point for worker loading
 */
void
_PG_init(void)
{
	BackgroundWorker worker;

	/* Add parameters */
	kill_idle_in_transaction_load_params();

	/* Worker parameter and registration */
	MemSet(&worker, 0, sizeof(BackgroundWorker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
		BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_ConsistentState;
	worker.bgw_main = kill_idle_in_transaction_main;
	snprintf(worker.bgw_name, BGW_MAXLEN, "%s", worker_name);
	/* Wait 10 seconds for restart before crash */
	worker.bgw_restart_time = 10;
	worker.bgw_main_arg = (Datum) 0;
#if PG_VERSION_NUM >= 90400
	/*
	 * Notify PID is present since 9.4. If this is not initialized
	 * a static background worker cannot start properly.
	 */
	worker.bgw_notify_pid = 0;
#endif
	RegisterBackgroundWorker(&worker);
}
