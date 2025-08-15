tcwfutil
========

``tcwfutil`` is a utility for compressing and decompressing TCWF
(``.tcw``) audio files.

TCWF
====

TCWF is a proprietary audio compression format which compresses 16 bit
wave format to 8 bit wave format, resulting in half the file size.

Regarding sound quality
=======================

Because it is a lossy format, the sound quality will be slightly
degraded. Noise is added according to the volume of the sound. It may be
difficult to hear if it is a noisy song. Most people can’t tell the
difference unless you compare it with uncompressed wave format.

For the time being, please use it because it is a format with high sound
quality that you can use with peace of mind (without being bothered by
patents).

Usage
=====

| Specify one or more audio files supported by
  `libsndfile <https://github.com/erikd/libsndfile>`__ to be compressed
  or decompressed on the command line.
| On Windows, files can be dropped on the executable in Windows
  Explorer.

Acknowledgment
==============

| `libsndfile <https://github.com/erikd/libsndfile>`__ is used to read
  and write audio files.
| This project is based on ``tcwfcomp`` and ``wutcwf`` from `Kirikiri
  2 <https://github.com/krkrz/krkr2>`__.

Algorithm
=========

It is not decomposed into frequency components or anything like that.

Two types of 4bit ADPCM are used in combination.

First, Microsoft ADPCM is used to compress the source. At this point,
there will be some margin of error from the original source. The error
is compressed using IMA ADPCM. However, even in this state, the error
that could not be corrected remains as sharp pulse-like noise (squashing
noise), so this is detected and corrected.

Data is divided into small blocks and compressed to enable seeking.
Because the header is generated for each block, the compressed file will
be slightly larger than half the source file.

License
=======

This project is licensed under the GNU Public License v2 (GPLv2), the
same license as Kirikiri 2. Please see the file ``LICENSE`` for more
details.
