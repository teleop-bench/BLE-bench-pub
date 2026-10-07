RECONSTRUCTION BUILDS -- NOT the exact capture binaries.
These 1M/2M ELF/HEX were built AFTER the matrix captures. The generator here
byte-differs from the matrix generator ONLY by the boot-time DEVICEID printk
(no RF / TIMER / DPPI / timing change); the observer is unchanged. They
reproduce the Q1 behavior but are not hash-identical to what produced
matrix/*. Exact matrix source: git commit d0c6717 (generator_main.c.snapshot
there predates the DEVICEID line). Build commands + sha256: ../BUILD-INFO.txt.
