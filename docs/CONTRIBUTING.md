# Contributing to Opkg

Thanks for taking an interest in contributing to the [Yocto project's](https://www.yoctoproject.org/) fork of the Opkg package manager!

This document should provide useful information for how to get started developing, testing, and upstreaming to our mainline.


----
## Communication

Clone the **source** from the Yocto project's [git server](https://git.yoctoproject.org/opkg/), and read the [README](https://git.yoctoproject.org/opkg/tree/README).

Get **help** using this repository and **discuss** changes on the [opkg mailing list](https://lists.yoctoproject.org/g/opkg).

File **bugs** and **enhancement** requests to the Yocto project's [opkg bugzilla tracker](https://bugzilla.yoctoproject.org/buglist.cgi?quicksearch=Product%3Aopkg)

Send **security** concerns directly to the project **maintainer** by email: Alex Stewart <[alex.stewart@emerson.com](mailto:alex.stewart@emerson.com)>

**Historic Maintainers**
- Alejandro del Castillo <alejandro.delcastillo@ni.com>
- Paul Barker <paul@paulbarker.me.uk>
- Thomas Wood <thomas@openedhand.com>
- Tick Chen <ticktock35@gmail.com>
- Graham Gower <graham.gower@gmail.com>
- Ipkg
	- Pierluigi Frullani <pigi@frumar.it>
	- Carl Worth <cworth@handhelds.org>
	- Steve Ayer <steven.ayer@compaq.com>


----
## Building Opkg

1. Build the libsolv satisfaction solver.

[libsolv](https://github.com/openSUSE/libsolv) is a free package dependency solver, which opkg uses as its backend satisfaction solver. Though it is strictly possible to build and use opkg with its legacy "internal" solver instead, it is not well-maintained and not recommended.

You will need the libsolv development headers to build opkg. If they are not available from your distribution vendor, you can quickly build and install libsolv using the following commands on debian/ubuntu.

```bash
apt-get update && apt-get install cmake g++ git zlib1g-dev
git clone https://github.com/openSUSE/libsolv.git /usr/local/libsolv
cd /usr/local/libsolv
mkdir build && cd build
cmake \
	-DCMAKE_INSTALL_PREFIX="/usr/local/" \
	-DENABLE_COMPLEX_DEPS=on \
	-DMULTI_SEMANTICS=on \
	..
make install
ldconfig
```

**NOTE:** The libsolv `ENABLE_COMPLEX_DEPS` and `MULTI_SEMANTICS` options must be enabled for libsolv to support opkg.

2. Install opkg build requirements.

**Debian 12**
```bash
apt-get update && apt-get install -y git libarchive-dev libcurl4-openssl-dev libgpgme11-dev libssl-dev libtool-bin make pkg-config python3
```

If you want to build statically, you'll also need -dev packages which provide static libraries.
```bash
apt-get install libssh-dev libnghttp2-dev librtmp-dev libpsl-dev libkrb5-dev libldap-dev libacl1-dev liblzma-dev libbrotli-dev
```

3. Clone the repository from the Yocto project git server. Then use cmake to configure, and gnu make to build the project.
`USE_SOLVER_LIBSOLV=1` is the default, however it is shown here to demonstrate how to set build options:

```bash
git clone https://git.yoctoproject.org/opkg --branch master
cd opkg
mkdir build && cd build
cmake .. -DUSE_SOLVER_LIBSOLV=1
make
```

cmake options are set using e.g. -DOPTION=1.
You can also use cmake-gui which provides a graphical environment for the configuration. Install it with apt-get install cmake-qt-gui and call:

```bash
mkdir build && cd build
cmake-gui ..
```

4. (Optional) Install opkg to your system.

It is not required that you install opkg to test your changes, but you can do so with the following steps.
The CMAKE_INSTALL_PREFIX may be defined when configuring a build tree to set
its installation prefix. Or, when using the cmake command-line tool's
--install mode, one may specify a different prefix using the --prefix option:

```bash
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX="/usr/local/"
make
# install to prefix defined by CMAKE_INSTALL_PREFIX:
make install

# or to some other installation --prefix:
cmake --install . --prefix /my/install/prefix

# verify:
opkg --version
```


----
## Testing

Once you have *built* your change, you can validate your changes using the integrated opkg test suite, in the `:tests/` directory.

See the [`:tests/README.md`](/tests/README.md) document for information about how to run the test suite and author new tests. (tldr; run `make check`)


----
## Commit Guidelines

This project uses *commits* as the fundamental unit-of-change (not pull requests). So please follow common best practices for authoring your commits such that they are: atomic, comprehensible, and considerate of users who work in the embedded space.

**Commit Signing-offs.** Please add a signoff to each of your commits, using the `--signoff` argument to git. (eg. `git commit -s`)

**Bug Fixes.** If your change resolves a bug from the opkg bugzilla, please include a `Closes: ${bugzilla number}` trailer to your commit message.

```bash
git commit -s --trailer Closes=12345
```


----
## Submitting Changes Upstream

In order to be able to post to the mailing-list, you first need to subscribe to it at https://lists.yoctoproject.org/g/opkg/join
If you attempt to send patches to a list you haven’t subscribed to, your email will be returned as undelivered.

**Git Send-Email.** Once you have *tested* your change, you can submit it to the opkg mainline by embedding it into an email, and sending it to the opkg mailing list (<opkg@lists.yoctoproject.org>).

The easiest way to do this is using the git send-email extension (apt install git-email).
You can use the git send-email command directly, e.g. to send the latest commit of your branch:
```bash
git send-email -M -1 --to opkg@lists.yoctoproject.org
```

For the second version of a patch, you need to modify the subject-prefix:
```bash
git send-email -M -1 --to opkg@lists.yoctoproject.org --subject-prefix="opkg][PATCH v2"
```

You can also use the following commands to configure your opkg workspace with the correct defaults.

```bash
git config diff.renames copy
git config format.to "opkg@lists.yoctoproject.org"
git config format.subjectprefix "opkg][PATCH"
```

You can then create a patchset using the `git format-patch` command, and send it upstream. eg.

```bash
git format-patch origin/master..HEAD
# or
git format-patch --cover-letter origin/master..HEAD

git send-email ./*.patch
```


----
## Developer's Certificate of Origin

Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
	have the right to submit it under the open source license
	indicated in the file; or

(b) The contribution is based upon previous work that, to the best
	of my knowledge, is covered under an appropriate open source
	license and I have the right under that license to submit that
	work with modifications, whether created in whole or in part
	by me, under the same open source license (unless I am
	permitted to submit under a different license), as indicated
	in the file; or

(c) The contribution was provided directly to me by some other
	person who certified (a), (b) or (c) and I have not modified
	it.

(d) I understand and agree that this project and the contribution
	are public and that a record of the contribution (including all
	personal information I submit with it, including my sign-off) is
	maintained indefinitely and may be redistributed consistent with
	this project or the open source license(s) involved.
