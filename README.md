# TACAMan, the T+A Cover Art Daemon

## Copyright and contact

TACAMan is released under the terms of the GNU General Public License version 3
(GPLv3). See file <tt>COPYING</tt> for licensing terms.

Contact:

    T+A elektroakustik GmbH & Co. KG
    Planckstrasse 11
    32052 Herford
    Germany

## Short description

The _tacaman_ daemon implements a cover art format conversion and caching
service. Cover art (pictures) sent to the daemon is converted to a specified
output format. The converted pictures are stored on file(s) and are associated
with keys for later retrieval.

Several pictures may be stored for the same key, distinguished by a numeric
rank, or _priority_. This allows updating of cover art from various source
without running at risk of replacing the "best", or most-trusted, picture by
pictures from bogus sources. It would also, in theory, allow cycling through
the various pictures.

The software is written in C++11.

## Communication with other system processes

The daemon uses _D-Bus_ for IPC. Any process may call functions for adding new
pictures and retrieving them. For daemons that need to react on changes of
pictures, a monitoring interface is provided.

All _D-Bus_ functions may be called synchronously as they will never block.
Downloads, image conversions, or other lengthy tasks are processed in the
background. Their results are communicated as signals through the monitoring
interface.

## Cache organization

All incoming pictures are converted to a single, fixed format. However,
measures have been taken to support multiple output formats in the future if
and when needed.

The whole cache is stored in a directory hierarchy below a directory that we'll
refer to as `CACHEDIR` in the rest of this document. The structure stored in
that directory makes heavy use of hardlinks, so it will not work on _vfat_ or
similar simplistic file systems.

Pictures are associated with a stream by a pair of string key (called the
_stream key_) and priority (numbers in range 1 through 255). Stream keys are to
be generated and provided by users of the cache. The structure of these keys is
not really prescribed by _tacaman_, except that it expects keys to be longer
than 2 characters from the range `[0-9a-f]`. In fact, a stream key might be a
hash of any kind computed from something derived from the stream, but as well
it might be just a plain random number. The priority determines the
trustworthiness, where higher numbers mean higher degree of trust. There can be
multiple pictures for the same stream key, and they may be pushed to _tacaman_
from different sources that do not communicate with each other. Using
priorities effectively prevents overwriting pictures passed from different
sources, and they help _tacaman_ selecting the best available picture when
being asked for it.

Each picture also has an associated source, which is either a download URI or
raw image data bytes. Pictures that are passed by URI are downloaded by
_tacaman_ in the background. Sources are associated with the converted
pictures.

Expressed in mathematical terms, the cache provides an efficient mapping from
stream key and priority tuple _(K, P)_ to image source hash _S_. Multiple such
tuples can map to the same source. Each source maps to a set of converted
output pictures _C<sub>1</sub>, C<sub>2</sub>, ..._, where multiple sources may
map to the same output picture.

* _(K, P) -> S_
* _S -> { C<sub>i</sub> }_

The cache is organized on top of the regular file system. There are three kinds
of directories:

1. Stream keys. There is one directory per stream key added to the cache.
2. Picture sources. There will be one such directory for each input picture
   added to the cache.
3. Converted pictures. All converted pictures are stored there.

Each stream key directory contains one directory per priority, each of which
containing an empty file. Its name contains the hash of the original input
picture source (MD5 sum of either the picture URI or the picture raw data). The
file is a hardlink of a file used for reference counting the picture source.
Another hardlink to the same file is stored in the picture source directory
matching the hash found in the file name under the name `.ref`.

Note that in the current implementation there is always only one file stored in
any priority directory. It may seem wasteful to have all those extra
directories around, but they are there to allow simple future extension to
multiple files per priority, maybe to support sets of sources or for storing
extra information not planned for at the moment.

Each picture source directory contains a `.ref` file and a bunch of files
containing the converted pictures. The `.ref` file is used for reference
counting the directory. As long as there are stream keys that reference the
source, its reference count will be greater than one. The names of the picture
files contain the image format and an MD5 hash of the file content. That is,
their checksum can be read directly from the file name so it only ever needs to
be computed once.

Hardlinks to the same picture files are also stored in the pool of converted
pictures. Their names only reflect their MD5 hash for quick lookup. Once the
reference count of any picture drops to one, it is may be deleted from the
cache since there is no source which references it anymore.

Only the converted files are cached on file system, the original pictures are
discarded after conversion as their size might be arbitrarily large.

