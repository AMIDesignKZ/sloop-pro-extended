# DX7 ROM integration

The first two supplied Yamaha DX7 bulk dumps are stored under `assets/dx7/` as gzip-compressed SysEx source files. The build-time converter checks the standard 4,104-byte DX7 bulk-dump frame, the 7-bit voice payload and checksum, then imports all 32 voices from each ROM as native packed 128-byte FM6 voice records.

The FM6 patch selector now exposes 72 factory voices: the eight original System 37 factory sounds followed by 32 voices from ROM 1A and 32 from ROM 1B. The existing writable B1-B27 bank remains unchanged. The converter also exports the factory labels for the firmware's parameter descriptor and the browser editor's generated data path.

The original DX7 ROM source files are retained in the repository; they are never modified during generation.
