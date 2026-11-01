# SPDX-License-Identifier: BSD-2-Clause-Patent
# Orion O6 EC firmware (ITE IT5570E, 8051), built with SDCC.
# Output: $(O)/ec_fw.bin, 128 KiB, the e-flash image (the board's ec_fw.bin).
#   make [O=dir]

SDCC	?= sdcc
SDAS	?= sdas8051
MAKEBIN	?= makebin
O	?= build
SRC	:= main.c gpio.c uart.c power.c host.c smbus.c fan.c pd.c led.c eflash.c scratch.c
OBJ	:= $(O)/vectors.rel $(addprefix $(O)/,$(SRC:.c=.rel)) $(O)/scratch_body.rel
HDR	:= src/ec.h src/it5570.h src/ntc_table.h
CFLAGS	:= -mmcs51 --model-large --std-c11 --opt-code-size --fomit-frame-pointer \
	   -Isrc --disable-warning 85
# code from 0x50 (vectors and ITE's signature below it, vectors.s), the
# e-flash routine at 0x7C00 (scratch ROM), XRAM as the vendor's
LDFLAGS	:= -mmcs51 --model-large --code-loc 0x0050 --code-size 0x7C00 \
	   --xram-loc 0x8000 --xram-size 0x1800 --iram-size 0x100 \
	   -Wl-bSCRCODE=0x7C00

all: $(O)/ec_fw.bin

$(O)/vectors.rel: src/vectors.s | $(O)
	$(SDAS) -plosgff -o $@ $<

$(O)/scratch_body.rel: src/scratch_body.c $(HDR) | $(O)
	$(SDCC) $(CFLAGS) --codeseg SCRCODE -c $< -o $@

$(O)/%.rel: src/%.c $(HDR) | $(O)
	$(SDCC) $(CFLAGS) -c $< -o $@

$(O)/ec.ihx: $(OBJ)
	$(SDCC) $(LDFLAGS) $(OBJ) -o $@

# 128 KiB, 0xFF where nothing is linked (as an erased e-flash: the AC-loss
# byte at 0x1F8FF reads 0xFF, the vendor's "power on")
$(O)/ec_fw.bin: $(O)/ec.ihx
	python3 tools/ihx2bin.py $< $@ 131072

$(O):
	mkdir -p $@

clean:
	rm -rf $(O)

.PHONY: all clean
