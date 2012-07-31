
# Native rubygems support for libzypp

SUSE Hackwek 8 project

* Michael Andres <ma@suse.de>
* Duncan Mac-Vicar P. <dmacvicar@suse.de>

This is an EXPERIMENTAL research project.

## Tools

### rubygems2solv

Parses rubygem(s) files or directories containing them and generate solv data.
Requires.

Note: common_write.* and tools_util.h are copied from libsolv as currently the
headers are not installed.

### gem2rpm

Libzypp generator plugin converting a downloaded `.gem` into a `.rpm` package.
The plugin together whith its helper files `gem2rpm.sh` and `gem2rpm.spec.template`
need to be installed in `/usr/lib/zypp/plugins/generator`.
