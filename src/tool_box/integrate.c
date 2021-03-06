/*
 * Copyright 2013-2017 Guardtime, Inc.
 *
 * This file is part of the Guardtime client SDK.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES, CONDITIONS, OR OTHER LICENSES OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 * "Guardtime" and "KSI" are trademarks or registered trademarks of
 * Guardtime, Inc., and no license to trademarks is granted; Guardtime
 * reserves and retains all trademark rights.
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ksi/ksi.h>
#include <ksi/compatibility.h>
#include "param_set/param_set.h"
#include "param_set/task_def.h"
#include "param_set/parameter.h"
#include "tool_box/ksi_init.h"
#include "tool_box/param_control.h"
#include "tool_box/task_initializer.h"
#include "smart_file.h"
#include "logksi_err.h"
#include "conf_file.h"
#include "api_wrapper.h"
#include "printer.h"
#include "debug_print.h"
#include "tool.h"
#include "rsyslog.h"

static int generate_tasks_set(PARAM_SET *set, TASK_SET *task_set);
static int generate_filenames(ERR_TRCKR *err, IO_FILES *files);
static int open_input_and_output_files(ERR_TRCKR *err, IO_FILES *files, int forceOverwrite);
static int acquire_file_locks(ERR_TRCKR *err, MULTI_PRINTER *mp, IO_FILES *files);
static int rename_temporary_and_backup_files(ERR_TRCKR *err, IO_FILES *files);
static void close_input_and_output_files(ERR_TRCKR *err, int res, IO_FILES *files);
static int check_pipe_errors(PARAM_SET *set, ERR_TRCKR *err);

int integrate_run(int argc, char **argv, char **envp) {
	int res;
	char buf[2048];
	PARAM_SET *set = NULL;
	TASK_SET *task_set = NULL;
	TASK *task = NULL;
	KSI_CTX *ksi = NULL;
	ERR_TRCKR *err = NULL;
	SMART_FILE *logfile = NULL;
	int d = 0;
	int forceOverwrite = 0;
	IO_FILES files;
	MULTI_PRINTER *mp = NULL;

	IO_FILES_init(&files);

	/**
	 * Extract command line parameters.
	 */
	res = PARAM_SET_new(
			CONF_generate_param_set_desc("{input}{o}{insert-missing-hashes}{force-overwrite}{use-computed-hash-on-fail}{use-stored-hash-on-fail}{d}{log}{h|help}", "", buf, sizeof(buf)),
			&set);
	if (res != KT_OK) goto cleanup;

	res = TASK_SET_new(&task_set);
	if (res != PST_OK) goto cleanup;

	res = generate_tasks_set(set, task_set);
	if (res != PST_OK) goto cleanup;

	res = TASK_INITIALIZER_getServiceInfo(set, argc, argv, envp);
	if (res != PST_OK) goto cleanup;

	res = TASK_INITIALIZER_check_analyze_report(set, task_set, 0.5, 0.1, &task);
	if (res != KT_OK) goto cleanup;

	res = TASK_INITIALIZER_getPrinter(set, &mp);
	ERR_CATCH_MSG(err, res, "Error: Unable to create Multi printer!");

	res = TOOL_init_ksi(set, &ksi, &err, &logfile);
	if (res != KT_OK) goto cleanup;

	d = PARAM_SET_isSetByName(set, "d");

	res = check_pipe_errors(set, err);
	if (res != KT_OK) goto cleanup;

	res = PARAM_SET_getStr(set, "input", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &files.user.inLog);
	if (res != KT_OK && res != PST_PARAMETER_EMPTY) goto cleanup;

	res = PARAM_SET_getStr(set, "o", NULL, PST_PRIORITY_HIGHEST, PST_INDEX_LAST, &files.user.inSig);
	if (res != KT_OK && res != PST_PARAMETER_EMPTY) goto cleanup;

	res = generate_filenames(err, &files);
	if (res != KT_OK) goto cleanup;

	forceOverwrite = PARAM_SET_isSetByName(set, "force-overwrite");

	res = open_input_and_output_files(err, &files, forceOverwrite);
	if (res != KT_OK) goto cleanup;

	res = acquire_file_locks(err, mp, &files);
	if (res == KT_VERIFICATION_SKIPPED) {
		res = KT_OK;
		goto cleanup;
	} else if (res != KT_OK) goto cleanup;


	print_progressDesc(mp, MP_ID_BLOCK, 0, DEBUG_EQUAL | DEBUG_LEVEL_1, "Integrating... ");
	res = logsignature_integrate(set, mp, err, ksi, &files);
	print_progressResult(mp, MP_ID_BLOCK, DEBUG_LEVEL_1, res);
	if (res != KT_OK) goto cleanup;

	res = rename_temporary_and_backup_files(err, &files);
	if (res != KT_OK) goto cleanup;

	res = KT_OK;

