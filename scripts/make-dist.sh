#!/bin/bash
set -euo pipefail


## CONSTANTS
SCRIPT_ROOT=$(realpath $(dirname ${BASH_SOURCE}))

YOCTO_DL_URL=http://downloads.yoctoproject.org/releases/opkg

# Channels which are only printed when verbose
LOG_VERBOSEONLY_CHANNELS=(DEBUG INFO)
log() {
	local log_level=$1
	local log_msg=${@:2}
	if [[ "${LOG_VERBOSEONLY_CHANNELS[@]}" == *"$log_level"* \
		&& "${verbose:-}" != true ]]; then
		return
	else
		echo "${log_level}:" $log_msg >&2
	fi
}


## CLI PARSING
usage() {
	cat <<EOF
$(basename ${BASH_SOURCE}) [--help] [-k KEY_ID] [-m] DIST_COMMIT_HASH

Create a source distribution archive, signature file, release notes, and
checksums. Products are stored in the dist/ directory.

# Options
-h,--help
    Print this usage text and exit.
-k,--signing-key KEY_ID
    Specify a GPG private key ID (like 0xABCEDF), which will be used to sign
    the dist archive. If not specified, the default gpg signing key will be
    used.
-m,--manifest
    Create a plaintext manifest file of all members in the dist archive.
    (Useful for comparing with previous releases.)

# Positional Arguments
DIST_COMMIT_HASH
    The git tree-like hash from which to create the dist source archive.

# Returns
0 on success; non-zero on failure.
EOF
}


dist_commit=
do_manifest=
gpg_signing_key=
opkg_root=$(realpath "${SCRIPT_ROOT}/..")
positionals=()
verbose=true

while [ $# -ge 1 ]; do case "$1" in
	-h|--help)
		usage
		exit 0
		;;
	-k|--signing-key)
		shift
		gpg_signing_key=$1
		shift
		;;
	-m|--manifest)
		do_manifest=true
		shift
		;;
	-*|--*)
		log ERROR "Invalid or unknown option \"$1\"."
		exit 2
		;;
	*)
		positionals+=($1)
		shift
		;;
esac; done

if [ ${#positionals[@]} -lt 1 ]; then
	log ERROR "Missing required positional arguments.";
	usage
	exit 2
fi
dist_commit=${positionals[0]}

log DEBUG "Using $opkg_root as the opkg project root"


## MAIN
cd "$opkg_root"

# Refresh the build/ directory
distdir="$(realpath ${opkg_root}/dist)"
rm -rf "${distdir}"
mkdir -p "${distdir}"

log INFO 'Creating source dist archive...'
{
	stop_container() {
		local cid=$1
		if [ -n "$cid" ]; then
			log INFO "Stopping container $cid ..."
			docker container kill $cid
		fi
	}

	cid=$(docker container run -i --detach \
		-u $(id -u):$(id -g) \
		-v "${distdir}":/mnt/dist:rw \
		opkg-dev \
	)
	trap "stop_container $cid" EXIT

	# Copy the source code from the git commit to the container
	git archive --format=tar $dist_commit | docker container cp - $cid:/usr/local/src/opkg
	# Give the current user ownership of the opkg source code in the container
	docker container exec \
		-u 0:0 \
		$cid \
		/bin/bash -c "chown -R $(id -u):$(id -g) /usr/local/src/opkg"
	# Build the project and source dist in the container and copy into the dist/ directory
	docker container exec -i $cid /bin/bash <<-EOF
		mkdir build && cd build
		cmake .. -DUSE_SOLVER_LIBSOLV=1
		make -j$(nproc)
		make package_source
		cp -v opkg-*.tar.gz /mnt/dist/
	EOF
}


# Everything else is done from within the dist/ directory
cd "${distdir}"
dist_base=$(ls ./opkg-*.tar.gz | sed 's/.*\(opkg-.*\)\.tar\.gz/\1/')
if [ "$do_manifest" = true ]; then
	log INFO "Creating dist archive manifest file..."
	tar -z -f ${dist_base}.tar.gz --list | sort >${dist_base}.manifest
fi


# Sign the dist archive
if [ -n "$gpg_signing_key" ]; then
	log INFO "Signing with GPG key ID $gpg_signing_key ..."
	arg_key="--local-user $gpg_signing_key"
else
	log WARNING 'No GPG signing key specified; letting GPG choose the default.'
	arg_key=
fi
gpg --armor --detach-sig \
	$arg_key \
	--output ${dist_base}.tar.gz.asc \
	--sign ${dist_base}.tar.gz
gpg \
	$arg_key \
	--verify ${dist_base}.tar.gz.asc ${dist_base}.tar.gz

# Create the release notes file based on the changelog
log LOG 'Generating release notes...'
cat - >${dist_base}.release-notes <<EOF
Release Notes for ${dist_base}
====

# TODO: Cut irrelevant parts of the CHANGELOG file.
EOF
tar -xz \
	-f ${dist_base}.tar.gz \
	--to-stdout \
		${dist_base}/CHANGELOG.md \
	>>${dist_base}.release-notes
vi ${dist_base}.release-notes


# Download and update the downloads.yocto.org checksum files
sum_files=( \
	${dist_base}.tar.gz \
	${dist_base}.tar.gz.asc \
	${dist_base}.release-notes \
)

update_sums() {
	# Download the sum_file from the downloads.yoctoprojects.org fileserver and
	# update its values using the sum_binary.
	local sum_binary=$1
	local sum_file=$2

	wget "${YOCTO_DL_URL}/${sum_file}"
	${sum_binary} ${sum_files[@]} >>${sum_file}
	${sum_binary} --check --ignore-missing ${sum_file}
}

update_sums md5sum    MD5SUMS
update_sums sha1sum   SHA1SUMS
update_sums sha256sum SHA256SUMS
