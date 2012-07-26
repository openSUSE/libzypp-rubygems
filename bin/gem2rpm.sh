#! /bin/bash
# otherwise pipe to python blocks:
exec >/var/log/zypp/gem2rpm.last.log 2>&1

INPUTFILE="$1"
OUTPUTFILE="$2"

PLUGINDIR="$(dirname "$0")"
SPECTEMPLATE="$PLUGINDIR/gem2rpm.spec.template"

MTMPDIR=$(mktemp -d)
trap " [ -d \"$MTMPDIR\" ] && /bin/rm -rf -- \"$MTMPDIR\" " 0 1 2 3 6 9 13 15
chmod 777 "$MTMPDIR"
pushd "$MTMPDIR"

DROPPERM=eval
test $UID = 0 && DROPPERM="su nobody -c "

rm -f "$OUTPUTFILE"

/usr/bin/gem2rpm --template "$SPECTEMPLATE" --local --output gem2rpm.spec "${INPUTFILE}" || exit 1

export INPUTFILE
$DROPPERM '
  /usr/bin/rpmbuild	\
  --define "%topdir		$PWD"	\
  --define "%_builddir		%{topdir}/BUILD"	\
  --define "%_buildrootdir	%{topdir}/BUILDROOT"	\
  --define "%_specdir		%{topdir}"	\
  --define "%_sourcedir		$(dirname "$INPUTFILE")"	\
  --define "%_rpmdir		%{topdir}"	\
  --define "%_srcrpmdir		%{topdir}"	\
  --define "%_rpmfilename	gem2rpm.rpm"	\
  -bb --nodeps gem2rpm.spec'

test -f gem2rpm.rpm && {
  mv gem2rpm.rpm "$OUTPUTFILE" && exit 0
}
exit 1
