# Scratch files for Hailburst development

# Installation/set-up instructions

Under the same parent directory, clone the following repositories:
 - https://github.com/celskeggs/hailburst
 - https://github.com/celskeggs/qemu
 - https://sourceware.org/git/binutils-gdb.git

Then, download and extract https://buildroot.org/downloads/buildroot-2021.02.tar.bz2 somewhere on
your system, such as to ~/Binary/buildroot-2021.02/. Make sure to use THIS EXACT VERSION!

In the hailburst repository, copy local.config.template to local.config, and fill in the buildroot
path where you installed it.

Now, you need to configure buildroot:

    $ ./make.sh hailburst_defconfig

And you can kick off the build:

    $ ./make.sh

This may take some time.

Once that finishes, or in parallel, compile QEMU:

    $ mkdir build/
    $ cd build/
    $ ../configure --target-list=arm-softmmu
    $ make -j4

(You may need to install your distro's package for the ninja build system.)

Next, compile GDB and install it to a local directory:

    $ git checkout gdb-10.2-release
    $ mkdir ../gdbroot
    $ ./configure --enable-targets=arm-none-eabi --disable-sim --with-python=/PATH/TO/PYTHON/3/INTERPRETER --prefix=/PATH/TO/gdbroot
    $ make -j4
    $ make install

(If this doesn't work, make sure you have the crypt.h header available on your system.)
