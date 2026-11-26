#!/bin/sh
set -e
if grep -rnE "XSYS_ALLOC\b|xproc_quota\b|boot_accept_shell_lanes\b" source/arch/amd64/kernel/ source/arch/amd64/init/ >/dev/null 2>&1; then
    echo "POLICY VIOLATION: retired symbol present in kernel/init"
    exit 1
fi
echo "kernel_policy_check: OK"