The cache management code always works directly on the file system and avoids
reflecting the directory hierarchy in RAM. Only a minimal amount of data about
the cache is held in RAM. The reason for this is that the kernel's file system
cache will already have the directory hierarchy stored in RAM. First access to
the cache will be slow and I/O bound, but successive accesses will be much
faster and bound by speed of memory and CPU, and mainly by the cost of context
switches as any cache operation will involve a few system calls. This approach
makes optimal use of RAM since the kernel may purge its file system cache when
needed.

### Examples

The examples show how the cache would look like if there would be three
different output formats configured. In the current implementation, however,
only a single format is supported.

Playing some Internet radio stream A. The cache entry for the stream key is
created.

    AddImageByURI("64ef367018099de4d4183ffa3bc0848a", 1,
                  "http://here.is.my/station_logo.jpeg")

    [2] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@120x120:cb5a19a315017b74b33c72777f11d3e5  # image cb5a19a
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/jpg@400x400:a0beaa1ff00ac6560f3c1ee96a0c5733  # image a0beaa1
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@64x64:6bba413f8dfa44312fd38c2541496b4c    # image 6bba413
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/.ref [2]
    [2] CACHEDIR/.obj/6b/ba413f8dfa44312fd38c2541496b4c
    [2] CACHEDIR/.obj/a0/beaa1ff00ac6560f3c1ee96a0c5733
    [2] CACHEDIR/.obj/cb/5a19a315017b74b33c72777f11d3e5

There is also an Internet radio stream B referring to the same content as A,
but possibly using a different codec or bit rate. Both streams refer to the
exact same input image source. The input picture is not downloaded again
because the hash of the picture URI matches the hash of a known source
(`5d952936adb5f5e47a85c7fb61a1379d`). All that needs to be done is creating a
hardlink to the source's `.ref` file.

    AddImageByURI("99e300cedfe681df76ffc7b678e88afa", 1,
                  "http://here.is.my/station_logo.jpeg")

    [3] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [3] CACHEDIR/99/e300cedfe681df76ffc7b678e88afa/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@120x120:cb5a19a315017b74b33c72777f11d3e5  # image cb5a19a
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/jpg@400x400:a0beaa1ff00ac6560f3c1ee96a0c5733  # image a0beaa1
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@64x64:6bba413f8dfa44312fd38c2541496b4c    # image 6bba413
    [3] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/.ref
    [2] CACHEDIR/.obj/6b/ba413f8dfa44312fd38c2541496b4c
    [2] CACHEDIR/.obj/a0/beaa1ff00ac6560f3c1ee96a0c5733
    [2] CACHEDIR/.obj/cb/5a19a315017b74b33c72777f11d3e5

