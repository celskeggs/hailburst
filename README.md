# Scratch files for Hailburst development

# Installation/set-up instructions

Under the same parent directory, clone the following repositories:
 - https://github.com/celskeggs/hailburst
 - https://github.com/celskeggs/qemu
 - https://sourceware.org/git/binutils-gdb.git
 - https://github.com/embeddedartistry/libc (as ealibc/; remember to use --recursive)
 - https://github.com/FreeRTOS/FreeRTOS-Kernel.git @ commit 68ddb32b55be1bbe74977f79aedb8470a97b7542

Then, download and extract https://buildroot.org/downloads/buildroot-2021.02.tar.bz2 somewhere on
your system, such as to ~/Binary/buildroot-2021.02/. Make sure to use THIS EXACT VERSION!

Download and extract https://www.zlib.net/zlib-1.2.11.tar.gz such that there is a zlib-1.2.11/ directory under the same parent directory as hailburst.

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

(If this doesn't work, make sure you have the crypt.h header available on your system, and make sure you've installed the texinfo, bison, libpython3-dev, and flex packages.)
