
Debian
====================
This directory contains files used to package concordiad/concordia-qt
for Debian-based Linux systems. If you compile concordiad/concordia-qt yourself, there are some useful files here.

## pivx: URI support ##


concordia-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install concordia-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your concordia-qt binary to `/usr/bin`
and the `../../share/pixmaps/pivx128.png` to `/usr/share/pixmaps`

concordia-qt.protocol (KDE)

