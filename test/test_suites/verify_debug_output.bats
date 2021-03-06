#!/bin/bash

export KSI_CONF=test/test.cfg

@test "verify output with debug level 1" {
	run src/logksi verify test/resource/logs_and_signatures/log_repaired --ignore-desc-block-time -d
	[ "$status" -eq 0 ]
	[[ "$output" =~ (Verifying... ok.).(Summary of logfile:).( . Count of blocks:             30.*).( . Count of record hashes:      88).( . Count of meta.records:       1).( . Input hash:  SHA-512:dd4e87.*e2b137).( . Output hash: SHA-512:7f5a17.*cd7827) ]]
}

@test "verify output with debug level 2" {
	run src/logksi verify test/resource/logs_and_signatures/signed --ignore-desc-block-time -dd
	[ "$status" -eq 0 ]
	[[ "$output" =~ (Verifying block no.   1... ok.).(Summary of block 1:).( . Sig time:    .1517928882.*).( . Input hash:  SHA-512:7f3dea.*ee3141).( . Output hash: SHA-512:20cfea.*88944a).( . Lines:                       1 . 3 .3.)..(Verifying block no.   2... ok.).(Summary of block 2:).( . Sig time:    .1517928883.*).( . Input hash:  SHA-512:20cfea.*88944a).( . Output hash: SHA-512:9c1ea0.*42e444).( . Lines:                       4 . 6 .3.)..(Verifying block no.   3... ok.).(Summary of block 3:).( . Sig time:    .1517928884.*).( . Input hash:  SHA-512:9c1ea0.*42e444).( . Output hash: SHA-512:1dfeae.*43e987).( . Lines:                       7 . 9 .3.)..(Verifying block no.   4... ok.).(Summary of block 4:).( . Sig time:    .1517928885.*).( . Input hash:  SHA-512:1dfeae.*43e987).( . Output hash: SHA-512:f7f5b4.*b2b596).( . Line:                        n.a).( . Count of meta-records:       1)..(Summary of logfile:).( . Count of blocks:             4).( . Count of record hashes:      9).( . Count of meta-records:       1).( . Input hash:  SHA-512:7f3dea.*ee3141).( . Output hash: SHA-512:f7f5b4.*b2b596) ]]
}

@test "verify output with debug level 3. Multiple blocks. Missing hshes" {
	run src/logksi verify test/resource/logs_and_signatures/log_repaired --ignore-desc-block-time -ddd
	[ "$status" -eq 0 ]
	[[ "$output" =~ (Processing magic number... ok.).(Block no.   1: processing block header... ok.).(Block no.   1: input hash: SHA-512:dd4e87.*2b137.).(Block no.   1: .r.r..r..).(Block no.   1: processing block signature data... ok.).(Block no.   1: lines processed 1 . 3 .3.).(Block no.   1: verifying KSI signature... ok.*ms.).(Block no.   1: signing time: .1517928936.*).(Block no.   1: output hash: SHA-512:18708a.*eeeb7.).(Block no.   1: Warning: all final tree hashes are missing.).*(Block no.  30: all final tree hashes are present.).(Finalizing log signature... ok.) ]]
}

@test "verify output with debug level 3. Single block with Metarecord" {
	run src/logksi verify test/resource/interlink/ok-testlog-interlink-1 -ddd
	[ "$status" -eq 0 ]
	[[ "$output" =~ (Processing magic number... ok.).(Block no.   1: processing block header... ok.).(Block no.   1: input hash: SHA-256:a55829.*a5fc9.).(Block no.   1: Meta-record key  : .com.guardtime.blockCloseReason..).(Block no.   1: Meta-record value: 426c6f636b20636c6f7365642064756520746f2066696c6520636c6f737572652e00.).(Block no.   1: .rrrrrrrrrrrrMr.).(Block no.   1: processing block signature data... ok.).(Block no.   1: lines processed 1 . 12 .12.).(Block no.   1: verifying KSI signature... ok.*ms.).(Block no.   1: signing time: .1539771487.) ]]
}

@test "previous block is more recent than next block with debug level 2" {
	run ./src/logksi verify test/resource/logs_and_signatures/log_repaired -dd
	[ "$status" -eq 6 ]
	[[ "$output" =~ (Verifying block no.  17... ok.).*(Verifying block no.  18... failed.)..(Error: Block no.  17 .*is more recent than).(       block no.  18 .1517928940. 2018.02.06 14.55.40 UTC.00.00)..(Summary of block 18:).( . Sig time:    .1517928940.*).( . Input hash:  SHA-512:0e11fd.*1991c4).( . Output hash: SHA-512:907899.*d2be10).( . Lines:                       52 . 54 .3.)..(Verifying block no.  19... ok.) ]]
}

@test "previous block is more recent than next block with debug level 3" {
	run ./src/logksi verify -ddd test/resource/logs_and_signatures/log_repaired
	[ "$status" -eq 6 ]
	[[ "$output" =~ (Block no.  18: verifying KSI signature... ok.*ms.).(Block no.  18: signing time: .1517928940.).*(Block no.  18: checking signing time with previous block... failed.).(Block no.  18: output hash.*SHA-512:907899.*d2be10.).(Block no.  18: Warning: all final tree hashes are missing.).(Block no.  18: Error: Block no.  17 .*is more recent than block no.  18 .1517928940. 2018.02.06 14.55.40 UTC.00.00). ]]
}

@test "verify output with debug level 3. record hash missing for a logline" {
	run ./src/logksi verify test/resource/logfiles/all_hashes test/resource/logsignatures/record_hash_missing_for_last_record.logsig -ddd
	[ "$status" -eq 6 ]
	[[ "$output" =~ (Block no.   1: processing block header... ok.).(Block no.   1: input hash: SHA-512:7f5a17.*cd7827.).(Block no.   1: [{]r.r... X) ]]
	[[ "$output" =~ "Error: Block no. 1: missing record hash for logline no. 3" ]]
}

@test "verify output with debug level 3. tree hash out of block" {
	run ./src/logksi verify test/resource/logfiles/all_hashes test/resource/logsignatures/tree_hash_out_of_block.logsig -ddd
	[ "$status" -eq 6 ]
	[[ "$output" =~ (Block no.   1: processing block header... ok.).(Block no.   1: input hash: SHA-512:7f5a17.*cd7827.).(Block no.   1: [{]r.r..r.[}]).*(Block no.   1: [{]. X) ]]
	[[ "$output" =~ "Error: Block no. 2: tree hash without preceding block header found." ]]
}

@test "verify output with debug level 3. too much final tree hashes" {
	run ./src/logksi verify test/resource/logfiles/all_hashes test/resource/logsignatures/tree_hashes_final_too_many.logsig -ddd
	[ "$status" -eq 6 ]
	[[ "$output" =~ (Block no.   1: processing block header... ok.).(Block no.   1: input hash: SHA-512:7f5a17.*cd7827.).(Block no.   1: interpreting tree hash no.   5 as a final hash... ok.).(Block no.   1: [{]r.r..r.:. X) ]]
	[[ "$output" =~ "Error: Block no. 1: unexpected final tree hash no. 6." ]]
}
