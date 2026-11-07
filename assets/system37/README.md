# System 37 source assets

- `system37-icon.svg` and `system37-logo.svg` are the supplied System 37 vector identity assets.
- `../dx7/rom1a.syx.gz` and `rom1b.syx.gz` are the first two supplied Yamaha DX7 32-voice bulk ROM dumps, gzip-compressed to keep the repository compact.

The build generator validates each DX7 SysEx header, 7-bit payload, 32-voice size, and checksum before adding the 64 voices to the FM6 factory patch table. The original eight System 37 patches remain at the start of the table; the existing writable B1-B27 user bank remains after the factory voices.
