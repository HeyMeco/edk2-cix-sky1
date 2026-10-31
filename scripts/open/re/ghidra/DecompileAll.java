// SPDX-License-Identifier: BSD-2-Clause-Patent
// Ghidra headless post-script: decompile every function of the imported
// program (an ELF with its own symbols and DWARF) into <out>.c, each
// preceded by "// ADDRESS NAME size N" (the format show.py reads).
//
//   analyzeHeadless DIR PROJ -import FIRMWARE.elf -processor Xtensa:LE:32:default \
//     -postScript DecompileAll.java OUT
//@category CIX

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompileAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter all = new PrintWriter(args[0] + ".c")) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                DecompileResults r = d.decompileFunction(f, 60, monitor);
                String c = r.decompileCompleted() ? r.getDecompiledFunction().getC() : "/* decompile failed */\n";
                all.print("// " + f.getEntryPoint() + " " + f.getName() + " size "
                          + f.getBody().getNumAddresses() + "\n" + c + "\n");
            }
        }
    }
}
