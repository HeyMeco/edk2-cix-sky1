#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Write SCP-firmware's <fwk_module_idx.h> and fwk_module_list.c for a list
of modules (what its CMake build generates from SCP_MODULES).

  modules.py OUTDIR MODULE...
"""
import sys
from pathlib import Path

out, mods = Path(sys.argv[1]), sys.argv[2:]
out.mkdir(parents=True, exist_ok=True)
idx = ["#ifndef FWK_MODULE_IDX_H", "#define FWK_MODULE_IDX_H", "", "#include <fwk_id.h>", ""]
for m in mods:
    u = m.upper()
    idx.append(f"#define FWK_MODULE_ID_{u}_INIT FWK_ID_MODULE_INIT(FWK_MODULE_IDX_{u})")
    idx.append(f"#define FWK_MODULE_ID_{u} FWK_ID_MODULE(FWK_MODULE_IDX_{u})")
idx += ["", "enum fwk_module_idx {"]
idx += [f"\tFWK_MODULE_IDX_{m.upper()} = {i}," for i, m in enumerate(mods)]
idx += [f"\tFWK_MODULE_IDX_COUNT = {len(mods)},", "};", ""]
idx += [f"static const fwk_id_t fwk_module_id_{m} = FWK_MODULE_ID_{m.upper()}_INIT;" for m in mods]
idx += ["", "#endif", ""]
lst = ["#include <fwk_module.h>", "#include <fwk_module_idx.h>", "#include <stddef.h>", ""]
lst += [f"extern const struct fwk_module module_{m};" for m in mods]
lst += [f"extern const struct fwk_module_config config_{m};" for m in mods]
lst += ["", "const struct fwk_module *module_table[FWK_MODULE_IDX_COUNT] = {"]
lst += [f"\t&module_{m}," for m in mods] + ["};", ""]
lst += ["const struct fwk_module_config *module_config_table[FWK_MODULE_IDX_COUNT] = {"]
lst += [f"\t&config_{m}," for m in mods] + ["};", ""]


def write(p, text):
    if not p.exists() or p.read_text() != text:
        p.write_text(text)


write(out / "fwk_module_idx.h", "\n".join(idx))
write(out / "fwk_module_list.c", "\n".join(lst))
