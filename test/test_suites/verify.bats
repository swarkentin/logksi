#!/bin/bash

export KSI_CONF=conf.txt

cp -r test/resource/logfiles/signed test/out

@test "verify signed parts" {
	run ./src/logksi verify test/out/signed -d
	[ "$status" -eq 0 ]
	[[ "$output" =~ "Finalizing log signature... ok." ]]
}

cp -r test/resource/logfiles/unsigned test/out

@test "try verifying unsigned parts" {
	run ./src/logksi verify test/out/unsigned -d
	[ "$status" -ne 0 ]
	[[ "$output" =~ "missing KSI signature in block signature." ]]
}

@test "try verifying nonexistent logsignature" {
	run ./src/logksi verify test/out/nothing -d
	[ "$status" -eq 0 ]
	[[ "$output" =~ "Finalizing log signature... ok." ]]
}