cleanup:

	close_input_and_output_files(err, res, &files);

	MULTI_PRINTER_printByID(mp, MP_ID_BLOCK);
	if (MULTI_PRINTER_hasDataByID(mp, MP_ID_LOGFILE_WARNINGS)) {
		print_debug("\n");
		MULTI_PRINTER_printByID(mp, MP_ID_LOGFILE_WARNINGS);
	}

	LOGKSI_KSI_ERRTrace_save(ksi);

	if (res != KT_OK) {
		if (ERR_TRCKR_getErrCount(err) == 0) {ERR_TRCKR_ADD(err, res, NULL);}
		LOGKSI_KSI_ERRTrace_LOG(ksi);

		print_errors("\n");
		ERR_TRCKR_print(err, d);
	}

	SMART_FILE_close(logfile);
	PARAM_SET_free(set);
	TASK_SET_free(task_set);
	ERR_TRCKR_free(err);
	KSI_CTX_free(ksi);
	MULTI_PRINTER_free(mp);

	return LOGKSI_errToExitCode(res);
}

char *integrate_help_toString(char *buf, size_t len) {
	KSI_snprintf(buf, len,
		"Usage:\n"
		" %s integrate <logfile> [-o <out.logsig>]\n"
		"\n"
		" <logfile>\n"
		"           - Name of the log file whose temporary files are to be integrated.\n"
		"             The two temporary files created while asynchronously signing are:\n"
		"               * the log signature blocks file: '<logfile>.logsig.parts/blocks.dat'; and\n"
		"               * the log signature file containing the respective KSI signatures: \n"
		"                 '<logfile>.logsig.parts/block-signatures.dat'.\n"
		" -o <out.logsig>\n"
		"           - Name of the integrated output log signature file. If not specified,\n"
		"             the log signature file is saved as '<logfile>.logsig' in the same folder where\n"
		"             the '<logfile>' is located. An attempt to overwrite an existing log signature file will\n"
		"             result in an error. Use '-' to redirect the integrated log signature binary stream to stdout.\n"
		" --force-overwrite\n"
		"           - Force overwriting of existing log signature file.\n"
		" -d\n"
		"           - Print detailed information about processes and errors to stderr.\n"
		"             To make output more verbose use -dd or -ddd.\n"
		" --log <file>\n"
		"           - Write libksi log to the given file. Use '-' as file name to redirect the log to stdout.\n",
		TOOL_getName()
	);

	return buf;
}


const char *integrate_get_desc(void) {
	return "Integrates individual log signature blocks file and KSI signatures file into a single log signature file.";
}