Cover art for A is extracted from the currently playing stream. It might
contain the station logo, or it might be a picture matching the currently
playing song. It's hash `b4476bf6f427f2f2784664dee58a8f97` is not a known
source, so it is added to `.src`. The provided picture is converted. The
resulting pictures are added to the object pool (their hashes are different
from what's there, otherwise the converted pictures could be discarded) and
hardlinks are added to the source.

    AddImageByData("64ef367018099de4d4183ffa3bc0848a", 255,
                   [array of raw picture data bytes])

    [3] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [2] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/255/src:b4476bf6f427f2f2784664dee58a8f97           # source b4476bf
    [3] CACHEDIR/99/e300cedfe681df76ffc7b678e88afa/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@120x120:cb5a19a315017b74b33c72777f11d3e5  # image cb5a19a
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/jpg@400x400:a0beaa1ff00ac6560f3c1ee96a0c5733  # image a0beaa1
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@64x64:6bba413f8dfa44312fd38c2541496b4c    # image 6bba413
    [3] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/.ref
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/png@120x120:569cf7dd27d8898c9d7d7e26c7c90d2c  # image 569cf7d
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/jpg@400x400:73b2c0437b9c539882b831069fe96b64  # image 73b2c04
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/png@64x64:83fcf06d7159f2ef38e4c1d08c7d85c7    # image 83fcf06
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/.ref
    [2] CACHEDIR/.obj/56/9cf7dd27d8898c9d7d7e26c7c90d2c
    [2] CACHEDIR/.obj/6b/ba413f8dfa44312fd38c2541496b4c
    [2] CACHEDIR/.obj/73/b2c0437b9c539882b831069fe96b64
    [2] CACHEDIR/.obj/83/fcf06d7159f2ef38e4c1d08c7d85c7
    [2] CACHEDIR/.obj/a0/beaa1ff00ac6560f3c1ee96a0c5733
    [2] CACHEDIR/.obj/cb/5a19a315017b74b33c72777f11d3e5

Cover art for A is passed in by another source. It's hash is
`7dca08ebbcd6e557dee80f4a0eb04abe`. It is rated as less trustable as the
picture extracted from the stream, so it is not considered for display. Anyway,
the converted pictures are stored for future reference. Turns out that from the
converted pictures the smallest of them is exactly the same as is already
stored in the object pool.

    AddImageByURI("64ef367018099de4d4183ffa3bc0848a", 50,
                  "http://hq-radio.logos.my/logos/station_A.png")

    [3] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [2] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/050/src:7dca08ebbcd6e557dee80f4a0eb04abe           # source 7dca08e
    [2] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/255/src:b4476bf6f427f2f2784664dee58a8f97           # source b4476bf
    [3] CACHEDIR/99/e300cedfe681df76ffc7b678e88afa/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@120x120:cb5a19a315017b74b33c72777f11d3e5  # image cb5a19a
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/jpg@400x400:a0beaa1ff00ac6560f3c1ee96a0c5733  # image a0beaa1
    [3] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@64x64:6bba413f8dfa44312fd38c2541496b4c    # image 6bba413
    [3] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/.ref
    [2] CACHEDIR/.src/7d/ca08ebbcd6e557dee80f4a0eb04abe/png@120x120:8bd7eed2a98817d4ad30485b4f29ee44  # image 8bd7eed
    [2] CACHEDIR/.src/7d/ca08ebbcd6e557dee80f4a0eb04abe/jpg@400x400:f17ee700b01a577ed1d1d396c014674b  # image f17ee70
    [3] CACHEDIR/.src/7d/ca08ebbcd6e557dee80f4a0eb04abe/png@64x64:6bba413f8dfa44312fd38c2541496b4c    # image 6bba413
    [2] CACHEDIR/.src/7d/ca08ebbcd6e557dee80f4a0eb04abe/.ref
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/png@120x120:569cf7dd27d8898c9d7d7e26c7c90d2c  # image 569cf7d
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/jpg@400x400:73b2c0437b9c539882b831069fe96b64  # image 73b2c04
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/png@64x64:83fcf06d7159f2ef38e4c1d08c7d85c7    # image 83fcf06
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/.ref
    [2] CACHEDIR/.obj/56/9cf7dd27d8898c9d7d7e26c7c90d2c
    [3] CACHEDIR/.obj/6b/ba413f8dfa44312fd38c2541496b4c
    [2] CACHEDIR/.obj/73/b2c0437b9c539882b831069fe96b64
    [2] CACHEDIR/.obj/83/fcf06d7159f2ef38e4c1d08c7d85c7
    [2] CACHEDIR/.obj/8b/d7eed2a98817d4ad30485b4f29ee44
    [2] CACHEDIR/.obj/a0/beaa1ff00ac6560f3c1ee96a0c5733
    [2] CACHEDIR/.obj/cb/5a19a315017b74b33c72777f11d3e5
    [2] CACHEDIR/.obj/f1/7ee700b01a577ed1d1d396c014674b

Internet radio station logo is set again for A. The image is not downloaded
because the hash of the given source matches a known source
(`5d952936adb5f5e47a85c7fb61a1379d`). The entry for stream key
`64ef367018099de4d4183ffa3bc0848a` with priority 1 is not touched because the
hash of the source stored for that key/priority pair is equal to the hash of
the given source. Stream key `64ef367018099de4d4183ffa3bc0848a` becomes the
least likely key to be removed from the cache, but other than that nothing
changes.

    AddImageByURI("64ef367018099de4d4183ffa3bc0848a", 1,
                  "http://here.is.my/station_logo.jpeg")

Another picture is extracted from the playing stream. It is different from
anything seen before. The source hash is `db1a895d2d4fa573e924c69e07f007b7`,
replacing the old source hash. The reference count of the .ref file of the old
source `b4476bf6f427f2f2784664dee58a8f97` drops to 1, so it becomes a candidate
for immediate removal from the cache. Next time the cache is cleaned up, the
entry will be removed, along with the converted pictures inside the source
directory. This means that the reference counts of the deleted picture files
still present in the object pool will drop to 1 as well, so they may (but do
not _have_ to) be removed from there as well.

    AddImageByData("64ef367018099de4d4183ffa3bc0848a", 255,
                   [array of raw picture data bytes])

    [3] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [2] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/050/src:7dca08ebbcd6e557dee80f4a0eb04abe           # source 7dca08e
    [2] CACHEDIR/64/ef367018099de4d4183ffa3bc0848a/255/src:db1a895d2d4fa573e924c69e07f007b7           # source db1a895
    [3] CACHEDIR/99/e300cedfe681df76ffc7b678e88afa/001/src:5d952936adb5f5e47a85c7fb61a1379d           # source 5d95293
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@120x120:cb5a19a315017b74b33c72777f11d3e5  # image cb5a19a
    [2] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/jpg@400x400:a0beaa1ff00ac6560f3c1ee96a0c5733  # image a0beaa1
    [3] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/png@64x64:6bba413f8dfa44312fd38c2541496b4c    # image 6bba413
    [3] CACHEDIR/.src/5d/952936adb5f5e47a85c7fb61a1379d/.ref
    [2] CACHEDIR/.src/7d/ca08ebbcd6e557dee80f4a0eb04abe/png@120x120:8bd7eed2a98817d4ad30485b4f29ee44  # image 8bd7eed
    [2] CACHEDIR/.src/7d/ca08ebbcd6e557dee80f4a0eb04abe/jpg@400x400:f17ee700b01a577ed1d1d396c014674b  # image f17ee70
    [3] CACHEDIR/.src/7d/ca08ebbcd6e557dee80f4a0eb04abe/png@64x64:6bba413f8dfa44312fd38c2541496b4c    # image 6bba413
    [2] CACHEDIR/.src/7d/ca08ebbcd6e557dee80f4a0eb04abe/.ref
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/png@120x120:569cf7dd27d8898c9d7d7e26c7c90d2c  # image 569cf7d
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/jpg@400x400:73b2c0437b9c539882b831069fe96b64  # image 73b2c04
    [2] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/png@64x64:83fcf06d7159f2ef38e4c1d08c7d85c7    # image 83fcf06
    [1] CACHEDIR/.src/b4/476bf6f427f2f2784664dee58a8f97/.ref
    [2] CACHEDIR/.src/db/1a895d2d4fa573e924c69e07f007b7/png@120x120:9172e78df32a631e747d9e11b015cb44  # image 9172e78
    [2] CACHEDIR/.src/db/1a895d2d4fa573e924c69e07f007b7/jpg@400x400:4448d24679cbeada3cad626ca7064e4c  # image 4448d24
    [2] CACHEDIR/.src/db/1a895d2d4fa573e924c69e07f007b7/png@64x64:d2449cee526348ce40df8f20c639eefb    # image d2449ce
    [2] CACHEDIR/.src/db/1a895d2d4fa573e924c69e07f007b7/.ref
    [2] CACHEDIR/.obj/44/48d24679cbeada3cad626ca7064e4c
    [2] CACHEDIR/.obj/56/9cf7dd27d8898c9d7d7e26c7c90d2c
    [3] CACHEDIR/.obj/6b/ba413f8dfa44312fd38c2541496b4c
    [2] CACHEDIR/.obj/73/b2c0437b9c539882b831069fe96b64
    [2] CACHEDIR/.obj/83/fcf06d7159f2ef38e4c1d08c7d85c7
    [2] CACHEDIR/.obj/8b/d7eed2a98817d4ad30485b4f29ee44
    [2] CACHEDIR/.obj/91/72e78df32a631e747d9e11b015cb44
    [2] CACHEDIR/.obj/a0/beaa1ff00ac6560f3c1ee96a0c5733
    [2] CACHEDIR/.obj/cb/5a19a315017b74b33c72777f11d3e5
    [2] CACHEDIR/.obj/d2/449cee526348ce40df8f20c639eefb
    [2] CACHEDIR/.obj/f1/7ee700b01a577ed1d1d396c014674b

## Cache configuration

Tunable limits are

* Maximum number of stream keys (_#K_).
* Maximum number of pictures sources (_#S_).
* Maximum number of converted pictures (_#C_).

Defaults:

* _#K = 300_
* _#S = 2 * #K_
* _#C = #F * #S_

where _#F_ is the number of output formats (which is always 1 in the current
implementation). Assuming the single output format to be PNG at 120x120 pixels,
each converted picture typically takes less than 15 kiB, so the default
settings should keep the total cache size below 10 MiB.
