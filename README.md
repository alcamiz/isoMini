# isoMini: Simple ISO-9660 Library 

Minimal *Nix library for reading ISO-9660 filesystems. Designed to easily
expose raw filesystem structure information to the user w/o sacrificing
usability.

**Note:** Currently in alpha; while directory iteration has been fully
implemented, open/read functions for files are currently missing.

## Features:

- Read-only; use libisofs for modifying ISO images.
- Handle raw ISO filesystem headers without breaking functionality.
- Easily iterate through any directory through a simple callback system.
- Works regardless of the target system's endianness.

## Limitations:

- Currently only works with the Joliet extension.
- Assumes Joliet SVD is written on offset 0x8800 (which may not always be true).
- Only designed to work on POSIX systems.
- Defaults to a filename encoding of UTF-8, regardless of locale.
- Does not thoroughly check for ISO/ECMA standard violations.

## Planned Features:

- [ ] Add proper documentation on GitHub.
- [ ] Complete open/read support for files and directories.
- [ ] Read callback system to easily encrypt/compress data.
- [ ] Handle non-Joliet ISOs (no Rock-Ridge at the moment).
- [ ] Easier parsing of raw filesystem extents.

## Building/Testing:

To see the library's current progress, you can build the provided test
by running the following: 

```
git clone https://github.com/alcamiz/isoMini
cd isoMini
make test-iter
```

To use the resulting executable, you can run ```iso_iter <ISO_FILE>```.
This should list the contents of the provided ISO file.

## License

[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

isoMini is Free Software: You can use, study share and improve it at your
will. Specifically you can redistribute and/or modify it under the terms of the
[GNU General Public License](https://www.gnu.org/licenses/gpl.html) as
published by the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