static int generate_tasks_set(PARAM_SET *set, TASK_SET *task_set) {
	int res;

	if (set == NULL || task_set == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/**
	 * Configure parameter set, control, repair and object extractor function.
	 */
	PARAM_SET_addControl(set, "{input}", isFormatOk_inputFile, NULL, convertRepair_path, NULL);
	PARAM_SET_addControl(set, "{log}{o}", isFormatOk_path, NULL, convertRepair_path, NULL);
	PARAM_SET_addControl(set, "{insert-missing-hashes}{force-overwrite}{use-computed-hash-on-fail}{use-stored-hash-on-fail}{d}", isFormatOk_flag, NULL, NULL, NULL);

	PARAM_SET_setParseOptions(set, "input", PST_PRSCMD_COLLECT_LOOSE_VALUES | PST_PRSCMD_HAS_NO_FLAG | PST_PRSCMD_NO_TYPOS);

	PARAM_SET_setParseOptions(set, "insert-missing-hashes,force-overwrite,use-computed-hash-on-fail,use-stored-hash-on-fail", PST_PRSCMD_HAS_NO_VALUE);
	PARAM_SET_setParseOptions(set, "d", PST_PRSCMD_HAS_NO_VALUE | PST_PRSCMD_NO_TYPOS);

	/**
	 * Define possible tasks.
	 */
	/*					  ID	DESC													MAN			ATL		FORBIDDEN		IGN	*/
	TASK_SET_add(task_set, 0,	"Integrate log signature block with KSI signature.",	"input",	NULL,	NULL,			NULL);

	res = KT_OK;

cleanup:

	return res;
}

static int check_pipe_errors(PARAM_SET *set, ERR_TRCKR *err) {
	int res;

	res = get_pipe_out_error(set, err, NULL, "o,log", NULL);
	if (res != KT_OK) goto cleanup;

cleanup:
	return res;
}

static int generate_filenames(ERR_TRCKR *err, IO_FILES *files) {
	int res;
	IO_FILES tmp;

	memset(&tmp.internal, 0, sizeof(tmp.internal));

	if (err == NULL || files == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/* Input consists of two parts - blocks and signatures. Names of these files are generated from the log file name. */
	res = concat_names(files->user.inLog, ".logsig.parts/blocks.dat", &tmp.internal.partsBlk);
	ERR_CATCH_MSG(err, res, "Error: Could not generate input blocks file name.");

	res = concat_names(files->user.inLog, ".logsig.parts/block-signatures.dat", &tmp.internal.partsSig);
	ERR_CATCH_MSG(err, res, "Error: Could not generate input signatures file name.");

	/* Output log signature file name, if not specified, is generated from the log file name. */
	if (files->user.inSig == NULL) {
		res = concat_names(files->user.inLog, ".logsig", &tmp.internal.outSig);
		ERR_CATCH_MSG(err, res, "Error: Could not generate output log signature file name.");
		res = temp_name(tmp.internal.outSig, &tmp.internal.tempSig);
		ERR_CATCH_MSG(err, res, "Error: Could not generate temporary output log signature file name.");
	} else if (!strcmp(files->user.inSig, "-")) {
		/* Output must go to a nameless temporary file before redirecting it to stdout. */
		tmp.internal.bStdout = 1;
	} else {
		/* Output must go to a named temporary file that is renamed appropriately on success. */
		res = temp_name(files->user.inSig, &tmp.internal.tempSig);
		ERR_CATCH_MSG(err, res, "Error: Could not generate temporary output log signature file name.");
		res = duplicate_name(files->user.inSig, &tmp.internal.outSig);
		ERR_CATCH_MSG(err, res, "Error: Could not duplicate output log signature file name.");
	}

	files->internal = tmp.internal;
	memset(&tmp.internal, 0, sizeof(tmp.internal));
	res = KT_OK;

cleanup:

	logksi_internal_filenames_free(&tmp.internal);

	return res;
}

static int open_input_and_output_files(ERR_TRCKR *err, IO_FILES *files, int forceOverwrite) {
	int res;
	int partsBlkErr = 0;
	int partsSigErr = 0;
	IO_FILES tmp;

	memset(&tmp.files, 0, sizeof(tmp.files));

	if (err == NULL || files == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	tmp.files.partsBlk = fopen(files->internal.partsBlk, "rb");
	if (tmp.files.partsBlk == NULL) partsBlkErr = errno;

	tmp.files.partsSig = fopen(files->internal.partsSig, "rb");
	if (tmp.files.partsSig == NULL) partsSigErr = errno;

	if (partsBlkErr == 0 && partsSigErr == 0) {
		/* If both of the input files exist and the output log signature file also exists,
		 * the output log signature file must not be overwritten because it may contain KSI signatures
		 * obtained by sign recovery but not present in the input signatures file. */
		if (!forceOverwrite) {
			tmp.files.outSig = fopen(files->internal.outSig, "rb");
			if (tmp.files.outSig != NULL) {
				res = KT_IO_ERROR;
				ERR_CATCH_MSG(err, res, "Error: Overwriting of existing log signature file %s not allowed. Run 'logksi integrate' with '--force-overwrite' to force overwriting.", files->internal.outSig);
			}
		}
		res = logksi_file_create_temporary(files->internal.tempSig, &tmp.files.outSig, files->internal.bStdout);
		ERR_CATCH_MSG(err, res, "Error: Could not create temporary output log signature file.");
	} else if (partsBlkErr == ENOENT && partsSigErr == ENOENT) {
		/* If none of the input files exist, but the output log signature file exists,
		 * the output log signature file is the result of the synchronous signing process
		 * and must not be overwritten. A read mode file handle is needed for acquiring a file lock. */
		tmp.files.inSig = fopen(files->internal.outSig, "rb");
		if (tmp.files.inSig != NULL) {
			/* Reassign ouput file name as input file name to avoid potential removal as an incomplete output file. */
			files->internal.inSig = files->internal.outSig;
			files->internal.outSig = NULL;
		} else {
			if (errno == ENOENT) {
				res = KT_KSI_SIG_VER_IMPOSSIBLE;
				ERR_CATCH_MSG(err, res, "Error: Unable to find input blocks file %s.", files->internal.partsBlk);
			} else {
				res = KT_IO_ERROR;
				ERR_CATCH_MSG(err, res, "Error: Could not open output log signature file %s in read mode.", files->internal.inSig);
			}
		}
	} else {
		res = KT_KSI_SIG_VER_IMPOSSIBLE;
		if (partsBlkErr != 0) {
			ERR_CATCH_MSG(err, res, "Error: Unable to %s blocks file %s.", partsBlkErr == ENOENT ? "find ": "open", files->internal.partsBlk);
		} else {
			ERR_CATCH_MSG(err, res, "Error: Unable to %s signatures file %s.", partsSigErr == ENOENT ? "find ": "open", files->internal.partsSig);
		}
	}

	files->files = tmp.files;
	memset(&tmp.files, 0, sizeof(tmp.files));

	res = KT_OK;

cleanup:

	logksi_files_close(&tmp.files);
	return res;
}

static int get_file_read_lock(FILE *in, MULTI_PRINTER *mp) {
	struct flock lock;
	int fres;

	if (in == NULL || mp == NULL) return KT_INVALID_ARGUMENT;

	lock.l_type = F_RDLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	fres = fcntl(fileno(in), F_SETLK, &lock);
	if (fres != 0) {
		if (errno == EAGAIN || errno == EACCES) {
			print_progressDesc(mp, MP_ID_BLOCK, 1, DEBUG_LEVEL_0, "Waiting to acquire read lock... ");
			MULTI_PRINTER_printByID(mp, MP_ID_BLOCK);
			fres = fcntl(fileno(in), F_SETLKW, &lock);
			print_progressResult(mp, MP_ID_BLOCK, DEBUG_LEVEL_0, fres);
			MULTI_PRINTER_printByID(mp, MP_ID_BLOCK);
		}
	}

	if (fres != 0) {
		return KT_IO_ERROR;
	} else {
		return KT_OK;
	}
}

static int acquire_file_locks(ERR_TRCKR *err, MULTI_PRINTER *mp, IO_FILES *files) {
	int res = KT_UNKNOWN_ERROR;

	if (err == NULL || files == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	if (files->files.partsBlk && files->files.partsSig) {
		/* Check that the asynchronous signing process has completed writing to blocks and signatures files. */
		res = get_file_read_lock(files->files.partsBlk, mp);
		ERR_CATCH_MSG(err, res, "Error: Could not acquire read lock for input blocks file %s.", files->internal.partsBlk);
		res = get_file_read_lock(files->files.partsSig, mp);
		ERR_CATCH_MSG(err, res, "Error: Could not acquire read lock for input signatures file %s.", files->internal.partsSig);
		res = KT_OK;
	} else if (files->files.partsBlk == NULL && files->files.partsSig == NULL) {
		res = get_file_read_lock(files->files.inSig, mp);
		ERR_CATCH_MSG(err, res, "Error: Could not acquire read lock for output log signature file %s.", files->internal.inSig);
		res = KT_VERIFICATION_SKIPPED;
	}

cleanup:

	return res;


}

static int rename_temporary_and_backup_files(ERR_TRCKR *err, IO_FILES *files) {
	int res;

	if (err == NULL || files == NULL) {
		res = KT_INVALID_ARGUMENT;
		goto cleanup;
	}

	if (files->internal.tempSig) {
		/* Output must be saved in output log signature file, so the temporary file is renamed. */
		logksi_file_close(&files->files.outSig);
		res = logksi_file_rename(files->internal.tempSig, files->internal.outSig);
		ERR_CATCH_MSG(err, res, "Error: Could not rename temporary file %s to output log signature file %s.", files->internal.tempSig, files->internal.outSig);
	} else if (files->internal.bStdout) {
		res = logksi_file_redirect_to_stdout(files->files.outSig);
		ERR_CATCH_MSG(err, res, "Error: Could not write temporary output log signature file to stdout.");
	}

	res = KT_OK;

cleanup:

	return res;
}

void close_input_and_output_files(ERR_TRCKR *err, int res, IO_FILES *files) {
	if (files) {
		logksi_files_close(&files->files);
		if (files->internal.tempSig && res != KT_OK) {
			if (remove(files->internal.tempSig) != 0) {
				if (err) ERR_TRCKR_ADD(err, KT_IO_ERROR, "Error: Could not remove temporary output log signature %s.", files->internal.tempSig);
			}
		}
		logksi_internal_filenames_free(&files->internal);
	}
}
