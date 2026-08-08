#!/bin/sh
echo "PROBE kernel: $(uname -r)"
echo "PROBE nproc: $(nproc)"
echo "PROBE mem: $(awk '/MemTotal/{print $2" kB"}' /proc/meminfo)"

# Issue #26: unprivileged user namespaces, the Bubblewrap blocker.
echo "PROBE userns_clone sysctl: $(cat /proc/sys/kernel/unprivileged_userns_clone 2>/dev/null || echo ABSENT)"
echo "PROBE max_user_namespaces: $(cat /proc/sys/user/max_user_namespaces 2>/dev/null || echo ABSENT)"
echo "PROBE apparmor_restrict_userns: $(cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns 2>/dev/null || echo ABSENT)"

if bwrap --unshare-all --ro-bind / / /bin/true 2>/tmp/bw1; then
  echo "PROBE bwrap --unshare-all (as root)    = OK"
else
  echo "PROBE bwrap --unshare-all (as root)    = FAIL: $(cat /tmp/bw1)"
fi

if bwrap --unshare-net --ro-bind / / /bin/true 2>/tmp/bw2; then
  echo "PROBE bwrap --unshare-net              = OK"
else
  echo "PROBE bwrap --unshare-net              = FAIL: $(cat /tmp/bw2)"
fi

# The real case: unprivileged, which is how a guest must run.
if setpriv --reuid=1000 --regid=1000 --clear-groups bwrap --unshare-all --ro-bind / / /bin/true 2>/tmp/bw3; then
  echo "PROBE bwrap unprivileged (uid 1000)    = OK"
else
  echo "PROBE bwrap unprivileged (uid 1000)    = FAIL: $(cat /tmp/bw3)"
fi

# Does a --unshare-net guest really have no interfaces?
echo "PROBE ifaces inside --unshare-net: $(bwrap --unshare-net --ro-bind / / ip -o link show 2>/dev/null | wc -l) link(s)"
echo "PROBE ifaces outside:              $(ip -o link show 2>/dev/null | wc -l) link(s)"

/probe
echo "PROBE ALLDONE"

echo "PROBE holding open for log capture"
sleep 240
