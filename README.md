# Scratch files for Hailburst development

# Installation/set-up instructions

First, clone https://github.com/celskeggs/hailburst to your machine.

Then, download and extract https://buildroot.org/downloads/buildroot-2021.02.tar.bz2 somewhere on
your system, such as to ~/Binary/buildroot-2021.02/. Make sure to use THIS EXACT VERSION!

In the hailburst repository, copy local.config.template to local.config, and fill in the buildroot
path where you installed it.

Now, you need to configure buildroot:

    $ ./make.sh hailburst_defconfig

And you can kick off the build:

    $ ./make.sh

This may take some time.
